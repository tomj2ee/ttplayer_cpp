#include "app/Application.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <sstream>


namespace ttplayer::app {
    namespace {
        constexpr std::array<std::array < float, 10>
        ,
        13
        >
        presets {
            {
                {
                    -1, 2, 0, 0, 1, 1, 0, 0, 0, 0
                },
                {
                    4, 2, 0, -2, -1, 2, 3, 3, 3, 3
                },
                {
                    5, 3, 0, -1, -1, 1, 3, 4, 4, 4
                },
                {
                    5, 3, 1, 0, 0, -1, -1, -1, -2, -2
                },
                {
                    4, 2, 0, -1, 0, 1, 2, 3, 3, 4
                },
                {
                    0, 0, 0, 1, 1, 1, -1, -1, 0, -1
                },
                {
                    2, 1, -1, -1, -1, 0, 1, 1, 2, 2
                },
                {
                    3, 2, 1, 0, -1, -1, 0, 0, 1, 1
                },
                {
                    4, 3, 1, 0, -1, -1, 0, 1, 1, 2
                },
                {
                    1, 1, 0, 0, 0, -1, -1, 0, 0, 0
                },
                {
                    2, 1, 0, 0, -1, 0, 1, 1, 2, 2
                },
                {
                    -2, -2, -1, 0, 2, 3, 2, 0, -1, -2
                },
                {
                    4, 2, 0, 0, 0, 0, 0, -1, -1, -2
                }
            }
        };
    }

