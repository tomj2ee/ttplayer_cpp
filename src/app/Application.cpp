#include "app/Application.hpp"
#include "app/EmbeddedAssets.hpp"

#include <SDL3/SDL_properties.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ttplayer::app {
    namespace {
SDL_HitTestResult SDLCALL hitTest(SDL_Window *, const SDL_Point *point, void *userdata) {
#ifdef __APPLE__
            static_cast<void>(point); static_cast<void>(userdata);
            return SDL_HITTEST_NORMAL;
#else
            return windowHitTest(*static_cast<UiWindow *>(userdata), *point);
#endif
        }

        std::string skinWindowName(const std::string_view name) {
            return std::string(name == "eq" ? "equalizer" : name) + "_window";
        }
    }

    Application::Application(const int argc, char **argv) {
        for (int index = 1; index < argc; ++index) arguments_.emplace_back(argv[index]);
        initialize();
    }

    Application::~Application() {
#ifdef __APPLE__
        macApplication_.reset();
#endif
        popupMenu_.close();
        tray_.reset();
        if (windowWatchInstalled_) SDL_RemoveEventWatch(windowEventWatch, this);
#ifdef _WIN32
        if (nativeWindowManagement_) SDL_SetWindowsMessageHook(nullptr, nullptr);
#endif
        importer_.cancel();
        fileDialogs_.close();

        engine_.stop();
        importer_.wait();
        lyricRequests_.wait();
        if (initialized_ && !smoke_) {
            try { save(); } catch (const std::exception &error) { SDL_Log("Save failed: %s", error.what()); }
        }
    }

    UiWindow &Application::view(const std::string_view name) { return *windows_.at(std::string(name)); }
    Playlist &Application::playlist() { return settings_.lists.at(static_cast<std::size_t>(settings_.playlist)); }

    void Application::initialize() {
        SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#ifdef __APPLE__
        SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
#endif
        const char *executable = SDL_GetBasePath();
        base_ = executable ? fromUtf8(executable) : std::filesystem::current_path();
        assets_ = base_ / "assets";
        legacy_ = base_ / "legacy-config";
        for (auto directory = base_; !directory.empty();) {
            if (std::filesystem::is_directory(directory / "src/main/resources/skin")) {
                if (!std::filesystem::is_directory(assets_ / "skin")) assets_ = directory / "src/main/resources";
                legacy_ = directory / "config";
                break;
            }
            const auto parent = directory.parent_path();
            if (parent == directory) break;
            directory = parent;
        }
        char *preference = SDL_GetPrefPath("TTPlayer", "CppPlayer");
        if (!preference) throw std::runtime_error(SDL_GetError());
        config_ = fromUtf8(preference);
        SDL_free(preference);
        for (std::size_t index = 0; index < arguments_.size(); ++index) {
            if (arguments_[index] == "--portable") {
#ifdef __APPLE__
                const auto resourceBase = base_.filename().empty() ? base_.parent_path() : base_;
                config_ = resourceBase.filename() == "Resources" && resourceBase.parent_path().filename() == "Contents"
                              ? resourceBase.parent_path().parent_path().parent_path() / "TTPlayerData"
                              : base_ / "config";
#else
                config_ = base_ / "config";
#endif
            } else if (arguments_[index] == "--config-dir" && index + 1 < arguments_.size())
                config_ = fromUtf8(arguments_[++index]);
            else if (arguments_[index] == "--smoke-ui" && index + 1 < arguments_.size()) {
                smoke_ = true;
                smokeOutput_ = fromUtf8(arguments_[++index]);
            } else if (arguments_[index] == "--quit-after" && index + 1 < arguments_.size())
                quitAt_ = SDL_GetTicks() + std::stoull(arguments_[++index]);
        }
        if (const auto embeddedAssets = EmbeddedAssets().prepare(config_ / "bundled-resources"))
            assets_ = *embeddedAssets;
        if (!smoke_ && std::filesystem::exists(config_ / "player.json")) {
            try { settings_ = loadSettings(config_ / "player.json"); } catch (const std::exception &error) {
                status_ = "Settings could not be read; original file preserved: " + std::string(error.what());
                const auto backup = config_ / ("player.invalid-" + std::to_string(SDL_GetTicks()) + ".json");
                std::filesystem::copy_file(config_ / "player.json", backup, std::filesystem::copy_options::none);
            }
        } else if (!smoke_ && std::filesystem::exists(legacy_ / "playlists.xml")) {
            try {
                auto lists = importPlaylists(legacy_ / "playlists.xml");
                if (!lists.empty()) settings_.lists = std::move(lists);
                const auto root = parseXml(decodeText(readFile(legacy_ / "playlists.xml")));
                int listIndex = 0;
                for (const auto &node: root.children) {
                    if (node.name != "playlist") continue;
                    if (node.value("current") == "true") settings_.playlist = listIndex;
                    ++listIndex;
                }
                settings_.playlist = std::clamp(settings_.playlist, 0, static_cast<int>(settings_.lists.size()) - 1);
                std::istringstream properties(readFile(legacy_ / "equalizer.properties"));
                std::string line;
                while (std::getline(properties, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    const auto equal = line.find('=');
                    if (equal == std::string::npos) continue;
                    const auto key = line.substr(0, equal), value = line.substr(equal + 1);
                    try {
                        if (key == "enabled") settings_.equalizer.enabled = value == "true";
                        else if (key == "dolby") settings_.equalizer.dolby = value == "true";
                        else if (key == "preamp") settings_.equalizer.preamp = std::stof(value);
                        else if (key == "balance") settings_.equalizer.balance = std::clamp(
                                                       (std::stof(value) - 50) / 50, -1.0F, 1.0F);
                        else if (key == "surround" && value != "0") settings_.equalizer.surround = 0;
                        else if (key.starts_with("gain.")) settings_.equalizer.bands.at(
                                                               static_cast<std::size_t>(std::stoi(key.substr(5)))) =
                                                           std::stof(value);
                    } catch (const std::exception &) {
                    }
                }
                status_ = tr("已导入 Java 播放列表，原配置未修改", "Imported Java playlists; original files are unchanged");
            } catch (const std::exception &error) { status_ = error.what(); }
        }
        const bool restoreAppearance = std::find(arguments_.begin(), arguments_.end(), "--restore-java-ui") !=
                                       arguments_.end();
        if (settings_.appearanceVersion < 2 || restoreAppearance) {
            if (!smoke_ && std::filesystem::exists(config_ / "player.json") && !std::filesystem::exists(
                    config_ / "player.before-classic-ui.json"))
                std::filesystem::copy_file(config_ / "player.json", config_ / "player.before-classic-ui.json");
            if (restoreAppearance || settings_.skin.empty()) {
                settings_.windows.clear();
                settings_.skin = assets_ / "skin/default.skn";
                auto javaConfig = legacy_ / "windows.properties";
                const char *home = SDL_getenv("USERPROFILE");
                if (!home) home = SDL_getenv("HOME");
                if (home && std::filesystem::is_regular_file(fromUtf8(home) / ".ttplayer/windows.properties"))
                    javaConfig = fromUtf8(home) / ".ttplayer/windows.properties";
                if (!smoke_ && std::filesystem::is_regular_file(javaConfig)) {
                    try { importJavaAppearance(javaConfig, assets_ / "skin", settings_); } catch (const std::exception &
                        error) { status_ = error.what(); }
                }
            } else {
                for (auto &[name, geometry]: settings_.windows) {
                    if (name == "settings" || name == "desktop") continue;
                    geometry.width = 0;
                    geometry.height = 0;
                }
            }
            settings_.appearanceVersion = 2;
            dirty_ = true;
        }
        localization_.load(assets_);
        lyricsRepository_ = LyricsRepository(config_ / "lyrics");
        karaoke_ = settings_.karaoke;
        miniVisibility_ = settings_.miniVisibility;
        if (!settings_.rememberPosition)
            for (auto &[name, geometry]: settings_.windows) {
                static_cast<void>(name);
                geometry.x = geometry.y = -1;
                geometry.width = geometry.height = 0;
            }
        fonts_.locate(assets_);
        if (std::filesystem::is_directory(assets_ / "skin")) {
            for (const auto &entry: std::filesystem::directory_iterator(assets_ / "skin")) {
                if (entry.is_regular_file() && lower(utf8(entry.path().extension())) == ".skn") skins_.push_back(
                    entry.path());
            }
            std::sort(skins_.begin(), skins_.end());
        }
        engine_.setVolume(settings_.volume);
        engine_.equalizer().configure(settings_.equalizer);
        settings_.equalizer = engine_.equalizer().settings();
        createWindows();
        if (!settings_.skin.empty()) {
            auto path = settings_.skin;
            if (!std::filesystem::exists(path) && std::filesystem::exists(assets_ / "skin" / path.filename()))
                path = assets_ / "skin" / path.filename();
            try { applySkin(path); } catch (const std::exception &error) {
                status_ = error.what();
                settings_.skin.clear();
            }
        }
        applyDesktopLock();
        setLanguage(settings_.language);
        recoverWindows();
        if (!smoke_ && settings_.miniOnStart && !view("mini").visible()) command("minimode");
        if (!smoke_) createTray();
        initialized_ = true;
        std::vector<std::filesystem::path> files;
        for (std::size_t index = 0; index < arguments_.size(); ++index) {
            if (arguments_[index] == "--config-dir" || arguments_[index] == "--smoke-ui" || arguments_[index] ==
                "--quit-after") {
                ++index;
                continue;
            }
            if (!arguments_[index].starts_with("--")) files.push_back(fromUtf8(arguments_[index]));
        }
        if (!files.empty()) processDroppedFiles(std::move(files));
        if (smoke_) {
            settings_.lists = {
                {"验证 / Verification", {{fromUtf8("示例音乐.flac"), "夜空中最亮的星 / The brightest star", "中文歌手", "示例专辑", 245000}}}
            };
            lyrics_.parse("[00:00.00]C++ · FFmpeg · SDL3\n[00:02.00]中文歌词与逐字高亮\n[00:06.00]听见音乐，看见星空\n");
            nowSong_ = playlist().songs.front();
            std::filesystem::create_directories(smokeOutput_);
        }
        initializeWindowManagement();
        presentInitialWindows();
#ifdef __APPLE__
        if (std::string_view(SDL_GetCurrentVideoDriver()) == "cocoa") {
            macReopenEvent_ = SDL_RegisterEvents(1);
            if (!macReopenEvent_) throw std::runtime_error(SDL_GetError());
            macApplication_ = std::make_unique<MacApplication>(macReopenEvent_,
                [this](SDL_Window *window, MacWindowEvent event) { macWindowEvent(window, event); });
            nativeWindowManagement_ = true;
        }
#endif
    }

    void Application::createWindows() {
        struct Spec {
            const char *name;
            const char *title;
            int width, height;
        };
        const Spec specs[]{
            {"player", "TTPlayer · C++", 840, 320}, {"playlist", "播放列表 / Playlist", 840, 470},
            {"eq", "均衡器 / Equalizer", 700, 320}, {"lyric", "歌词 / Lyrics", 520, 370},
            {"mini", "TTPlayer Mini", 620, 96},
            {"desktop", "桌面歌词 / Desktop lyrics", 600, settings_.desktopDualLine ? 120 : 80},
            {"settings", "设置与工具 / Settings", 860, 610}, {"dialog", "TTPlayer", 580, 400},
            {"progress", "TTPlayer", 420, 150}
        };
        for (const auto &spec: specs) {
            const bool desktop = std::string_view(spec.name) == "desktop";
            const auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_TRANSPARENT |
                               (std::string_view(spec.name) != "player" ? SDL_WINDOW_UTILITY : 0) |
                               (desktop ? SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP : 0);
            auto window = std::make_unique<UiWindow>(spec.name, spec.title, spec.width, spec.height, flags, fonts_);
            window->deferPresentation();
            if (!desktop) SDL_SetWindowMinimumSize(window->window, spec.width, spec.height);
            SDL_SetWindowAlwaysOnTop(window->window,
                                     desktop || std::string_view(spec.name) == "mini" || std::string_view(spec.name) ==
                                     "progress" || settings_.topmost);
            SDL_SetWindowPosition(window->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            SDL_Rect usable{};
            if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window->window), &usable)) {
                const int left = usable.x + std::max(0, (usable.w - 1480) / 2);
                const int top = usable.y + 40;
                if (std::string_view(spec.name) == "player") SDL_SetWindowPosition(window->window, left, top);
                else if (std::string_view(spec.name) == "playlist") SDL_SetWindowPosition(
                    window->window, left, std::min(top + 360, usable.y + usable.h - spec.height));
                else if (std::string_view(spec.name) == "eq" || std::string_view(spec.name) == "lyric")
                    SDL_SetWindowPosition(window->window, std::min(left + 860, usable.x + usable.w - spec.width),
                                          std::string_view(spec.name) == "eq"
                                              ? top
                                              : std::min(top + 360, usable.y + usable.h - spec.height));
                else if (desktop) SDL_SetWindowPosition(window->window, usable.x + (usable.w - spec.width) / 2,
                                                        usable.y + usable.h - spec.height - 30);
            }
            const auto geometry = settings_.windows.find(spec.name);
            if (geometry != settings_.windows.end()) {
                if (geometry->second.width >= 200 && geometry->second.height >= 60) SDL_SetWindowSize(
                    window->window, geometry->second.width, geometry->second.height);
                const SDL_Rect wanted{
                    geometry->second.x, geometry->second.y, std::max(200, geometry->second.width),
                    std::max(60, geometry->second.height)
                };
                const auto display = SDL_GetDisplayForRect(&wanted);
                if ((geometry->second.x != -1 || geometry->second.y != -1) && display &&
                    SDL_GetDisplayUsableBounds(display, &usable) && wanted.x + wanted.w > usable.x + 40 && wanted.y +
                    wanted.h > usable.y + 40 && wanted.x < usable.x + usable.w - 40 && wanted.y < usable.y + usable.h -
                    40)
                    SDL_SetWindowPosition(window->window, wanted.x, wanted.y);
            }
            const bool showWindow = geometry == settings_.windows.end()
                                        ? std::string_view(spec.name) == "player" || std::string_view(spec.name) ==
                                          "playlist" || std::string_view(spec.name) == "lyric" || std::string_view(
                                              spec.name) == "eq"
                                        : geometry->second.visible;
            window->show(showWindow && !smoke_);
            if (desktop) SDL_SetWindowHitTest(window->window, hitTest, window.get());
            windows_.emplace(spec.name, std::move(window));
        }
        if (!smoke_ && !view("player").visible() && !view("mini").visible()) view("player").show(true);
    }

    void Application::presentInitialWindows() {
        for (const auto &[name, window]: windows_) {
            static_cast<void>(name);
            if (window->visible()) render(*window);
        }
        const auto hintValue = [](const char *name) -> std::optional<std::string> {
            const auto *value = SDL_GetHint(name);
            return value ? std::optional<std::string>(value) : std::nullopt;
        };
        const std::pair<const char *, std::optional<std::string> > activationHints[]{
            {SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, hintValue(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN)},
            {SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, hintValue(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED)}
        };
        for (const auto &[name, value]: activationHints) {
            static_cast<void>(value);
            SDL_SetHintWithPriority(name, "0", SDL_HINT_OVERRIDE);
        }
        auto &primary = view(view("mini").visible() ? "mini" : "player");
        primary.commitPresentation();
        for (const auto &[name, window]: windows_) {
            if (name != primary.name) window->commitPresentation();
        }
        for (const auto &[name, value]: activationHints) {
            if (value) SDL_SetHintWithPriority(name, value->c_str(), SDL_HINT_OVERRIDE);
            else SDL_ResetHint(name);
        }
        if (primary.visible()) {
            SDL_RaiseWindow(primary.window);
            raiseVisibleWindows(primary);
        }
    }

    void Application::createTray() {
        auto *original = skin_ ? skin_->windowIcon() : nullptr;
        SDL_Surface *icon = original
                                ? SDL_DuplicateSurface(original)
                                : SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
        if (!icon) return;
        if (!original) {
            SDL_FillSurfaceRect(icon, nullptr, SDL_MapSurfaceRGBA(icon, 12, 24, 38, 255));
            for (int index = 0; index < 4; ++index) {
                const SDL_Rect bar{5 + index * 6, 6 + (index % 3) * 4, 4, 22 - (index % 3) * 4};
                SDL_FillSurfaceRect(icon, &bar, SDL_MapSurfaceRGBA(icon, 57, 211, 174, 255));
            }
        }
        for (auto &[name, window]: windows_) {
            static_cast<void>(name);
            SDL_SetWindowIcon(window->window, icon);
        }
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> trayIcon(icon, SDL_DestroySurface);
        const std::pair<const char *, const char *> items[]{
            {"show", "显示播放器 / Show"}, {"play", "播放 / 暂停 · Play / Pause"}, {"prev", "上一首 / Previous"},
            {"next", "下一首 / Next"}, {"playlist", "播放列表 / Playlist"}, {"lyric", "歌词 / Lyrics"},
            {"desktop", "桌面歌词 / Desktop lyrics"},
            {"lock", "锁定 / 解锁桌面歌词 · Lock / Unlock"}, {"resetlayout", "恢复窗口布局 / Reset layout"},
            {"settings", "设置 / Settings"}, {"exit", "退出 / Quit"}
        };
        SystemTray::Entries entries;
        for (const auto &[name, label]: items) entries.emplace_back(name, label);
        tray_ = std::make_unique<SystemTray>(icon, entries, [this](const std::string &action) {
            command(action);
            if (action == "show" && view("mini").visible()) command("return");
            if (action == "show") SDL_RaiseWindow(view(view("dialog").visible() ? "dialog" : "player").window);
        });
    }

    bool Application::hasTray() const { return tray_ && tray_->available(); }

    void Application::applySkin(const std::filesystem::path &path) {
        popupMenu_.close();
        std::unique_ptr<Skin> replacement;
        if (!path.empty()) {
            replacement = std::make_unique<Skin>();
            replacement->load(path, assets_ / "ico/TTPlayer.ico");
        }
        endWindowDrag();
        for (auto &[name, window]: windows_) {
            window->clearCache();
            if (replacement && replacement->windowIcon()) SDL_SetWindowIcon(
                window->window, replacement->windowIcon());
            if (name == "desktop" || name == "settings" || name == "dialog" || name == "progress") continue;
            const auto *definition = replacement ? replacement->window(skinWindowName(name)) : nullptr;
            const bool resizable = !definition || (definition->resize.w > 0 && definition->resize.h > 0);
            SDL_SetWindowResizable(window->window, resizable);
            SDL_SetWindowBordered(window->window, definition == nullptr);
            SDL_SetWindowHitTest(window->window, definition ? hitTest : nullptr, definition ? window.get() : nullptr);
            if (definition) {
                SDL_SetWindowMinimumSize(window->window, definition->width, definition->height);
                SDL_SetWindowSize(window->window, definition->width, definition->height);
            } else {
                const std::map<std::string, std::pair<int, int> > sizes{
                    {"player", {840, 320}}, {"playlist", {840, 470}}, {"eq", {700, 320}}, {"lyric", {520, 370}},
                    {"mini", {620, 96}}
                };
                const auto size = sizes.at(name);
                SDL_SetWindowMinimumSize(window->window, size.first, size.second);
                SDL_SetWindowSize(window->window, size.first, size.second);
            }
            if (!initialized_ && resizable) {
                const auto geometry = settings_.windows.find(name);
                if (geometry != settings_.windows.end() && geometry->second.width > 0 && geometry->second.height > 0)
                    SDL_SetWindowSize(window->window, geometry->second.width, geometry->second.height);
            }
            if (definition && (initialized_ || !settings_.windows.contains(name))) {
                int anchorX = 0, anchorY = 0;
                SDL_GetWindowPosition(view("player").window, &anchorX, &anchorY);
                const auto *player = replacement->window("player_window");
                const int offsetX = static_cast<int>(definition->position.x - player->position.x);
                const int offsetY = static_cast<int>(definition->position.y - player->position.y);
                if (name != "player") SDL_SetWindowPosition(window->window, anchorX + offsetX, anchorY + offsetY);
            }
        }
        skin_ = std::move(replacement);
        contextWindow_.clear();
        dragging_.reset();
        settings_.skin = path;
        if (!path.empty() && std::find(skins_.begin(), skins_.end(), path) == skins_.end()) skins_.push_back(path);
        settings_.appearanceVersion = 2;
        if (initialized_) recoverWindows();
        dirty_ = true;
    }

    void Application::applyDesktopLock() {
#ifdef _WIN32
        auto *window = view("desktop").window;
        const auto properties = SDL_GetWindowProperties(window);
        const auto handle = static_cast<HWND>(SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                                                     nullptr));
        if (handle) {
            auto style = GetWindowLongPtrW(handle, GWL_EXSTYLE);
            style = settings_.desktopLocked
                        ? style | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE
                        : style & ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
            SetWindowLongPtrW(handle, GWL_EXSTYLE, style);
        }
