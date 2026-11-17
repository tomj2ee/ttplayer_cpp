#include "app/FileDialogService.hpp"

namespace ttplayer::app {
    FileDialogService::~FileDialogService() {
        close();
    }

    void SDLCALL FileDialogService::selectedFiles(void *userdata, const char *const*files, int) {
        const std::unique_ptr<RequestContext> request(static_cast<RequestContext *>(userdata));
        const auto state = request->state.lock();
        if (!state) return;
        Result result{request->kind, {}, {}};
        try {
            if (!files) result.error = SDL_GetError();
            else {
                for (std::size_t index = 0; index < 100000 && files[index]; ++index) {
                    result.files.push_back(fromUtf8(files[index]));
                }
            }
        } catch (const std::exception &error) {
            result.error = error.what();
        }
        std::scoped_lock lock(state->mutex);
        if (state->accepting) state->results.push_back(std::move(result));
    }

    void FileDialogService::show(const FileDialogKind kind, SDL_Window *parent, std::string lyricSavePath) {
        {
            std::scoped_lock lock(state_->mutex);
            if (!state_->accepting) return;
        }
        static const SDL_DialogFileFilter audio[]{
            {"Audio / 音频", "mp3;flac;wav;ogg;opus;wma;aac;m4a;mp4;ape;wv;aiff;aif"}, {"All / 所有文件", "*"}
        };
        static const SDL_DialogFileFilter lists[]{{"Playlist / 播放列表", "xml;m3u;m3u8;pls"}};
        static const SDL_DialogFileFilter m3u[]{{"M3U8", "m3u8"}};
        static const SDL_DialogFileFilter lrc[]{{"Lyrics / 歌词", "lrc"}};
        static const SDL_DialogFileFilter skn[]{{"TTPlayer Skin", "skn"}};
        auto request = std::make_unique<RequestContext>(RequestContext{state_, kind, std::move(lyricSavePath)});
        if (kind == FileDialogKind::ExportPlaylist) request->defaultPath = "playlist.m3u8";
        else if (kind != FileDialogKind::ChooseLyricDestination || request->defaultPath.empty())
            request->defaultPath = "lyrics.lrc";
        const auto *defaultPath = request->defaultPath.c_str();
        auto *userdata = request.release();
        switch (kind) {
            case FileDialogKind::AddFolder:
            case FileDialogKind::ExportAllPlaylists:
                SDL_ShowOpenFolderDialog(selectedFiles, userdata, parent, nullptr, kind == FileDialogKind::AddFolder);
                break;
            case FileDialogKind::ExportPlaylist:
                SDL_ShowSaveFileDialog(selectedFiles, userdata, parent, m3u, 1, defaultPath);
                break;
            case FileDialogKind::ExportLyrics:
            case FileDialogKind::ChooseLyricDestination:
                SDL_ShowSaveFileDialog(selectedFiles, userdata, parent, lrc, 1, defaultPath);
                break;
            default:
                SDL_ShowOpenFileDialog(selectedFiles, userdata, parent,
                                       kind == FileDialogKind::ImportPlaylists
                                           ? lists
                                           : kind == FileDialogKind::OpenLyrics
                                                 ? lrc
                                                 : kind == FileDialogKind::OpenSkin
                                                       ? skn
                                                       : audio,
                                       kind == FileDialogKind::OpenAudio ? 2 : 1, nullptr,
                                       kind == FileDialogKind::OpenAudio || kind == FileDialogKind::ImportPlaylists);
                break;
        }
    }

    std::vector<FileDialogService::Result> FileDialogService::takeResults() {
        std::vector<Result> results;
        std::scoped_lock lock(state_->mutex);
        results.swap(state_->results);
        return results;
    }

    void FileDialogService::close() {
        std::scoped_lock lock(state_->mutex);
        state_->accepting = false;
        state_->results.clear();
    }
}