    std::vector<Application::MenuItem> Application::contextItems(const int kind) {
        switch (kind) {
            case 0: return {
                    {message("menu.addFile"), "open"}, {message("menu.addFolder"), "folder"}, {},
                    {message("menu.addUrl"), "add_url"}, {message("menu.addSearchResult"), "add_search"}
                };
            case 1: return {
                    {message("menu.deleteSelected"), "delete"}, {message("menu.deleteNotSelected"), "delete_other"}, {},
                    {message("menu.deleteDuplicates"), "delete_duplicates"}, {message("menu.clearList"), "clear_list"}
                };
            case 2: return {
                    {message("menu.switchList"), {}, false, contextItems(12)}, {},
                    {message("menu.newList"), "new_list"}, {message("menu.addList"), "import"}, {},
                    {message("menu.saveList"), "export"}, {message("menu.deleteList"), "delete_list"},
                    {message("menu.saveAllLists"), "save_all"}, {}, {message("menu.rename"), "rename_list"},
                    {message("menu.sortByName"), "sort_lists"}
                };
            case 3: return {
                    {message("menu.sortByTitle"), "sort_title"}, {message("menu.sortByArtist"), "sort_artist"},
                    {message("menu.sortByFilename"), "sort_filename"}, {message("menu.sortByPath"), "sort_path"}, {},
                    {message("menu.reverse"), "sort_reverse"}, {message("menu.randomSort"), "sort_random"}
                };
            case 4: return {
                    {message("menu.findDot"), "find"}, {message("menu.findNext"), "find_next"}, {},
                    {message("menu.locateCurrent"), "locate"}
                };
            case 5: return {
                    {message("menu.playlistOptions"), "playlist_options"}, {},
                    {message("menu.batchFileInfo"), "batch_info"}, {message("menu.filenameFormat"), "filename_format"},
                    {tr("全选", "Select all"), "select_all"}, {tr("反选", "Invert selection"), "select_invert"}, {},
                    {message("menu.language"), {}, false, contextItems(13)}
                };
            case 6: {
                const char *chinese[]{"单曲播放", "单曲循环", "顺序播放", "列表循环", "随机播放"};
                const char *english[]{"Single track", "Repeat track", "Sequential", "Repeat list", "Random"};
                std::vector<MenuItem> items;
                for (int index = 0; index < 5; ++index) items.push_back({
                    tr(chinese[index], english[index]), "mode:" + std::to_string(index), settings_.mode == index
                });
                items.push_back({});
                items.push_back({message("menu.pauseAfterPlay"), "pause_after", settings_.pauseAfterPlay});
                return items;
            }
            case 7: return {
                    {message("menu.add"), {}, false, contextItems(0)},
                    {message("menu.delete"), {}, false, contextItems(1)},
                    {message("menu.sort"), {}, false, contextItems(3)},
                    {message("menu.find"), {}, false, contextItems(4)},
                    {message("menu.mode"), {}, false, contextItems(6)},
                    {message("menu.options"), {}, false, contextItems(5)}, {},
                    {message("menu.skin"), "skin"}, {message("menu.options_"), "preferences"}, {},
                    {message("menu.lyricShow"), "lyric", view("lyric").visible()},
                    {message("menu.equalizer"), "equalizer", view("eq").visible()}, {},
                    {message("menu.desktopLyric"), "desktop", view("desktop").visible()},
                    {message("menu.miniMode"), "minimode", view("mini").visible()}, {},
                    {message("menu.language"), {}, false, contextItems(13)},
                    {message("menu.about"), "about"},
                    {tr("恢复窗口布局  Ctrl+Shift+R", "Reset window layout  Ctrl+Shift+R"), "resetlayout"},
                    {tr("扩展工具", "Advanced tools"), "tools"}, {tr("退出", "Exit"), "exit"}
                };
            case 8: return {
                    {tr("搜索歌词…", "Search lyrics…"), "lyric_search"}, {tr("加载本地歌词…", "Load lyrics…"), "loadlrc"},
                    {tr("卡拉 OK 逐字高亮", "Karaoke highlighting"), "karaoke", karaoke_},
                    {tr("桌面歌词", "Desktop lyrics"), "desktop", view("desktop").visible()},
                    {tr("锁定桌面歌词", "Lock desktop lyrics"), "lock", settings_.desktopLocked}
                };
            case 9: return {
                    {tr("启用均衡器", "Enable equalizer"), "enabled", settings_.equalizer.enabled},
                    {tr("启用杜比环绕", "Enable Dolby surround"), "dolby", settings_.equalizer.dolby},
                    {tr("重置", "Reset"), "reset"}, {tr("预设类别", "Presets"), {}, false, contextItems(10)}
                };
            case 10: {
                const char *chinese[]{
                    "流行音乐", "摇滚", "金属乐", "舞曲", "电子乐", "乡村音乐", "爵士乐", "古典乐", "布鲁斯", "怀旧音乐", "歌剧", "语音", "环绕"
                };
                const char *english[]{
                    "Pop", "Rock", "Metal", "Dance", "Electronic", "Country", "Jazz", "Classical", "Blues", "Oldies",
                    "Opera", "Speech", "Surround"
                };
                std::vector<MenuItem> items;
                for (int index = 0; index < static_cast<int>(presets.size()); ++index) items.push_back({
                    tr(chinese[index], english[index]), "preset:" + std::to_string(index),
                    settings_.equalizer.bands == presets[index]
                });
                return items;
            }
            case 12: {
                std::vector<MenuItem> items;
                for (std::size_t index = 0; index < settings_.lists.size(); ++index) items.push_back({
                    settings_.lists[index].name, "list:" + std::to_string(index),
                    index == static_cast<std::size_t>(settings_.playlist)
                });
                return items;
            }
            case 13: return {
                    {"中文", "language:zh_CN", settings_.language == "zh_CN"},
                    {"English", "language:en", settings_.language == "en"},
                    {"日本語", "language:ja", settings_.language == "ja"},
                    {"한국어", "language:ko", settings_.language == "ko"},
                    {"Deutsch", "language:de", settings_.language == "de"}
                };
            case 14: return {
                    {
                        message(settings_.desktopDualLine ? "desktopLyric.oneLine" : "desktopLyric.twoLine"),
                        "desktop_lines"
                    },
                    {tr("锁定桌面歌词", "Lock desktop lyrics"), "lock", settings_.desktopLocked},
                    {message("lyricSearch.title"), "lyric_search"}, {tr("关闭", "Close"), "desktop"}
                };
            case 11: {
                std::vector<MenuItem> items;
                for (std::size_t index = 0; index < skins_.size(); ++index) items.push_back({
                    utf8(skins_[index].stem()), "skin:" + std::to_string(index), skin_ && skin_->path() == skins_[index]
                });
                items.push_back({tr("打开皮肤文件…", "Open skin file…"), "open_skin"});
                return items;
            }
            default: return {};
        }
    }