#elif defined(__APPLE__)
        applyMacDesktopLock();
#endif
    }

    void Application::toggle(const std::string_view name) {
        const auto suspended = suspendedVisibility_.find(std::string(name));
        if (suspended != suspendedVisibility_.end()) suspended->second = !suspended->second;
        else {
            auto &window = view(name);
            window.show(!window.visible());
        }
        dirty_ = true;
    }

    void Application::command(std::string name) {
        popupMenu_.close();
        try {
            if (name == "eq") name = "equalizer";
            else if (name == "mini" || name == "mini_mode") name = "minimode";
            else if (name == "normal" || name == "normalmode" || name == "defaultmode" || name == "default")
                name = "return";
            else if (name == "option" || name == "options") name = "settings";
            else if (name == "desklrc_bar") name = "desktop";
            if (name == "play" || name == "pause") playPause();
            else if (name == "stop") {
                autoAdvance_ = false;
                engine_.stop();
            } else if (name == "prev") advance(-1);
            else if (name == "next") advance(1);
            else if (name.starts_with("mode")) settings_.mode = (settings_.mode + 1) % 5;
            else if (name == "open" || name == "browser") showDialog(FileDialogKind::OpenAudio);
            else if (name == "folder") showDialog(FileDialogKind::AddFolder);
            else if (name == "export") showDialog(FileDialogKind::ExportPlaylist);
            else if (name == "import") showDialog(FileDialogKind::ImportPlaylists);
            else if (name == "loadlrc") showDialog(FileDialogKind::OpenLyrics);
            else if (name == "skin") openPanel("skin");
            else if (name == "about") contextAction("about");
            else if (name == "playlist" || name == "equalizer" || name == "lyric" || name == "desktop" || name ==
                     "desklrc") toggle(name == "equalizer" ? "eq" : name == "desklrc" ? "desktop" : name);
            else if (name == "settings" || name == "set") openPanel("options");
            else if (name == "tools") {
                if (!suspendedVisibility_.empty()) restoreSuspendedWindows();
                view("settings").show(true);
            } else if (name == "resetlayout") resetWindowLayout();
            else if (name == "show" && !suspendedVisibility_.empty()) {
                restoreSuspendedWindows();
            } else if (name == "show" || name == "return" || name == "minimode") {
                if (!suspendedVisibility_.empty()) restoreSuspendedWindows();
                endWindowDrag();
                const bool dialogVisible = view("dialog").visible();
                if (dialogVisible) view("dialog").show(false);
                const bool enterMini = name == "minimode" && !view("mini").visible();
                if (enterMini) {
                    miniVisibility_.clear();
                    for (const auto *key: {"player", "playlist", "lyric", "eq"}) {
                        miniVisibility_[key] = view(key).visible();
                        view(key).show(false);
                    }
                    view("mini").show(true);
                } else {
                    view("mini").show(false);
                    for (const auto &[key, visible]: miniVisibility_) view(key).show(visible);
                    miniVisibility_.clear();
                    view("player").show(true);
                }
                updateDialogOwner(dialogVisible);
                raiseVisibleWindows(view(enterMini ? "mini" : "player"));
            } else if (name == "minimize") {
                endWindowDrag();
                if (suspendedVisibility_.empty())
                    for (auto &[key, window]: windows_) {
                        if (key == "desktop") continue;
                        suspendedVisibility_[key] = window->visible();
                        if (hasTray() || (key != "player" && key != "mini")) window->show(false);
                    }
                if (!hasTray()) SDL_MinimizeWindow(view(view("mini").visible() ? "mini" : "player").window);
            } else if (name == "exit") running_ = false;
            else if (name == "mute") {
                if (engine_.volume() > 0) {
                    unmutedVolume_ = engine_.volume();
                    engine_.setVolume(0);
                } else engine_.setVolume(unmutedVolume_);
            } else if (name == "visual") settings_.visual = (settings_.visual + 1) % 7;
            else if (name == "ontop") {
                settings_.topmost = !settings_.topmost;
                for (auto &[key, window]: windows_) if (key != "desktop" && key != "mini" && key != "progress")
                    SDL_SetWindowAlwaysOnTop(window->window, settings_.topmost);
            } else if (name == "lock") {
                settings_.desktopLocked = !settings_.desktopLocked;
                applyDesktopLock();
            } else if (name == "enabled") {
                settings_.equalizer.enabled = !settings_.equalizer.enabled;
                engine_.equalizer().configure(settings_.equalizer);
            } else if (name == "dolby") {
                settings_.equalizer.dolby = !settings_.equalizer.dolby;
                engine_.equalizer().configure(settings_.equalizer);
            } else if (name == "reset") {
                settings_.equalizer = {};
                settings_.equalizer.enabled = true;
                engine_.equalizer().configure(settings_.equalizer);
            } else if (name == "profile") {
                static const std::array<std::array < float, 10>, 4 > presets{
                                                                     {
                                                                         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                                                         {5, 4, 2, 0, -2, 0, 2, 4, 5, 5},
                                                                         {-2, -1, 1, 3, 4, 3, 1, 0, -1, -2},
                                                                         {5, 4, 1, 0, -2, -2, 0, 1, 3, 4}
                                                                     }
                                                                 };
                const auto current = std::find(presets.begin(), presets.end(), settings_.equalizer.bands);
                settings_.equalizer.bands = presets[current == presets.end()
                                                        ? 0
                                                        : (static_cast<std::size_t>(current - presets.begin()) + 1) %
                                                          presets.size()];
                settings_.equalizer.enabled = true;
                engine_.equalizer().configure(settings_.equalizer);
            } else if (name == "delete") removeSong();
            else if (name.starts_with("sort_")) reorderSongs(name.substr(5));
            else if (name == "properties") {
                if (selected_ >= 0 && selected_ < static_cast<int>(playlist().songs.size())) {
                    const auto &song = playlist().songs[static_cast<std::size_t>(selected_)];
                    const auto information = song.title + "\n" + song.artist + "\n" + song.album + "\n" +
                                             timeText(song.duration) + "\n" + utf8(song.path);
                    openPanel("info", tr("歌曲信息", "Track information"), information);
                }
            } else if (name == "search") startSearch();
            else if (name == "karaoke") settings_.karaoke = karaoke_ = !karaoke_;
            else if (name == "desktop_lines") {
                settings_.desktopDualLine = !settings_.desktopDualLine;
                SDL_SetWindowSize(view("desktop").window, view("desktop").width, settings_.desktopDualLine ? 120 : 80);
            }
            dirty_ = true;
        } catch (const std::exception &error) {
            status_ = error.what();
            view("settings").show(true);
        }
    }

    void Application::playSong(const int list, const int index) {
        if (list < 0 || list >= static_cast<int>(settings_.lists.size())) return;
        const auto &songs = settings_.lists[static_cast<std::size_t>(list)].songs;
        if (index < 0 || index >= static_cast<int>(songs.size())) return;
        nowSong_ = songs[static_cast<std::size_t>(index)];
        nowList_ = list;
        nowIndex_ = index;
        settings_.song = index;
        autoAdvance_ = engine_.play(nowSong_.path);
        if (!autoAdvance_) status_ = engine_.errorMessage();
        loadLyrics();
        startAutomaticLyrics();
        dirty_ = true;
    }

    void Application::playPause() {
        const auto state = engine_.state();
        if (state == media::PlaybackState::stopped || state == media::PlaybackState::error) {
            if (!nowSong_.path.empty()) { autoAdvance_ = engine_.play(nowSong_.path); } else playSong(
                settings_.playlist, selected_ >= 0 ? selected_ : settings_.song >= 0 ? settings_.song : 0);
        } else engine_.togglePause();
    }

    void Application::advance(const int direction, const bool automatic) {
        const int list = nowSong_.path.empty()
                             ? settings_.playlist
                             : std::clamp(nowList_, 0, static_cast<int>(settings_.lists.size()) - 1);
        const auto &songs = settings_.lists[static_cast<std::size_t>(list)].songs;
        if (songs.empty()) return;
        const int count = static_cast<int>(songs.size());
        const auto current = std::find_if(songs.begin(), songs.end(), [&](const auto &song) {
            return song.path == nowSong_.path;
        });
        const int index = current == songs.end()
                              ? std::clamp(nowIndex_, -1, count - 1)
                              : static_cast<int>(current - songs.begin());
        int next = index + direction;
        if (automatic && settings_.mode == 0) return;
        if (automatic && settings_.mode == 1) next = index;
        else if (settings_.mode == 4 && count > 1) {
            std::uniform_int_distribution<int> choice(1, count - 1);
            next = (std::max(0, index) + choice(random_)) % count;
        }
        if (automatic && settings_.mode == 2 && next >= count) return;
        if (automatic && settings_.pauseAfterPlay) return;
        next = (next % count + count) % count;
        playSong(list, next);
    }

    void Application::removeSong() {
        auto &songs = playlist().songs;
        auto selection = selectedSongs_;
        if (selection.empty() && selected_ >= 0) selection.insert(selected_);
        for (auto index = selection.rbegin(); index != selection.rend(); ++index) {
            if (*index < 0 || *index >= static_cast<int>(songs.size())) continue;
            if (nowList_ == settings_.playlist && *index <= nowIndex_) --nowIndex_;
            songs.erase(songs.begin() + *index);
        }
        selected_ = std::min(selected_, static_cast<int>(songs.size()) - 1);
        selectedSongs_.clear();
        if (selected_ >= 0) selectedSongs_.insert(selected_);
        selectionAnchor_ = selected_;
        settings_.song = selected_;
        dirty_ = true;
    }

    void Application::save() {
        settings_.volume = engine_.volume();
        settings_.equalizer = engine_.equalizer().settings();
        settings_.karaoke = karaoke_;
        settings_.miniVisibility = miniVisibility_;
        for (auto &[name, window]: windows_) {
            if (name == "dialog" || name == "progress") continue;
            auto &geometry = settings_.windows[name];
            const auto bounds = normalWindowBounds(*window);
            geometry.x = bounds.x;
            geometry.y = bounds.y;
            geometry.width = bounds.w;
            geometry.height = bounds.h;
            geometry.visible = suspendedVisibility_.contains(name) ? suspendedVisibility_.at(name) : window->visible();
        }
        saveSettings(config_ / "player.json", settings_);
        dirty_ = false;
        lastSave_ = SDL_GetTicks();
    }

    int Application::run() {
        Uint64 previousFrame = 0;
        while (running_) {
            SDL_Event pending{};
            while (SDL_PollEvent(&pending)) event(pending);
            try { popupMenu_.update(); } catch (const std::exception &error) {
                popupMenu_.close();
                status_ = error.what();
            }
            if (!nativeWindowManagement_ && !windowDrag_.root().empty() && !(
                    SDL_GetGlobalMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) endWindowDrag();
            pollJobs();
            const auto state = engine_.state();
            if (autoAdvance_ &&state == media::PlaybackState::stopped)
            {
                autoAdvance_ = false;
                advance(1, true);
            }
            if (autoAdvance_ &&state == media::PlaybackState::error)
            {
                autoAdvance_ = false;
                status_ = engine_.errorMessage();
            }
            const auto now = SDL_GetTicks();
            if (now - previousFrame >= 30) {
                spectrum_ = engine_.spectrumBars(48);
                waveform_ = engine_.waveform(256);
                for (auto &[name, window]: windows_) {
                    static_cast<void>(name);
                    if (window->visible()) render(*window);
                }
                previousFrame = now;
            }
            if (now - lastTitle_ > 500) {
                refreshWindowWorkAreas();
                const auto title = "TTPlayer · " + (nowSong_.title.empty() ? tr("就绪", "Ready") : nowSong_.title) + " · "
                                   + timeText(engine_.positionMs()) + " / " + timeText(engine_.durationMs());
                SDL_SetWindowTitle(view("player").window, title.c_str());
                if (tray_) tray_->setTooltip(title);
                lastTitle_ = now;
            }
            if (dirty_ &&now -lastSave_ > 30000 && !smoke_)
            {
                try { save(); } catch (const std::exception &error) {
                    status_ = error.what();
                    lastSave_ = now;
                }
            }
            if (smoke_) smokeStep();
            if (quitAt_ &&now >= quitAt_) running_ = false;
            SDL_Delay(8);
        }
        return 0;
    }

    int runApplication(const int argc, char **argv) {
        SDL_SetAppMetadata("TTPlayer C++", "1.0.0", "org.ttplayer.cpp");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
            SDL_Log("SDL: %s", SDL_GetError());
            return 1;
        }
        if (!TTF_Init()) {
            SDL_Log("TTF: %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        int result = 0;
        try {
            Application application(argc, argv);
            result = application.run();
        } catch (const std::exception &error) {
            SDL_Log("TTPlayer: %s", error.what());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TTPlayer", error.what(), nullptr);
            result = 1;
        }
        TTF_Quit();
        SDL_Quit();
        return result;
    }
}
