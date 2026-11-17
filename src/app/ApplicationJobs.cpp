#include "app/Application.hpp"

#include <algorithm>
#include <unordered_set>

namespace ttplayer::app {
    void Application::loadLyrics(const std::filesystem::path &path) {
        lyrics_ = {};
        try {
            lyrics_ = lyricsRepository_.load(nowSong_.path, path);
            const auto offset = settings_.lyricOffsets.find(utf8(nowSong_.path));
            if (offset != settings_.lyricOffsets.end()) lyrics_.offset = offset->second;
        } catch (const std::exception &error) {
            status_ = error.what();
        }
    }

    void Application::importFiles(std::vector<std::filesystem::path> files, const bool play) {
        if (!importer_.start({std::move(files), settings_.playlist, play})) {
            status_ = tr("正在导入，请稍候", "Import is already running");
            return;
        }
        status_ = tr("正在读取歌曲信息…", "Reading audio metadata...");
        auto &window = view("progress");
        SDL_SetWindowTitle(window.window, message("dialog.loadingSongs").c_str());
        SDL_SetWindowResizable(window.window, false);
        SDL_SetWindowAlwaysOnTop(window.window, true);
        SDL_SetWindowPosition(window.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        if (!smoke_) window.show(true);
    }

    void Application::startAutomaticLyrics() {
        if (!settings_.autoLyrics || smoke_ || !lyrics_.lines.empty() || nowSong_.path.empty() || lyricRequests_.busy())
            return;
        const auto query = nowSong_.artist.empty() ? nowSong_.title : nowSong_.artist + " " + nowSong_.title;
        if (!query.empty()) lyricRequests_.findAutomatically(nowSong_.path, query);
    }

    void Application::startSearch() {
        if (lyricRequests_.busy()) {
            status_ = tr("歌词请求进行中…", "A lyric request is running...");
            return;
        }
        if (query_.empty()) query_ = nowSong_.artist + " " + nowSong_.title;
        if (query_.size() < 2) return;
        status_ = tr("正在搜索在线歌词…", "Searching online lyrics...");
        lyricRequests_.search(query_);
    }

    void Application::fetchLyric(const int index) {
        if (lyricRequests_.busy() || nowSong_.path.empty() || index < 0 || index >= static_cast<int>(searchResults_.
                size())) return;
        LyricRequestService::DownloadRequest request;
        request.id = searchResults_[static_cast<std::size_t>(index)].id;
        request.song = nowSong_.path;
        request.destination = panelKind_ == "lyrics" && view("dialog").visible()
                                  ? fromUtf8(panelSavePath_)
                                  : std::filesystem::path{};
        request.associate = associateLyrics_;
        status_ = tr("正在下载歌词…", "Downloading lyrics...");
        lyricRequests_.download(std::move(request));
    }

    void Application::pollJobs() {
        if (auto result = importer_.poll()) completeImport(std::move(*result));
        if (auto result = lyricRequests_.poll()) completeLyricRequest(std::move(*result));
        for (auto &result: fileDialogs_.takeResults()) completeFileDialog(std::move(result));
        if (!importer_.busy() && !pendingDropImports_.empty()) {
            auto files = std::move(pendingDropImports_);
            pendingDropImports_.clear();
            importFiles(std::move(files), true);
        }
    }

    void Application::completeImport(MediaImportService::Result result) {
        view("progress").show(false);
        suspendedVisibility_.erase("progress");
        const int target = std::clamp(result.targetPlaylist, 0, static_cast<int>(settings_.lists.size()) - 1);
        auto &songs = settings_.lists[static_cast<std::size_t>(target)].songs;
        std::unordered_set<std::string> seen;
        for (const auto &song: songs) seen.insert(utf8(song.path.lexically_normal()));
        int first = -1;
        for (auto &song: result.songs) {
            if (!seen.insert(utf8(song.path.lexically_normal())).second) {
                if (first < 0) {
                    const auto existing = std::find_if(songs.begin(), songs.end(), [&](const auto &item) {
                        return item.path == song.path;
                    });
                    if (existing != songs.end()) first = static_cast<int>(existing - songs.begin());
                }
                continue;
            }
            if (first < 0) first = static_cast<int>(songs.size());
            songs.push_back(std::move(song));
        }
        const int firstImportedList = static_cast<int>(settings_.lists.size());
        for (auto &list: result.lists) settings_.lists.push_back(std::move(list));
        if (!result.lists.empty()) {
            settings_.playlist = firstImportedList;
            selectedSongs_.clear();
            selected_ = selectionAnchor_ = -1;
            view("playlist").scroll = 0;
        }
        status_ = result.error.empty() ? tr("导入完成", "Import complete") : result.error;
        if (result.playAfterImport && first >= 0) playSong(target, first);
        else if (result.playAfterImport && !result.lists.empty() && !settings_.lists[firstImportedList].songs.empty())
            playSong(firstImportedList, 0);
        dirty_ = true;
    }

    void Application::completeLyricRequest(LyricRequestService::Result result) {
        if (!result.error.empty()) {
            status_ = result.error;
            return;
        }
        if (!result.downloaded) {
            searchResults_ = std::move(result.results);
            view("settings").scroll = 0;
            status_ = tr("点击结果下载歌词", "Click a result to download lyrics");
            return;
        }
        Lyrics parsed;
        parsed.parse(result.lyrics);
        if (parsed.lines.empty()) {
            status_ = tr("服务器未返回带时间轴的歌词", "No timed lyrics in the server response");
            return;
        }
        try {
            lyricsRepository_.store(result.song, result.lyrics, result.destination, result.associate);
            if (result.song == nowSong_.path) {
                lyrics_ = std::move(parsed);
                const auto offset = settings_.lyricOffsets.find(utf8(nowSong_.path));
                if (offset != settings_.lyricOffsets.end()) lyrics_.offset = offset->second;
            }
            status_ = tr("歌词已缓存到用户配置目录", "Lyrics cached in your configuration directory");
        } catch (const std::exception &error) {
            status_ = error.what();
        }
    }

    void Application::completeFileDialog(FileDialogService::Result result) {
        if (!result.error.empty()) {
            status_ = result.error;
            return;
        }
        if (result.files.empty()) return;
        try {
            const auto &file = result.files.front();
            switch (result.kind) {
                case FileDialogKind::ExportPlaylist:
                    exportM3u(file, playlist().songs);
                    status_ = tr("已导出 M3U8", "Exported M3U8");
                    break;
                case FileDialogKind::ExportAllPlaylists:
                    saveAllLists(file);
                    break;
                case FileDialogKind::ChooseLyricDestination:
                    panelSavePath_ = utf8(file);
                    break;
                case FileDialogKind::OpenLyrics:
                    loadLyrics(file);
                    break;
                case FileDialogKind::OpenSkin:
                    applySkin(file);
                    break;
                case FileDialogKind::ExportLyrics:
                    writeFileAtomic(file, lyrics_.serialize());
                    status_ = tr("歌词已导出", "Lyrics exported");
                    break;
                case FileDialogKind::OpenAudio:
                case FileDialogKind::AddFolder:
                case FileDialogKind::ImportPlaylists:
                    importFiles(std::move(result.files), result.kind == FileDialogKind::OpenAudio);
                    break;
            }
        } catch (const std::exception &error) {
            status_ = error.what();
        }
    }

    void Application::showDialog(const FileDialogKind kind) {
        if (!suspendedVisibility_.empty()) restoreSuspendedWindows();
        fileDialogs_.show(kind, dialogOwner().window, panelSavePath_);
    }
}