    void Application::contextAction(const std::string &action) {
        if (action.starts_with("mode:")) {
            settings_.mode = std::clamp(std::stoi(action.substr(5)), 0, 4);
            dirty_ = true;
        } else if (action.starts_with("skin:")) applySkin(skins_.at(std::stoul(action.substr(5))));
        else if (action.starts_with("preset:")) {
            settings_.equalizer.bands = presets.at(std::stoul(action.substr(7)));
            settings_.equalizer.enabled = true;
            engine_.equalizer().configure(settings_.equalizer);
            dirty_ = true;
        } else if (action == "open_skin") showDialog(FileDialogKind::OpenSkin);
        else if (action == "preferences") openPanel("options");
        else if (action == "lyric_search") openPanel("lyrics");
        else if (action == "add_search") showDialog(FileDialogKind::AddFolder);
        else if (action == "library") openContext(view("playlist"), 2, 10, 30);
        else if (action.starts_with("language:")) setLanguage(action.substr(9));
        else if (action.starts_with("list:")) {
            const auto index = std::stoul(action.substr(5));
            if (index < settings_.lists.size()) {
                settings_.playlist = static_cast<int>(index);
                selectedSongs_.clear();
                selected_ = selectionAnchor_ = settings_.song = -1;
                view("playlist").scroll = 0;
                dirty_ = true;
            }
        } else if (action == "find" || action == "find_next") findSong(action == "find_next");
        else if (action == "locate") {
            if (nowList_ >= 0 && nowList_ < static_cast<int>(settings_.lists.size()) && nowIndex_ >= 0) {
                settings_.playlist = nowList_;
                filter_.clear();
                selectSong(nowIndex_);
                view("playlist").scroll = nowIndex_;
                view("playlist").show(true);
            } else openPanel("info", message("dialog.locateTitle"), message("dialog.noPlaying"));
        } else if (action == "pause_after") {
            settings_.pauseAfterPlay = !settings_.pauseAfterPlay;
            dirty_ = true;
        } else if (action == "add_url") {
            openPanel("input", message("dialog.addUrlPrompt"), "", [this](const auto &url) {
                if (url.empty()) return;
                if (!url.starts_with("https://") && !url.starts_with("http://")) {
                    status_ = tr("仅支持 HTTP/HTTPS 音频地址", "Only HTTP/HTTPS audio URLs are accepted");
                    return;
                }
                playlist().songs.push_back({fromUtf8(url), message("dialog.networkAudio"), "", "", 0});
                dirty_ = true;
            });
        } else if (action == "playlist_options") openPanel("input", message("dialog.fileAssocPrefix"),
                                                           settings_.playlistPrefix, [this](const auto &value) {
                                                               settings_.playlistPrefix = value;
                                                               dirty_ = true;
                                                           });
        else if (action == "filename_format") openPanel("format", message("dialog.filenameFormatTitle"));
        else if (action == "batch_info") {
            std::ostringstream information;
            for (const auto index: selectedSongs_)
                if (index >= 0 && index < static_cast<int>(playlist().songs.size())) {
                    const auto &song = playlist().songs[index];
                    information << index + 1 << ". " << song.artist << " - " << song.title << '\n' << utf8(song.path) <<
                            '\n';
                }
            openPanel("info", message("menu.batchFileInfo"),
                      selectedSongs_.empty() ? message("dialog.selectSongFirst") : information.str());
        } else if (action == "about") openPanel("info", message("menu.about"),
                                                message("about.text") + "\n\nC++20 / FFmpeg / SDL3\n" + tr(
                                                    "基于本仓库 Java 版复刻", "Recreated from this repository's Java player"));
        else if (action == "save_all") showDialog(FileDialogKind::ExportAllPlaylists);
        else if (action == "delete_list") {
            if (confirmAction(message("dialog.deleteListConfirm"))) removeList();
        } else if (action == "rename_list") {
            const int index = settings_.playlist;
            openPanel("input", message("dialog.renamePrompt"), playlist().name, [this, index](const auto &name) {
                if (!name.empty() && index < static_cast<int>(settings_.lists.size())) {
                    settings_.lists[index].name = name;
                    dirty_ = true;
                }
            });
        } else if (action == "sort_lists") {
            if (importer_.busy()) {
                status_ = tr("请等待导入结束", "Wait for import completion");
                return;
            }
            std::vector<int> indices;
            for (int index = 0; index < static_cast<int>(settings_.lists.size()); ++index) indices.push_back(index);
            std::stable_sort(indices.begin(), indices.end(), [this](const int first, const int second) {
                return lower(settings_.lists[first].name) < lower(settings_.lists[second].name);
            });
            auto source = std::move(settings_.lists);
            const int current = settings_.playlist, playing = nowList_;
            settings_.lists.clear();
            settings_.lists.reserve(source.size());
            for (int index = 0; index < static_cast<int>(indices.size()); ++index) {
                if (indices[index] == current) settings_.playlist = index;
                if (indices[index] == playing) nowList_ = index;
                settings_.lists.push_back(std::move(source[indices[index]]));
            }
            dirty_ = true;
        } else if (action == "filter") {
            auto &window = view("playlist");
            window.show(true);
            contextWindow_ = "playlist";
            contextKind_ = 4;
            contextBounds_ = {12, 28, 240, 46};
            inputField_ = 2;
            SDL_StartTextInput(window.window);
        } else if (action == "select_all" || action == "select_invert") {
            for (int index = 0; index < static_cast<int>(playlist().songs.size()); ++index) {
                if (action == "select_all" || !selectedSongs_.erase(index)) selectedSongs_.insert(index);
            }
            selected_ = selectedSongs_.empty() ? -1 : *selectedSongs_.begin();
            selectionAnchor_ = selected_;
        } else if (action == "delete_other" || action == "delete_duplicates" || action == "clear_list") {
            const auto before = selectedSongs_;
            if (action == "delete_other" && before.empty()) return;
            if (action == "clear_list" && !confirmAction(message("dialog.clearListConfirm"))) return;
            selectedSongs_.clear();
            std::set<std::filesystem::path> paths;
            for (int index = 0; index < static_cast<int>(playlist().songs.size()); ++index) {
                if (action == "clear_list" || (action == "delete_other" && !before.contains(index) && (
                                                   before.empty() ? selected_ != index : true)) ||
                    (action == "delete_duplicates" && !paths.insert(playlist().songs[index].path.lexically_normal()).
                     second))
                    selectedSongs_.insert(index);
            }
            if (!selectedSongs_.empty()) removeSong();
        } else if (action == "new_list") {
            openPanel("input", message("dialog.newListPrompt"), "", [this](const auto &name) {
                if (name.empty()) return;
                settings_.lists.push_back({name, {}});
                settings_.playlist = static_cast<int>(settings_.lists.size()) - 1;
                settings_.song = selected_ = selectionAnchor_ = -1;
                selectedSongs_.clear();
                view("playlist").scroll = 0;
                dirty_ = true;
            });
        } else command(action);
    }

    void Application::openContext(UiWindow &window, const int kind, const float left, const float top) {
        dragging_.reset();
        SDL_CaptureMouse(false);
        contextWindow_.clear();
        inputField_ = 0;
        SDL_StopTextInput(window.window);
        try { popupMenu_.open(window, fonts_, contextItems(kind), left, top); } catch (const std::exception &error) {
            status_ = error.what();
        }
    }
}
