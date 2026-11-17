#include "app/Application.hpp"

#include <algorithm>
#include <sstream>

namespace ttplayer::app {
    namespace {
        constexpr SDL_Color textColor{33, 33, 33, 255};
        constexpr SDL_Color secondary{120, 120, 120, 255};
        constexpr SDL_Color border{217, 217, 217, 255};
        constexpr SDL_Color blue{22, 119, 255, 255};
    }

    bool Application::confirmAction(const std::string &prompt) {
        if (smoke_) return true;
        if (!suspendedVisibility_.empty()) restoreSuspendedWindows();
        const auto accept = tr("确定", "OK"), cancel = message("dialog.cancel");
        const SDL_MessageBoxButtonData buttons[]{
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, cancel.c_str()}, {0, 1, accept.c_str()}
        };
        const SDL_MessageBoxData dialog{
            SDL_MESSAGEBOX_WARNING, dialogOwner().window, "TTPlayer", prompt.c_str(), 2, buttons, nullptr
        };
        int selected = 0;
        return SDL_ShowMessageBox(&dialog, &selected) && selected == 1;
    }

    void Application::openPanel(std::string kind, std::string title, std::string value,
                                std::function<void(const std::string &)> submit) {
        if (!suspendedVisibility_.empty()) restoreSuspendedWindows();
        auto &window = view("dialog");
        window.show(false);
        SDL_SetWindowModal(window.window, false);
        SDL_StopTextInput(window.window);
        inputField_ = 0;
        inputSelectedAll_ = false;
        inputCaret_ = std::string::npos;
        inputPreedit_.clear();
        window.clearCache();
        previewSkin_.reset();
        previewIndex_ = -1;
        panelKind_ = std::move(kind);
        panelValue_ = std::move(value);
        panelSubmit_ = std::move(submit);
        window.scroll = 0;
        contextWindow_.clear();
        const int width = panelKind_ == "lyrics" ? 580 : panelKind_ == "skin" ? 600 : 480;
        const int height = panelKind_ == "lyrics"
                               ? 540
                               : panelKind_ == "skin"
                                     ? 400
                                     : panelKind_ == "options" || panelKind_ == "format"
                                           ? 260
                                           : panelKind_ == "info"
                                                 ? 400
                                                 : 180;
        SDL_SetWindowMinimumSize(window.window, width, height);
        SDL_SetWindowSize(window.window, width, height);
        SDL_SetWindowParent(window.window, view(view("mini").visible() ? "mini" : "player").window);
        SDL_SetWindowModal(window.window, true);
        centerWindowOn(window, view(view("mini").visible() ? "mini" : "player"));
        if (title.empty()) title = panelKind_ == "skin"
                                       ? message("skinSelect.title")
                                       : panelKind_ == "lyrics"
                                             ? message("lyricSearch.title")
                                             : message("dialog.optionsTitle");
        SDL_SetWindowTitle(window.window, title.c_str());
        if (panelKind_ == "skin") {
            for (std::size_t index = 0; index < skins_.size(); ++index) if (skin_ &&skins_[index]== skin_->path())
            {
                previewIndex_ = static_cast<int>(index);
                break;
            }
            if (previewIndex_ < 0 && !skins_.empty()) previewIndex_ = 0;
            window.scroll = std::max(0, previewIndex_ - 3);
            if (previewIndex_ >= 0) try {
                previewSkin_ = std::make_unique<Skin>();
                previewSkin_->load(skins_[previewIndex_], assets_ / "ico/TTPlayer.ico");
            } catch (const std::exception &error) { status_ = error.what(); }
        } else if (panelKind_ == "lyrics") {
            panelArtist_ = nowSong_.artist;
            panelTitle_ = nowSong_.title;
            panelSavePath_ = nowSong_.path.empty() ? "" : utf8(lyricsRepository_.cachePath(nowSong_.path));
            resultIndex_ = -1;
            query_ = panelArtist_ + " " + panelTitle_;
        }
        window.show(true);
        if (panelKind_ == "input") {
            inputField_ = 10;
            SDL_StartTextInput(window.window);
        }
    }

    std::string *Application::inputValue() {
        switch (inputField_) {
            case 1: return &query_;
            case 2: return &filter_;
            case 10: return &panelValue_;
            case 11: return &panelArtist_;
            case 12: return &panelTitle_;
            case 13: return &panelSavePath_;
            default: return nullptr;
        }
    }

    void Application::submitPanel() {
        if (panelKind_ == "input") {
            auto action = panelSubmit_;
            auto value = panelValue_;
            view("dialog").show(false);
            inputField_ = 0;
            SDL_StopTextInput(view("dialog").window);
            if (action) action(value);
        } else if (panelKind_ == "lyrics") {
            query_ = panelArtist_ + " " + panelTitle_;
            startSearch();
        } else view("dialog").show(false);
    }

    void Application::panelButton(UiWindow &window, const SDL_FRect bounds, const std::string_view label,
                                  std::function<void()> action, const bool primary) {
        const bool hover = contains(bounds, window.mouseX, window.mouseY);
        window.fill(bounds, primary
                                ? hover
                                      ? SDL_Color{64, 150, 255, 255}
                                      : blue
                                : hover
                                      ? SDL_Color{245, 250, 255, 255}
                                      : SDL_Color{255, 255, 255, 255});
        window.outline(bounds, primary || hover ? blue : border);
        const auto metrics = window.measure(label, 13);
        window.text(label, bounds.x + 4, bounds.y + (bounds.h - metrics.y) / 2, 13,
                    primary ? SDL_Color{255, 255, 255, 255} : textColor, bounds.w - 8, true);
        window.hits.push_back({bounds, std::move(action), {}, false});
    }

    void Application::panelInput(UiWindow &window, const SDL_FRect bounds, const int field,
                                 const std::string_view placeholder) {
        std::string *value = field == 10
                                 ? &panelValue_
                                 : field == 11
                                       ? &panelArtist_
                                       : field == 12
                                             ? &panelTitle_
                                             : &panelSavePath_;
        window.fill(bounds, {255, 255, 255, 255});
        window.outline(bounds, inputField_ == field ? blue : border);
        renderInputText(window, bounds, *value, field, placeholder, true);
        window.hits.push_back({
            bounds, [this, &window, field, bounds] {
                inputField_ = field;
                inputSelectedAll_ = false;
                inputPreedit_.clear();
                auto *value = inputValue();
                inputCaret_ = value ? value->size() : 0;
                if (value)
                    for (std::size_t index = 0; index <= value->size(); ++index) {
                        if (index < value->size() && (static_cast<unsigned char>((*value)[index]) & 0xc0) == 0x80)
                            continue;
                        if (window.measure(std::string_view(*value).substr(0, index), 13).x >= window.mouseX - bounds.x
                            - 8) {
                            inputCaret_ = index;
                            break;
                        }
                    }
                SDL_StartTextInput(window.window);
                const SDL_Rect area{
                    static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.w),
                    static_cast<int>(bounds.h)
                };
                SDL_SetTextInputArea(window.window, &area, static_cast<int>(bounds.w));
            },
            {}, false
        });
    }

    void Application::insertInput(const std::string_view text) {
        auto *value = inputValue();
        if (!value) return;
        std::string insertion(text);
        insertion.erase(std::remove_if(insertion.begin(), insertion.end(), [](const char character) {
            return character == '\r' || character == '\n';
        }), insertion.end());
        const auto length = inputSelectedAll_ ? 0 : value->size();
        if (length + insertion.size() > 4096) return;
        if (inputSelectedAll_) {
            value->clear();
            inputSelectedAll_ = false;
            inputCaret_ = 0;
        }
        inputCaret_ = std::min(inputCaret_, value->size());
        value->insert(inputCaret_, insertion);
        inputCaret_ += insertion.size();
        inputPreedit_.clear();
        if (inputField_ == 2) view("playlist").scroll = 0;
    }

    void Application::renderInputText(UiWindow &window, const SDL_FRect bounds, const std::string &value,
                                      const int field, const std::string_view placeholder, const bool light) {
        const bool focused = inputField_ == field;
        const auto foreground = light ? textColor : SDL_Color{235, 244, 250, 255};
        const int size = light ? 13 : 14;
        const auto caret = focused ? std::min(inputCaret_, value.size()) : value.size();
        const auto prefix = window.measure(std::string_view(value).substr(0, caret), size);
        const auto composition = focused ? window.measure(inputPreedit_, size) : SDL_FPoint{};
        const float shift = focused ? std::max(0.0F, prefix.x + composition.x - bounds.w + 22) : 0;
        const float left = bounds.x + 8 - shift, top = bounds.y + (bounds.h - window.measure("Ag", size).y) / 2;
        SDL_Rect previous{};
        const bool clipped = SDL_RenderClipEnabled(window.renderer);
        SDL_GetRenderClipRect(window.renderer, &previous);
        const SDL_Rect clip{
            static_cast<int>(bounds.x + 6), static_cast<int>(bounds.y + 2), static_cast<int>(bounds.w - 12),
            static_cast<int>(bounds.h - 4)
        };
        SDL_SetRenderClipRect(window.renderer, &clip);
        if (focused && inputSelectedAll_) window.fill({left, bounds.y + 4, window.measure(value, size).x, bounds.h - 8},
                                                      light
                                                          ? SDL_Color{190, 220, 255, 255}
                                                          : SDL_Color{37, 88, 120, 255});
        if (value.empty() && (!focused || inputPreedit_.empty())) window.text(
            placeholder, left, top, size, light ? secondary : SDL_Color{132, 159, 180, 255});
        else {
            window.text(std::string_view(value).substr(0, caret), left, top, size, foreground);
            window.text(std::string_view(value).substr(caret), left + prefix.x + composition.x, top, size, foreground);
        }
        if (focused) {
            if (!inputPreedit_.empty()) {
                window.text(inputPreedit_, left + prefix.x, top, size, foreground);
                window.fill({left + prefix.x, bounds.y + bounds.h - 5, composition.x, 1},
                            light ? blue : SDL_Color{74, 219, 184, 255});
            }
            if ((SDL_GetTicks() / 500) % 2 == 0) window.fill({
                                                                 left + prefix.x + composition.x, bounds.y + 6, 1,
                                                                 bounds.h - 12
                                                             }, foreground);
            const SDL_Rect area{
                static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.w),
                static_cast<int>(bounds.h)
            };
            SDL_SetTextInputArea(window.window, &area, static_cast<int>(left + prefix.x + composition.x - bounds.x));
        }
        SDL_SetRenderClipRect(window.renderer, clipped ? &previous : nullptr);
    }

    void Application::renderImportProgress(UiWindow &window) {
        const float width = static_cast<float>(window.width), height = static_cast<float>(window.height);
        window.fill({0, 0, width, height}, {240, 242, 245, 255});
        const auto progress = importer_.progress();
        const int total = progress.total, loaded = progress.loaded;
        const auto &filename = progress.filename;
        const auto count = std::to_string(loaded) + "/" + std::to_string(std::max(0, total));
        window.text(total < 0 ? message("dialog.scanningDir") : message("dialog.loadingPrefix") + filename, 12, 14, 13,
                    textColor, width - 24, true);
        const SDL_FRect track{12, 47, width - 24, 24};
        window.fill(track, {230, 230, 230, 255});
        window.outline(track, border);
        const float ratio = total > 0 ? std::clamp(static_cast<float>(loaded) / total, 0.0F, 1.0F) : 0;
        if (total < 0) window.fill({
                                       track.x + 1 + static_cast<float>(SDL_GetTicks() % 1500) / 1500 * (track.w - 82),
                                       track.y + 1, 80, track.h - 2
                                   }, blue);
        else window.fill({track.x + 1, track.y + 1, (track.w - 2) * ratio, track.h - 2}, {145, 202, 250, 255});
        if (total >= 0) window.text(count, track.x, track.y + 3, 13, textColor, track.w, true);
        panelButton(window, {(width - 90) / 2, height - 43, 90, 30}, message("dialog.cancel"),
                    [this] { importer_.cancel(); });
    }

    void Application::renderPanel(UiWindow &window) {
        const float width = static_cast<float>(window.width), height = static_cast<float>(window.height);
        window.fill({0, 0, width, height}, {240, 242, 245, 255});
        auto close = [this] {
            view("dialog").show(false);
            inputField_ = 0;
            SDL_StopTextInput(view("dialog").window);
        };
        auto check = [&](const SDL_FRect bounds, const std::string &label, const bool checked,
                         std::function<void()> toggle) {
            window.fill({bounds.x, bounds.y + 3, 16, 16}, checked ? blue : SDL_Color{255, 255, 255, 255});
            window.outline({bounds.x, bounds.y + 3, 16, 16}, checked ? blue : border);
            if (checked) window.text("✓", bounds.x + 1, bounds.y, 13, {255, 255, 255, 255});
            window.text(label, bounds.x + 24, bounds.y + 1, 13, textColor, bounds.w - 24);
            window.hits.push_back({bounds, std::move(toggle), {}, false});
        };
        if (panelKind_ == "skin") {
            const int rows = std::max(1, (window.height - 78) / 24);
            window.scroll = std::clamp(window.scroll, 0, std::max(0, static_cast<int>(skins_.size()) - rows));
            window.fill({10, 10, 180, height - 68}, {255, 255, 255, 255});
            window.outline({10, 10, 180, height - 68}, border);
            for (int row = 0; row < rows && row + window.scroll < static_cast<int>(skins_.size()); ++row) {
                const int index = row + window.scroll;
                const SDL_FRect bounds{11, 11.0F + row * 24, 166, 24};
                if (index == previewIndex_) window.fill(bounds, {230, 244, 255, 255});
                window.text(utf8(skins_[index].stem()), bounds.x + 3, bounds.y + 4, 13,
                            index == previewIndex_ ? blue : textColor, bounds.w - 6);
                window.hits.push_back({
                    bounds, [this, &window, index] {
                        window.clearCache();
                        previewSkin_.reset();
                        previewIndex_ = index;
                        try {
                            previewSkin_ = std::make_unique<Skin>();
                            previewSkin_->load(skins_[index], assets_ / "ico/TTPlayer.ico");
                        } catch (const std::exception &error) { status_ = error.what(); }
                        if (clickCount_ >= 2 && previewSkin_) {
                            applySkin(skins_[index]);
                            window.show(false);
                        }
                    },
                    {}, false
                });
            }
            const int maximum = std::max(0, static_cast<int>(skins_.size()) - rows);
            if (maximum > 0) window.slider({179, 12, 8, height - 72}, 1 - static_cast<float>(window.scroll) / maximum,
                                           [&window, maximum](const float value) {
                                               window.scroll = static_cast<int>((1 - value) * maximum);
                                           }, true);
            const SDL_FRect preview{200, 10, width - 210, height - 130};
            window.fill(preview, {30, 30, 30, 255});
            if (previewSkin_) {
                const auto *definition = previewSkin_->window("player_window");
                if (definition) {
                    const float scale = std::min(preview.w / definition->width, preview.h / definition->height);
                    window.bitmap(previewSkin_->image(definition->image), {
                                      preview.x + (preview.w - definition->width * scale) / 2,
                                      preview.y + (preview.h - definition->height * scale) / 2,
                                      definition->width * scale, definition->height * scale
                                  });
                }
                window.text(previewSkin_->name(), 200, height - 111, 14, textColor, width - 210);
                window.text(message("skinSelect.authorPrefix") + previewSkin_->author(), 200, height - 85, 12,
                            secondary, width - 210);
            } else window.text(message("skinSelect.noPreview"), preview.x, preview.y + preview.h / 2, 14, secondary,
                               preview.w, true);
            panelButton(window, {width / 2 - 110, height - 43, 100, 30}, message("skinSelect.switch"), [this, close] {
                if (previewIndex_ >= 0) {
                    applySkin(skins_.at(previewIndex_));
                    close();
                }
            }, true);
            panelButton(window, {width / 2 + 10, height - 43, 100, 30}, message("dialog.cancel"), close);
        } else if (panelKind_ == "lyrics") {
            window.fill({0, 0, width, height - 55}, {255, 255, 255, 255});
            window.text(message("lyricSearch.singerLabel"), 20, 21, 13, textColor, 42);
            panelInput(window, {62, 12, 137, 32}, 11);
            window.text(message("lyricSearch.songLabel"), 212, 21, 13, textColor, 42);
            panelInput(window, {254, 12, width - 378, 32}, 12);
            panelButton(window, {width - 104, 12, 84, 32}, message("lyricSearch.search"), [this] { submitPanel(); },
                        true);
            const float tableHeight = height - 208;
            window.fill({20, 60, width - 40, 32}, {250, 250, 250, 255});
            window.text(message("lyricSearch.singerColumn"), 32, 68, 13, secondary, 160);
            window.text(message("lyricSearch.songColumn"), 210, 68, 13, secondary, width - 250);
            window.outline({20, 60, width - 40, tableHeight + 32}, border);
            const int rows = std::max(1, static_cast<int>(tableHeight / 36));
            window.scroll = std::clamp(window.scroll, 0, std::max(0, static_cast<int>(searchResults_.size()) - rows));
            for (int row = 0; row < rows && row + window.scroll < static_cast<int>(searchResults_.size()); ++row) {
                const int index = row + window.scroll;
                const SDL_FRect bounds{21, 92.0F + row * 36, width - 42, 36};
                if (index == resultIndex_) window.fill(bounds, {230, 244, 255, 255});
                else if (contains(bounds, window.mouseX, window.mouseY)) window.fill(bounds, {250, 250, 250, 255});
                const auto &result = searchResults_[index];
                window.text(result.artist, 32, bounds.y + 9, 13, textColor, 160);
                window.text(result.title, 210, bounds.y + 9, 13, textColor, width - 250);
                window.hits.push_back({
                    bounds, [this, index] {
                        resultIndex_ = index;
                        if (clickCount_ >= 2) fetchLyric(index);
                    },
                    {}, false
                });
            }
            window.text(status_, 20, height - 142, 12, secondary, width - 40);
            window.text(message("lyricSearch.saveAs"), 20, height - 107, 13, textColor, 60);
            panelInput(window, {80, height - 115, width - 148, 32}, 13);
            panelButton(window, {width - 58, height - 115, 38, 32}, "…", [this] {
                showDialog(FileDialogKind::ChooseLyricDestination);
            });
            check({20, height - 75, width - 40, 24}, message("lyricSearch.saveToSongDir"), associateLyrics_,
                  [this] { associateLyrics_ = !associateLyrics_; });
            panelButton(window, {width - 220, height - 43, 90, 32}, message("lyricSearch.download"),
                        [this] { fetchLyric(resultIndex_); }, true);
            panelButton(window, {width - 110, height - 43, 90, 32}, message("lyricSearch.close"), close);
        } else if (panelKind_ == "options") {
            check({24, 30, width - 48, 26}, message("dialog.miniOnStart"), settings_.miniOnStart, [this] {
                settings_.miniOnStart = !settings_.miniOnStart;
                dirty_ = true;
            });
            check({24, 72, width - 48, 26}, message("dialog.rememberPos"), settings_.rememberPosition, [this] {
                settings_.rememberPosition = !settings_.rememberPosition;
                dirty_ = true;
            });
            check({24, 114, width - 48, 26}, message("menu.pauseAfterPlay"), settings_.pauseAfterPlay, [this] {
                settings_.pauseAfterPlay = !settings_.pauseAfterPlay;
                dirty_ = true;
            });
            check({24, 156, width - 48, 26}, tr("自动歌词（首项匹配，仅写缓存）", "Automatic lyrics (first match, cache only)"),
                  settings_.autoLyrics, [this] {
                      settings_.autoLyrics = !settings_.autoLyrics;
                      dirty_ = true;
                      if (settings_.autoLyrics) startAutomaticLyrics();
                  });
            panelButton(window, {width - 114, height - 44, 90, 30}, tr("确定", "OK"), close, true);
        } else if (panelKind_ == "format") {
            const char *templates[]{
                "%artist% - %title%", "%track% - %title%", "%artist% - %album% - %title%", "%title%", "%filename%"
            };
            for (int index = 0; index < 5; ++index) check({24, 18.0F + index * 34, width - 48, 26}, templates[index],
                                                          settings_.titleFormat == templates[index],
                                                          [this, value = std::string(templates[index])] {
                                                              settings_.titleFormat = value;
                                                              dirty_ = true;
                                                          });
            panelButton(window, {width - 114, height - 44, 90, 30}, tr("确定", "OK"), close, true);
        } else if (panelKind_ == "input") {
            panelInput(window, {24, 30, width - 48, 34}, 10);
            panelButton(window, {width - 220, height - 46, 90, 32}, tr("确定", "OK"), [this] { submitPanel(); }, true);
            panelButton(window, {width - 114, height - 46, 90, 32}, message("dialog.cancel"), close);
        } else {
            std::istringstream stream(panelValue_);
            std::string line;
            std::vector<std::string> lines;
            while (std::getline(stream, line)) lines.push_back(line);
            const int rows = std::max(1, (window.height - 84) / 23);
            window.scroll = std::clamp(window.scroll, 0, std::max(0, static_cast<int>(lines.size()) - rows));
            for (int row = 0; row < rows && row + window.scroll < static_cast<int>(lines.size()); ++row) window.text(
                lines[row + window.scroll], 24, 20.0F + row * 23, 13, textColor, width - 48);
            panelButton(window, {width - 114, height - 44, 90, 30}, tr("确定", "OK"), close, true);
        }
    }
}
