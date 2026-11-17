#include "app/Application.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ttplayer::app {
    namespace {
#ifdef __APPLE__
        constexpr auto commandModifiers = SDL_KMOD_CTRL | SDL_KMOD_GUI;
#else
        constexpr auto commandModifiers = SDL_KMOD_CTRL;
#endif
    }

    void Application::processDroppedFiles(std::vector<std::filesystem::path> files) {
        std::vector<std::filesystem::path> audio;
        for (const auto &file: files) {
            try {
                const auto extension = lower(utf8(file.extension()));
                std::error_code error;
                if (std::filesystem::is_directory(file, error)) audio.push_back(file);
                else if (extension == ".skn") applySkin(file);
                else if (extension == ".lrc") loadLyrics(file);
                else if (isAudio(file) || extension == ".m3u" || extension == ".m3u8" || extension == ".pls" ||
                         extension == ".xml") audio.push_back(file);
                else status_ = tr("不支持拖入的文件：", "Unsupported dropped file: ") + utf8(file.filename());
            } catch (const std::exception &error) { status_ = error.what(); }
        }
        if (!audio.empty()) {
            if (importer_.busy()) pendingDropImports_.insert(pendingDropImports_.end(), audio.begin(), audio.end());
            else importFiles(std::move(audio), true);
        }
        dirty_ = true;
    }

    void Application::event(const SDL_Event &pending) {
#ifdef __APPLE__
        if (macReopenEvent_ && pending.type == macReopenEvent_) {
            if (!suspendedVisibility_.empty()) restoreSuspendedWindows();
            else if (!view("player").visible() && !view("mini").visible()) command("show");
            else {
                for (const auto &[name, window]: windows_) {
                    static_cast<void>(name);
                    if (window->visible() && (SDL_GetWindowFlags(window->window) & SDL_WINDOW_MINIMIZED)) SDL_RestoreWindow(window->window);
                }
                raiseVisibleWindows(view(view("mini").visible() ? "mini" : "player"));
            }
            return;
        }
#endif
        if (pending.type == SDL_EVENT_QUIT) {
            running_ = false;
            return;
        }
        bool menuEvent = false;
        try { menuEvent = popupMenu_.handleEvent(pending); } catch (const std::exception &error) {
            popupMenu_.close();
            status_ = error.what();
            return;
        }
        if (auto action = popupMenu_.takeAction()) {
            try { contextAction(*action); } catch (const std::exception &error) { status_ = error.what(); }
        }
        if (menuEvent) return;
        if (pending.type == SDL_EVENT_DROP_BEGIN) {
            dropFiles_.clear();
            dropping_ = true;
            return;
        }
        if (pending.type == SDL_EVENT_DROP_FILE) {
            if (pending.drop.data && *pending.drop.data) {
                const auto file = fromUtf8(pending.drop.data);
                if (dropping_) dropFiles_.push_back(file);
                else processDroppedFiles({file});
            }
            return;
        }
        if (pending.type == SDL_EVENT_DROP_COMPLETE) {
            dropping_ = false;
            auto files = std::move(dropFiles_);
            dropFiles_.clear();
            if (!files.empty()) processDroppedFiles(std::move(files));
            return;
        }
        if (pending.type == SDL_EVENT_DISPLAY_REMOVED || pending.type == SDL_EVENT_DISPLAY_ADDED ||
            pending.type == SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED || pending.type ==
            SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED ||
            pending.type == SDL_EVENT_DISPLAY_MOVED || pending.type == SDL_EVENT_DISPLAY_ORIENTATION ||
            pending.type == SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED) {
            recoverWindows();
            return;
        }
        UiWindow *window = nullptr;
        auto *native = SDL_GetWindowFromEvent(&pending);
        for (auto &[name, candidate]: windows_) {
            static_cast<void>(name);
            if (candidate->window == native) window = candidate.get();
        }
        if (pending.type == SDL_EVENT_KEY_DOWN) {
            keyboard(pending.key, window);
            return;
        }
        if (!window) return;
        if (pending.type == SDL_EVENT_WINDOW_FOCUS_GAINED && (window->name == "player" || window->name == "mini") &&
            SDL_GetKeyboardFocus() == window->window) {
            raiseVisibleWindows(*window);
            return;
        }
#ifdef __APPLE__
        if (pending.type == SDL_EVENT_MOUSE_MOTION && windowDrag_.usesCapturedPointer()) {
            SDL_FPoint pointer{};
            SDL_GetGlobalMouseState(&pointer.x, &pointer.y);
            updatePointerWindowDrag(pointer, (SDL_GetModState() & SDL_KMOD_ALT) != 0);
            return;
        }
        if (pending.type == SDL_EVENT_MOUSE_BUTTON_UP && pending.button.button == SDL_BUTTON_LEFT && windowDrag_.
            usesCapturedPointer()) {
            finishPointerWindowDrag(false);
            return;
        }
#endif
        if (pending.type == SDL_EVENT_WINDOW_RESTORED && (window->name == "player" || window->name == "mini") && !
            suspendedVisibility_.empty()) {
            command("show");
        } else if (pending.type == SDL_EVENT_WINDOW_MINIMIZED && (window->name == "player" || window->name == "mini") &&
                   suspendedVisibility_.empty()) {
            command("minimize");
        } else if (pending.type == SDL_EVENT_WINDOW_HIDDEN && windowDrag_.root() == window->name) {
            endWindowDrag();
        } else if (pending.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (window->name == "player") command("minimize");
            else if (window->name == "mini") command("return");
            else if (window->name == "progress") importer_.cancel();
            else window->show(false);
            dirty_ = true;
        } else if (pending.type == SDL_EVENT_MOUSE_MOTION) {
            SDL_RenderCoordinatesFromWindow(window->renderer, pending.motion.x, pending.motion.y, &window->mouseX,
                                            &window->mouseY);
            if (dragging_ &&dragWindow_ == SDL_GetWindowID(window->window) && dragging_->slide)
            {
                const auto &hit = *dragging_;
                const auto track = hit.dragBounds.w > 0 ? hit.dragBounds : hit.bounds;
                const float ratio = hit.vertical
                                        ? 1 - (window->mouseY - track.y) / std::max(1.0F, track.h)
                                        : (window->mouseX - track.x) / std::max(1.0F, track.w);
                hit.slide(std::clamp(ratio, 0.0F, 1.0F));
            }
            if (window->name == "playlist" && songDragIndex_ >= 0) {
                if (std::hypot(window->mouseX - songDragX_, window->mouseY - songDragY_) > 4) songDragging_ = true;
                if (songDragging_) {
                    songDropIndex_ = static_cast<int>(playlist().songs.size());
                    for (const auto &hit: window->hits) if (
                        hit.songIndex >= 0 && contains(hit.bounds, window->mouseX, window->mouseY)) {
                        songDropIndex_ = hit.songIndex + (window->mouseY > hit.bounds.y + hit.bounds.h / 2 ? 1 : 0);
                        break;
                    }
                }
            }
            if (window->name == "lyric" && lyricDragging_) {
                const float rowHeight = lyricRowHeight_;
                const float difference = (window->mouseY - lyricDragY_) / rowHeight;
                lyricPreviewLine_ = std::clamp(lyricPreviewLine_ - difference, 0.0F,
                                               static_cast<float>(std::max(
                                                   0, static_cast<int>(lyrics_.lines.size()) - 1)));
                lyricDragY_ = window->mouseY;
            }
        } else if (pending.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
            window->mouseX = window->mouseY = -1;
        } else if (pending.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            if (pending.button.button == SDL_BUTTON_RIGHT) {
                if (window->name == "dialog") return;
                SDL_RenderCoordinatesFromWindow(window->renderer, pending.button.x, pending.button.y, &window->mouseX,
                                                &window->mouseY);
                if (window->name == "playlist" && window->mouseX >= playlistDividerX_) for (
                    const auto &hit: window->hits) if (
                    hit.songIndex >= 0 && contains(hit.bounds, window->mouseX, window->mouseY) && !selectedSongs_.
                    contains(hit.songIndex)) {
                    selectSong(hit.songIndex);
                    break;
                }
                openContext(
                    *window, window->name == "lyric"
                                 ? 8
                                 : window->name == "desktop"
                                       ? 14
                                       : window->name == "eq"
                                             ? 9
                                             : window->name == "playlist" && window->mouseX < playlistDividerX_
                                                   ? 2
                                                   : 7, window->mouseX, window->mouseY);
                return;
            }
            if (pending.button.button != SDL_BUTTON_LEFT) return;
#ifdef __APPLE__
            if (beginPointerWindowDrag(*window, pending.button)) return;
#endif
            SDL_RenderCoordinatesFromWindow(window->renderer, pending.button.x, pending.button.y, &window->mouseX,
                                            &window->mouseY);
            if (!contextWindow_.empty() && (contextWindow_ != window->name || !contains(
                                                contextBounds_, window->mouseX, window->mouseY))) {
                contextWindow_.clear();
                dirty_ = true;
                inputField_ = 0;
                SDL_StopTextInput(window->window);
                render(*window);
            }
            inputField_ = 0;
            inputCaret_ = std::string::npos;
            inputPreedit_.clear();
            inputSelectedAll_ = false;
            SDL_StopTextInput(window->window);
            clickCount_ = pending.button.clicks;
            for (auto hit = window->hits.rbegin(); hit != window->hits.rend(); ++hit) {
                if (!contains(hit->bounds, window->mouseX, window->mouseY)) continue;
                const auto action = *hit;
                if (action.songIndex >= 0) {
                    songDragIndex_ = action.songIndex;
                    songDragX_ = window->mouseX;
                    songDragY_ = window->mouseY;
                    songDragging_ = false;
                    songDropIndex_ = -1;
                    const auto modifiers = SDL_GetModState();
                    if (!selectedSongs_.contains(action.songIndex) || modifiers & (commandModifiers | SDL_KMOD_SHIFT))
                        selectSong(action.songIndex, modifiers);
                    SDL_CaptureMouse(true);
                }
                if (action.lyric && !lyrics_.lines.empty()) {
                    lyricDragging_ = true;
                    lyricDragY_ = window->mouseY;
                    const float rowHeight = lyricRowHeight_;
                    lyricPreviewLine_ = std::clamp(
                        static_cast<float>(std::max(0, lyrics_.active(engine_.positionMs()))) + (
                            window->mouseY - action.bounds.y - action.bounds.h / 2) / rowHeight, 0.0F,
                        static_cast<float>(lyrics_.lines.size() - 1));
                    SDL_CaptureMouse(true);
                }
                if (action.slide) {
                    dragging_ = action;
                    dragWindow_ = SDL_GetWindowID(window->window);
                    SDL_CaptureMouse(true);
                    const auto track = action.dragBounds.w > 0 ? action.dragBounds : action.bounds;
                    const float ratio = action.vertical
                                            ? 1 - (window->mouseY - track.y) / std::max(1.0F, track.h)
                                            : (window->mouseX - track.x) / std::max(1.0F, track.w);
                    action.slide(std::clamp(ratio, 0.0F, 1.0F));
                } else if (action.click) action.click();
                break;
            }
        } else if (pending.type == SDL_EVENT_MOUSE_BUTTON_UP || pending.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
            if (windowDrag_.usesCapturedPointer()) finishPointerWindowDrag(false);
            if (pending.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (songDragging_) moveSongs(songDropIndex_);
                else if (songDragIndex_ >= 0 && !(SDL_GetModState() & (commandModifiers | SDL_KMOD_SHIFT))) selectSong(
                    songDragIndex_);
                if (lyricDragging_ && !lyrics_.lines.empty()) {
                    const auto index = std::clamp(static_cast<int>(std::round(lyricPreviewLine_)), 0,
                                                  static_cast<int>(lyrics_.lines.size()) - 1);
                    engine_.seek(std::max<std::int64_t>(0, lyrics_.lines[index].time - lyrics_.offset));
                }
            }
            songDragIndex_ = songDropIndex_ = -1;
            songDragging_ = lyricDragging_ = false;
            dragging_.reset();
            dragWindow_ = 0;
            SDL_CaptureMouse(false);
        } else if (pending.type == SDL_EVENT_MOUSE_WHEEL) {
            const float delta = pending.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -pending.wheel.y : pending.wheel.y;
            auto &scroll = window->name == "playlist" && window->mouseX < playlistDividerX_
                               ? playlistScroll_
                               : window->scroll;
            scroll = std::max(0, scroll - static_cast<int>(std::round(delta * 3)));
        } else if (pending.type == SDL_EVENT_WINDOW_HIT_TEST && !nativeWindowManagement_ && windowDrag_.root().empty()
                   &&
                   (layout::coreWindow(window->name) || window->name == "desktop") &&
                   (SDL_GetGlobalMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) { beginWindowDrag(*window); } else if
        (pending.type == SDL_EVENT_WINDOW_MOVED) {
            trackWindowMove(*window);
            dirty_ = true;
        } else if (pending.type == SDL_EVENT_WINDOW_RESIZED) dirty_ = true;
        else if (pending.type == SDL_EVENT_TEXT_INPUT && pending.text.text) insertInput(pending.text.text);
        else if (pending.type == SDL_EVENT_TEXT_EDITING) inputPreedit_ = inputField_ != 0 && pending.edit.text
                                                                             ? pending.edit.text
                                                                             : "";
    }

    void Application::keyboard(const SDL_KeyboardEvent &key, UiWindow *window) {
        if (windowDrag_.usesCapturedPointer() && key.key == SDLK_ESCAPE) {
            finishPointerWindowDrag(true);
            return;
        }
        const bool control = (key.mod & commandModifiers) != 0;
        if (control && key.key == SDLK_Q) {
            running_ = false;
            return;
        }
        if (window && window->name == "progress") {
            if (key.key == SDLK_ESCAPE) importer_.cancel();
            return;
        }
        if (inputField_ != 0 && !inputPreedit_.empty()) return;
        if (window && window->name == "dialog" && key.key == SDLK_ESCAPE) {
            window->show(false);
            inputField_ = 0;
            SDL_StopTextInput(window->window);
            return;
        }
        if (key.key == SDLK_ESCAPE) {
            const bool dismissMenu = !contextWindow_.empty() || inputField_ != 0;
            contextWindow_.clear();
            inputField_ = 0;
            if (window) SDL_StopTextInput(window->window);
            if (!dismissMenu && window && window->name != "player" && window->name != "mini") window->show(false);
            return;
        }
        if (inputField_ != 0 && window && ((inputField_ == 1 && window->name == "settings") || (
                                               inputField_ == 2 && window->name == "playlist") || (
                                               inputField_ >= 10 && window->name == "dialog"))) {
            auto *pointer = inputValue();
            if (!pointer) return;
            auto &value = *pointer;
            inputCaret_ = std::min(inputCaret_, value.size());
            auto previous = [&] {
                auto position = inputCaret_;
                if (position > 0) {
                    --position;
                    while (position > 0 && (static_cast<unsigned char>(value[position]) & 0xc0) == 0x80) --position;
                }
                return position;
            };
            auto next = [&] {
                auto position = inputCaret_;
                if (position < value.size()) {
                    ++position;
                    while (position < value.size() && (static_cast<unsigned char>(value[position]) & 0xc0) == 0x80) ++
                            position;
                }
                return position;
            };
            if (control && key.key == SDLK_V) {
                char *clipboard = SDL_GetClipboardText();
                if (clipboard) {
                    insertInput(clipboard);
                    SDL_free(clipboard);
                }
            } else if (control && (key.key == SDLK_C || key.key == SDLK_X)) {
                SDL_SetClipboardText(value.c_str());
                if (key.key == SDLK_X) {
                    value.clear();
                    inputCaret_ = 0;
                    inputSelectedAll_ = false;
                }
            } else if (control && key.key == SDLK_A) inputSelectedAll_ = true;
            else if (key.key == SDLK_HOME || key.key == SDLK_END || key.key == SDLK_LEFT || key.key == SDLK_RIGHT) {
                inputCaret_ = key.key == SDLK_HOME
                                  ? 0
                                  : key.key == SDLK_END
                                        ? value.size()
                                        : key.key == SDLK_LEFT
                                              ? inputSelectedAll_
                                                    ? 0
                                                    : previous()
                                              : inputSelectedAll_
                                                    ? value.size()
                                                    : next();
                inputSelectedAll_ = false;
            } else if (key.key == SDLK_BACKSPACE || key.key == SDLK_DELETE) {
                if (inputSelectedAll_) {
                    value.clear();
                    inputSelectedAll_ = false;
                    inputCaret_ = 0;
                } else if (key.key == SDLK_BACKSPACE) {
                    const auto start = previous();
                    value.erase(start, inputCaret_ - start);
                    inputCaret_ = start;
                } else value.erase(inputCaret_, next() - inputCaret_);
            } else if (key.key == SDLK_RETURN && window->name == "dialog") submitPanel();
            else if (key.key == SDLK_RETURN && inputField_ == 1 && page_ == 2) startSearch();
            else if (key.key == SDLK_TAB && window->name == "dialog" && panelKind_ == "lyrics") {
                inputField_ = inputField_ == 11 ? 12 : inputField_ == 12 ? 13 : 11;
                inputCaret_ = std::string::npos;
                inputSelectedAll_ = false;
            }
            return;
        }
        if (window && window->name == "dialog") {
            if (key.key == SDLK_RETURN) submitPanel();
            return;
        }
        if (control && (key.mod & SDL_KMOD_SHIFT) && key.key == SDLK_R) {
            command("resetlayout");
            return;
        }
        if (control && key.key == SDLK_A && window && window->name == "playlist") {
            selectedSongs_.clear();
            for (int index = 0; index < static_cast<int>(playlist().songs.size()); ++index) selectedSongs_.
                    insert(index);
        } else if (control && key.key == SDLK_O) command((key.mod & SDL_KMOD_SHIFT) ? "folder" : "open");
        else if (control && key.key == SDLK_I) command("import");
        else if (control && key.key == SDLK_E) command("export");
        else if (control && key.key == SDLK_F) {
            auto &list = view("playlist");
            list.show(true);
            inputField_ = 2;
            inputCaret_ = std::string::npos;
            inputPreedit_.clear();
            SDL_StartTextInput(list.window);
            if (skin_ &&skin_->window("playlist_window"))
            {
                contextWindow_ = "playlist";
                contextKind_ = 4;
                contextBounds_ = {12, 28, 240, 46};
            }
        } else if (control && key.key == SDLK_L) command("lock");
        else if (key.key == SDLK_SPACE && !key.repeat) playPause();
        else if (key.key == SDLK_S && !key.repeat) command("stop");
        else if (key.key == SDLK_N && !key.repeat) advance(1);
        else if (key.key == SDLK_P && !key.repeat) advance(-1);
        else if (key.key == SDLK_LEFT) engine_.seek(engine_.positionMs() - 5000);
        else if (key.key == SDLK_RIGHT) engine_.seek(engine_.positionMs() + 5000);
        else if (key.key == SDLK_UP || key.key == SDLK_DOWN) {
            if (window && window->name == "playlist") {
                if (!(key.mod & SDL_KMOD_SHIFT) || selectionAnchor_ < 0) selectionAnchor_ = std::max(0, selected_);
                selected_ = std::clamp(selected_ + (key.key == SDLK_UP ? -1 : 1), 0,
                                       std::max(0, static_cast<int>(playlist().songs.size()) - 1));
                selectedSongs_.clear();
                if (key.mod & SDL_KMOD_SHIFT) {
                    for (int index = std::min(selectionAnchor_, selected_);
                         index <= std::max(selectionAnchor_, selected_); ++index) selectedSongs_.insert(index);
                } else if (!playlist().songs.empty()) selectedSongs_.insert(selected_);
                if (selected_ < window->scroll) window->scroll = selected_;
                if (selected_ > window->scroll + 8) window->scroll = selected_ - 8;
            } else {
                engine_.setVolume(engine_.volume() + (key.key == SDLK_UP ? 0.05F : -0.05F));
                dirty_ = true;
            }
        } else if (key.key == SDLK_RETURN && window && window->name == "playlist") playSong(
            settings_.playlist, selected_);
        else if (key.key == SDLK_DELETE && window && window->name == "playlist") removeSong();
        else if (key.key == SDLK_F1) {
            page_ = 4;
            command("tools");
        } else if (key.key == SDLK_F2) command("skin");
        else if (key.key == SDLK_F3) openPanel("lyrics");
        else if (key.key == SDLK_F4) command("visual");
        else if (key.key == SDLK_F5) command("lyric");
        else if (key.key == SDLK_F6) command("desktop");
        else if (key.key == SDLK_F7) command("equalizer");
        else if (key.key == SDLK_F8) command("playlist");
        else if (key.key == SDLK_F9) command("minimode");
        else if (key.key == SDLK_F10) command("settings");
        else if ((key.key == SDLK_EQUALS || key.key == SDLK_MINUS) && !nowSong_.path.empty()) {
            lyrics_.offset = std::clamp<std::int64_t>(lyrics_.offset + (key.key == SDLK_EQUALS ? 100 : -100), -3600000,
                                                      3600000);
            settings_.lyricOffsets[utf8(nowSong_.path)] = lyrics_.offset;
            status_ = "LRC offset: " + std::to_string(lyrics_.offset) + " ms";
            dirty_ = true;
        }
    }

    float Application::sliderValue(const std::string_view name) const {
        if (name == "progress") return engine_.durationMs() > 0
                                           ? static_cast<float>(engine_.positionMs()) / static_cast<float>(engine_.
                                                 durationMs())
                                           : 0;
        if (name == "volume") return engine_.volume();
        if (name == "preamp") return (settings_.equalizer.preamp + 12) / 24;
        if (name == "balance") return (settings_.equalizer.balance + 1) / 2;
        if (name == "surround") return settings_.equalizer.surround;
        if (name.starts_with("band") && name.size() == 5 && name[4] >= '0' && name[4] <= '9') return
                (settings_.equalizer.bands[static_cast<std::size_t>(name[4] - '0')] + 12) / 24;
        return 0;
    }

    void Application::changeSlider(const std::string_view name, const float ratio) {
        if (name == "progress") engine_.seek(
            static_cast<std::int64_t>(ratio * static_cast<float>(engine_.durationMs())));
        else if (name == "volume") engine_.setVolume(ratio);
        else {
            if (name == "preamp") settings_.equalizer.preamp = ratio * 24 - 12;
            else if (name == "balance") settings_.equalizer.balance = ratio * 2 - 1;
            else if (name == "surround") settings_.equalizer.surround = ratio;
            else if (name.starts_with("band") && name.size() == 5 && name[4] >= '0' && name[4] <= '9')
                settings_.equalizer.bands[static_cast<std::size_t>(name[4] - '0')] = ratio * 24 - 12;
            if (name == "preamp" || name.starts_with("band")) settings_.equalizer.enabled = true;
            engine_.equalizer().configure(settings_.equalizer);
        }
        dirty_ = true;
    }

    void Application::renderSkin(UiWindow &window, const SkinWindow &definition) {
        window.skinBackground(*skin_, definition);
        for (const auto &control: definition.controls) {
            auto layoutControl = control;
            if ((control.name == "playlist" && window.name == "playlist") || (
                    control.name == "lyric" && window.name == "lyric")) layoutControl.attributes["align"] = "fill";
            auto bounds = controlBounds(definition, layoutControl, window.width, window.height);
            if (bounds.w <= 0 || bounds.h <= 0) continue;
            const auto &name = control.name;
            const bool playing = engine_.state() == media::PlaybackState::playing;
            if ((name == "play" && playing) || (name == "pause" && !playing)) continue;
            if (name.starts_with("mode_")) {
                constexpr std::string_view modes[]{
                    "mode_single", "mode_circle", "mode_slider", "mode_loop", "mode_random"
                };
                if (name != modes[settings_.mode]) continue;
            }
            if (name == "info" || name == "status" || name == "stereo" || (
                    (name == "song" || name == "title") && window.name == "mini" && control.value("image").empty())) {
                int size = 12;
                try { size = std::clamp(std::stoi(control.value("font_size", "12")), 8, 24); } catch (const
                    std::exception &) {
                }
                const auto title = name == "stereo"
                                       ? message("player.stereo")
                                       : name == "status"
                                             ? message(playing
                                                           ? "status.playing"
                                                           : engine_.state() == media::PlaybackState::paused
                                                                 ? "status.paused"
                                                                 : "status.stopped")
                                             : nowSong_.title.empty()
                                                   ? message("app.name")
                                                   : nowSong_.artist.empty()
                                                         ? nowSong_.title
                                                         : nowSong_.artist + "  " + nowSong_.title;
                const auto metrics = window.measure(title, size, control.value("font"));
                if (!control.value("bkgnd").empty()) window.fill(bounds, parseColor(control.value("bkgnd")));
                const auto align = lower(control.value("align"));
                const float left = bounds.x + (align.find("right") != std::string::npos
                                                   ? std::max(0.0F, bounds.w - metrics.x)
                                                   : 0);
                if (window.name == "mini" && name != "status" && name != "stereo" && !nowSong_.path.empty()) {
                    auto format = utf8(nowSong_.path.extension());
                    if (!format.empty()) format.erase(0, 1);
                    std::transform(format.begin(), format.end(), format.begin(), [](const unsigned char character) {
                        return static_cast<char>(std::toupper(character));
                    });
                    const auto duration = engine_.durationMs() > 0 ? engine_.durationMs() : nowSong_.duration;
                    const std::vector<std::string> values{
                        nowSong_.title.empty() ? message("song.unknownTitle") : nowSong_.title,
                        nowSong_.artist.empty() ? message("song.unknownArtist") : "♪ " + nowSong_.artist,
                        nowSong_.album.empty() ? message("song.unknownAlbum") : "♫ " + nowSong_.album,
                        format.empty() ? message("song.unknownFormat") : message("song.formatPrefix") + format,
                        duration > 0
                            ? message("song.durationPrefix") + timeText(duration)
                            : message("song.unknownLength")
                    };
                    window.carouselText(bounds, values, size, parseColor(control.value("color")), control.value("font"),
                                        align);
                } else window.text(title, left, bounds.y + (bounds.h - metrics.y) / 2, size,
                                   parseColor(control.value("color")), bounds.x + bounds.w - left,
                                   align.find("center") != std::string::npos, control.value("font"));
            } else if (name == "icon") {
                auto *icon = skin_->image(control.value("icon", control.value("image")));
                if (icon) window.bitmap(icon, bounds);
                else if ((icon = skin_->image("TTPlayer.ico"))) window.bitmap(icon, {
                                                                                  bounds.x,
                                                                                  bounds.y + (bounds.h - icon->h) / 2,
                                                                                  static_cast<float>(icon->w),
                                                                                  static_cast<float>(icon->h)
                                                                              });
                window.hits.push_back({
                    bounds, [this, &window, bounds] { openContext(window, 7, bounds.x, bounds.y + bounds.h); }, {},
                    false
                });
            } else if (name == "led") {
                auto time = timeText(remainingTime_
                                         ? std::max<std::int64_t>(0, engine_.durationMs() - engine_.positionMs())
                                         : engine_.positionMs());
                if (time.find(':') == 1) time.insert(time.begin(), '0');
                if (remainingTime_) time.insert(time.begin(), '-');
                auto *sheet = skin_->image(control.value("image"));
                if (!sheet) window.text(time, bounds.x, bounds.y - 2, 14, {210, 240, 250, 255}, bounds.w, true);
                else {
                    const float digitWidth = static_cast<float>(sheet->w) / 12;
                    const float characterWidth = std::min(digitWidth, bounds.w / static_cast<float>(time.size()));
                    const float left = bounds.x + std::max(
                                           0.0F, bounds.w - characterWidth * static_cast<float>(time.size()));
                    for (std::size_t index = 0; index < time.size(); ++index) {
                        const int digit = time[index] == ':' ? 10 : time[index] == '-' ? 11 : time[index] - '0';
                        const SDL_FRect source{digit * digitWidth, 0, digitWidth, static_cast<float>(sheet->h)};
                        window.bitmap(sheet, {
                                          left + static_cast<float>(index) * characterWidth,
                                          bounds.y + (bounds.h - sheet->h) / 2, characterWidth,
                                          static_cast<float>(sheet->h)
                                      }, &source);
                    }
                }
                window.hits.push_back({bounds, [this] { remainingTime_ = !remainingTime_; }, {}, false});
            } else if (name == "visual") {
                window.visual(bounds, spectrum_, settings_.visual, waveform_, &skin_->style("visual"));
                window.hits.push_back({bounds, [this] { command("visual"); }, {}, false});
            } else if (name == "playlist" && window.name == "playlist") renderPlaylist(window, bounds, true);
            else if (name == "lyric" && window.name == "lyric") renderLyrics(window, bounds);
            else if (name == "progress" || name == "volume" || name == "preamp" || name == "balance" || name ==
                     "surround" || name.starts_with("band")) {
                const float ratio = std::clamp(sliderValue(name), 0.0F, 1.0F);
                const bool vertical = control.value("vertical") == "true" || name == "preamp" || name.
                                      starts_with("band");
                window.bitmap(skin_->image(control.value("bar_image")), bounds);
                auto *thumb = skin_->image(control.value("thumb_image"));
                const float thumbHeight = thumb ? static_cast<float>(thumb->h) : 0;
                const float thumbWidth = thumb ? static_cast<float>(thumb->w % 4 == 0 ? thumb->w / 4 : thumb->w) : 0;
                if (name == "progress") {
                    const SDL_FRect track{bounds.x, bounds.y + 2, bounds.w, std::max(1.0F, bounds.h - 4)};
                    if (auto *image = skin_->image(control.value("fill_image2"))) window.bitmap(image, track);
                    if (ratio > 0) if (auto *image = skin_->image(control.value("fill_image"))) window.bitmap(
                        image, {track.x, track.y, track.w * ratio, track.h});
                } else if (name == "volume")
                    if (auto *image = skin_->image(control.value("fill_image"))) {
                        const float end = ratio * std::max(0.0F, bounds.w - thumbWidth) + thumbWidth / 2;
                        const SDL_FRect source{
                            0, 0, std::min(static_cast<float>(image->w), (image->w - 1) * end / bounds.w + 1),
                            static_cast<float>(image->h)
                        };
                        window.bitmap(image, {
                                          bounds.x, bounds.y + (bounds.h - image->h) / 2, end,
                                          static_cast<float>(image->h)
                                      }, &source);
                    }
                if (vertical)
                    if (auto *image = skin_->image(control.value("fill_image"))) {
                        const float fillStart = (1 - ratio) * std::max(0.0F, bounds.h - thumbHeight) + thumbHeight / 2;
                        const float fillHeight = bounds.h - fillStart;
                        const float sourceHeight = image->h * fillHeight / bounds.h;
                        const SDL_FRect source{0, image->h - sourceHeight, static_cast<float>(image->w), sourceHeight};
                        window.bitmap(image, {
                                          bounds.x + (bounds.w - image->w) / 2, bounds.y + fillStart,
                                          static_cast<float>(image->w), fillHeight
                                      }, &source);
                    }
                auto dragBounds = bounds;
                if (thumb) {
                    const SDL_FRect source{0, 0, thumbWidth, thumbHeight};
                    const SDL_FRect target{
                        vertical
                            ? bounds.x + (bounds.w - thumbWidth) / 2
                            : bounds.x + ratio * std::max(0.0F, bounds.w - thumbWidth),
                        vertical
                            ? bounds.y + (1 - ratio) * std::max(0.0F, bounds.h - thumbHeight)
                            : bounds.y + (bounds.h - thumbHeight) / 2,
                        thumbWidth, thumbHeight
                    };
                    window.bitmap(thumb, target, &source);
                    if (vertical) {
                        dragBounds.y += thumbHeight / 2;
                        dragBounds.h = std::max(1.0F, bounds.h - thumbHeight);
                    } else {
                        dragBounds.x += thumbWidth / 2;
                        dragBounds.w = std::max(1.0F, bounds.w - thumbWidth);
                    }
                }
                window.hits.push_back({
                    bounds, {}, [this, name](const float value) { changeSlider(name, value); }, vertical, dragBounds
                });
            } else if (name == "toolbar" && window.name == "playlist") {
                window.bitmap(skin_->image(control.value("image")), bounds);
                const int segmentCount = 7;
                for (int index = 0; index < segmentCount; ++index) {
                    const SDL_FRect segment{
                        bounds.x + bounds.w / segmentCount * index, bounds.y, bounds.w / segmentCount, bounds.h
                    };
                    if (contains(segment, window.mouseX, window.mouseY)) {
                        if (auto *hot = skin_->image(control.value("hot_image"))) {
                            const SDL_FRect source{
                                static_cast<float>(hot->w) / segmentCount * index, 0,
                                static_cast<float>(hot->w) / segmentCount, static_cast<float>(hot->h)
                            };
                            window.bitmap(hot, segment, &source);
                        }
                    }
                    window.hits.push_back({
                        segment, [this, &window, index, segment] {
                            openContext(window, index, segment.x, segment.y + segment.h);
                        },
                        {}, false
                    });
                }
            } else if (auto *image = skin_->image(control.value("image"))) {
                const int columns = name == "title" ? 1 : image->w % 4 == 0 ? 4 : 1;
                const int frameWidth = image->w / columns;
                const int frameHeight = image->h;
                bounds = controlBounds(definition, control, window.width, window.height, static_cast<float>(frameWidth),
                                       static_cast<float>(frameHeight));
                const bool hover = contains(bounds, window.mouseX, window.mouseY);
                const int frame = hover ? ((SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) ? 2 : 1) : 0;
                const bool checked = name == "enabled" && settings_.equalizer.enabled;
                const int actualFrame = name == "mute" && columns >= 3
                                            ? (engine_.volume() == 0 ? 2 : 0)
                                            : checked && columns >= 4 && !hover ? 3 : std::min(frame, columns - 1);
                const SDL_FRect source{
                    static_cast<float>(actualFrame * frameWidth), 0, static_cast<float>(frameWidth),
                    static_cast<float>(frameHeight)
                };
                window.bitmap(image, bounds, &source);
                window.hits.push_back({
                    bounds, [this, name, windowName = window.name] {
                        if ((name == "close" || name == "exit") && windowName == "player") command("minimize");
                        else if ((name == "close" || name == "exit") && windowName == "mini") command("return");
                        else if (name == "close") view(windowName).show(false);
                        else if (name == "profile") openContext(view(windowName), 10, view(windowName).mouseX,
                                                                view(windowName).mouseY);
                        else command(name);
                    },
                    {}, false
                });
            }
            static const std::map<std::string, std::string> tooltips{
                {"play", "tooltip.play"}, {"pause", "tooltip.pause"}, {"stop", "tooltip.stop"},
                {"prev", "tooltip.prev"}, {"next", "tooltip.next"},
                {"mute", "tooltip.mute"}, {"set", "tooltip.settings"}, {"open", "tooltip.openFile"},
                {"skin", "tooltip.skin"}, {"browser", "tooltip.browser"},
                {"minimize", "tooltip.minimize"}, {"exit", "tooltip.quit"}, {"close", "tooltip.quit"},
                {"normal", "tooltip.restoreFull"}, {"return", "tooltip.restoreFull"}, {"minimode", "menu.miniMode"},
                {"about", "tooltip.about"},
                {"mode_single", "mode.single"}, {"mode_circle", "mode.singleLoop"}, {"mode_slider", "mode.sequential"},
                {"mode_loop", "mode.listLoop"}, {"mode_random", "mode.random"},
                {"equalizer", "menu.equalizer"}, {"eq", "menu.equalizer"}, {"desklrc", "menu.desktopLyric"},
                {"options", "menu.options"}
            };
            if (const auto tooltip = tooltips.find(name); tooltip != tooltips.end()) window.tip(
                bounds, message(window.name == "mini" && (name == "exit" || name == "close")
                                    ? "tooltip.restoreFull"
                                    : tooltip->second));
            else if (name == "lyric" && window.name != "lyric") window.tip(bounds, message("menu.lyricShow"));
            else if (name == "playlist" && window.name != "playlist") window.tip(bounds, message("playlist.title"));
        }
    }
}
