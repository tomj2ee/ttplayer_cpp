#include "app/Application.hpp"

#include <algorithm>
#include <cmath>

namespace ttplayer::app {
    namespace {
        constexpr SDL_Color muted{132, 159, 180, 255};
        constexpr SDL_Color accent{74, 219, 184, 255};
        std::string definitionName(const std::string &name) { return (name == "eq" ? "equalizer" : name) + "_window"; }
    }

    void Application::render(UiWindow &window) {
        const auto *definition = skin_ && window.name != "desktop" && window.name != "settings" && window.name !=
                                 "dialog" && window.name != "progress"
                                     ? skin_->window(definitionName(window.name))
                                     : nullptr;
        if (definition) {
            int width = 0, height = 0;
            SDL_GetWindowSize(window.window, &width, &height);
            window.begin(width, height, true);
            renderSkin(window, *definition);
        } else {
            window.begin(0, 0, window.name == "desktop");
            if (window.name == "dialog") renderPanel(window);
            else if (window.name == "progress") renderImportProgress(window);
            else if (window.name == "player" || window.name == "mini") renderPlayer(window);
            else if (window.name == "eq") renderEqualizer(window);
            else if (window.name == "lyric") {
                window.text(tr("同步歌词", "SYNCHRONIZED LYRICS"), 20, 14, 18, accent);
                window.button({static_cast<float>(window.width - 96), 10, 76, 30}, tr("找歌词", "Search"),
                              [this] { openPanel("lyrics"); });
                renderLyrics(window, {
                                 16, 56, static_cast<float>(window.width - 32), static_cast<float>(window.height - 90)
                             });
                window.text(tr("点击歌词跳转 · +/- 调整歌词偏移 · F6 桌面歌词", "Click to seek · +/- lyric offset · F6 desktop lyrics"),
                            16, static_cast<float>(window.height - 26), 12, muted,
                            static_cast<float>(window.width - 32));
            } else if (window.name == "desktop") {
                window.roundedRect({4, 4, static_cast<float>(window.width - 8), static_cast<float>(window.height - 8)},
                                   {0, 0, 0, 76}, 8);
                renderLyrics(window, {12, 0, static_cast<float>(window.width - 24), static_cast<float>(window.height)},
                             true);
            } else if (window.name == "playlist") {
                window.text(playlist().name + " · " + std::to_string(playlist().songs.size()), 18, 12, 18, accent,
                            static_cast<float>(window.width - 340));
                drawInput(window, {static_cast<float>(window.width - 305), 10, 285, 30},
                          tr("搜索歌曲 / Ctrl+F", "Filter songs / Ctrl+F"), 2);
                const char *actions[]{"open", "folder", "delete", "import", "export", "settings"};
                const char *chinese[]{"添加歌曲", "添加目录", "移除选中", "导入列表", "导出列表", "列表管理"};
                const char *english[]{"Add files", "Add folder", "Remove", "Import", "Export", "Manage"};
                for (int index = 0; index < 6; ++index) window.button({18.0F + index * 112, 52, 104, 30},
                                                                      tr(chinese[index], english[index]),
                                                                      [this, action = std::string(actions[index])] {
                                                                          if (action == "settings") {
                                                                              page_ = 0;
                                                                              command("tools");
                                                                          } else command(action);
                                                                      });
                window.button({690, 52, 130, 30}, tr("按标题排序", "Sort by title"), [this] { command("sort_title"); });
                renderPlaylist(window, {
                                   18, 96, static_cast<float>(window.width - 36),
                                   static_cast<float>(window.height - 132)
                               });
                window.text(
                    tr("双击播放 · Enter 播放 · Delete 移除 · 拖放音频/目录/播放列表",
                       "Double click / Enter to play · Delete removes · Drop files/folders/playlists"), 18,
                    static_cast<float>(window.height - 26), 12, muted, static_cast<float>(window.width - 36));
            } else if (window.name == "settings") renderSettings(window);
        }
        if (contextWindow_ == window.name) renderContext(window);
        else window.renderTooltip();
        if (smoke_ &&smokeStage_ > 0)
        {
            const auto path = smokeOutput_ / (std::to_string(smokeStage_) + "-" + window.name + ".bmp");
            if (!std::filesystem::exists(path)) {
                if (auto *pixels = SDL_RenderReadPixels(window.renderer, nullptr)) {
                    SDL_SaveBMP(pixels, utf8(path).c_str());
                    SDL_DestroySurface(pixels);
                }
            }
        }
        SDL_RenderPresent(window.renderer);
    }

    void Application::renderContext(UiWindow &window) {
        if (contextKind_ == 4 && inputField_ == 2) {
            contextBounds_.w = std::min(240.0F, static_cast<float>(window.width - 8));
            contextBounds_.h = 46;
            contextBounds_.x = std::clamp(contextBounds_.x, 4.0F, std::max(4.0F, window.width - contextBounds_.w - 4));
            contextBounds_.y = std::clamp(contextBounds_.y, 4.0F, std::max(4.0F, window.height - contextBounds_.h - 4));
            window.fill(contextBounds_, {20, 34, 50, 255});
            window.outline(contextBounds_, {83, 139, 155, 255});
            drawInput(window, {contextBounds_.x + 6, contextBounds_.y + 6, contextBounds_.w - 12, 34},
                      tr("输入过滤文字 / Esc 关闭", "Filter tracks / Esc closes"), 2);
            return;
        }
    }

    void Application::renderPlayer(UiWindow &window) {
        const float width = static_cast<float>(window.width);
        const bool mini = window.name == "mini";
        const auto state = engine_.state();
        const std::string states[]{
            tr("已停止", "Stopped"), tr("正在加载", "Loading"), tr("播放中", "Playing"), tr("已暂停", "Paused"), tr("播放失败", "Error")
        };
        if (mini) {
            window.text(nowSong_.title.empty() ? "TTPlayer C++" : nowSong_.title, 14, 6, 15, accent, width - 28);
            window.button({14, 34, 56, 28}, "|<", [this] { advance(-1); });
            window.button({76, 34, 88, 28},
                          state == media::PlaybackState::playing ? tr("暂停", "Pause") : tr("播放", "Play"),
                          [this] { playPause(); });
            window.button({170, 34, 56, 28}, ">|", [this] { advance(1); });
            window.slider({240, 42, std::max(60.0F, width - 350), 12}, sliderValue("progress"),
                          [this](const float ratio) { changeSlider("progress", ratio); });
            window.button({width - 96, 34, 82, 28}, tr("完整模式", "Full"), [this] { command("minimode"); });
            window.text(timeText(engine_.positionMs()) + " / " + timeText(engine_.durationMs()), 14, 70, 12, muted);
            return;
        }
        window.fill({0, 0, width, 54}, {18, 30, 44, 255});
        window.text("TTPLAYER", 20, 10, 23, accent);
        window.text("C++ / FFmpeg / SDL3", 184, 20, 12, muted);
        const char *commands[]{"playlist", "equalizer", "lyric", "settings"};
        const char *chinese[]{"列表", "音效", "歌词", "设置"};
        const char *english[]{"List", "EQ", "Lyrics", "Tools"};
        for (int index = 0; index < 4; ++index) window.button({width - 288 + index * 68, 12, 60, 30},
                                                              tr(chinese[index], english[index]),
                                                              [this, name = std::string(commands[index])] {
                                                                  command(name);
                                                              });
        window.text(nowSong_.title.empty() ? tr("打开音乐，开始聆听", "Open your music") : nowSong_.title, 22, 68, 22,
                    {235, 243, 252, 255}, width * 0.58F);
        const auto subtitle = nowSong_.artist.empty()
                                  ? tr("支持 MP3 / FLAC / OGG / AAC / APE / WAV", "MP3 / FLAC / OGG / AAC / APE / WAV")
                                  : nowSong_.artist + (nowSong_.album.empty() ? "" : " · " + nowSong_.album);
        window.text(subtitle, 24, 104, 13, muted, width * 0.55F);
        window.text(
            states[static_cast<std::size_t>(state)] + "  " + timeText(engine_.positionMs()) + " / " +
            timeText(engine_.durationMs()), 24, 137, 17, accent);
        const SDL_FRect spectrum{width * 0.62F, 68, width * 0.35F, 100};
        window.visual(spectrum, spectrum_, settings_.visual, waveform_);
        window.hits.push_back({spectrum, [this] { command("visual"); }, {}, false});
        const float progressY = static_cast<float>(window.height - 133);
        window.slider({24, progressY, width - 48, 10}, sliderValue("progress"), [this](const float ratio) {
            changeSlider("progress", ratio);
        });
        const float buttonY = progressY + 30;
        window.button({24, buttonY, 76, 38}, tr("打开", "Open"), [this] { command("open"); });
        window.button({110, buttonY, 54, 38}, "|<", [this] { advance(-1); });
        window.button({174, buttonY, 100, 38},
                      state == media::PlaybackState::playing ? tr("暂停", "Pause") : tr("播放", "Play"),
                      [this] { playPause(); }, state == media::PlaybackState::playing);
        window.button({284, buttonY, 54, 38}, ">|", [this] { advance(1); });
        window.button({348, buttonY, 64, 38}, tr("停止", "Stop"), [this] { command("stop"); });
        const char *modeZh[]{"单曲", "单曲循环", "顺序播放", "列表循环", "随机播放"};
        const char *modeEn[]{"Single", "Repeat one", "In order", "Repeat all", "Shuffle"};
        window.button({424, buttonY, 106, 38}, tr(modeZh[settings_.mode], modeEn[settings_.mode]),
                      [this] { command("mode"); });
        window.button({width - 274, buttonY, 56, 38}, engine_.volume() == 0 ? tr("恢复", "Unmute") : tr("音量", "Vol"),
                      [this] { command("mute"); });
        window.slider({width - 205, buttonY + 14, 125, 10}, engine_.volume(), [this](const float ratio) {
            changeSlider("volume", ratio);
        });
        window.text(std::to_string(static_cast<int>(engine_.volume() * 100)) + "%", width - 66, buttonY + 8, 13, muted);
        window.text(
            state == media::PlaybackState::error
                ? engine_.errorMessage()
                : status_.empty()
                      ? tr("空格暂停 · 方向键快进/音量 · 右键工具 · F1 帮助",
                           "Space pause · Arrows seek/volume · Right click tools · F1 help")
                      : status_, 24, static_cast<float>(window.height - 30), 12,
            state == media::PlaybackState::error ? SDL_Color{245, 143, 124, 255} : muted, width - 48);
    }

    void Application::renderPlaylist(UiWindow &window, const SDL_FRect bounds, const bool skinned) {
        if (skinned) {
            renderClassicPlaylist(window, bounds);
            return;
        }
        const float rowHeight = skinned ? 19 : 32;
        const int rows = std::max(1, static_cast<int>(bounds.h / rowHeight));
        std::vector<int> filtered;
        const auto needle = lower(filter_);
        const auto &songs = playlist().songs;
        for (std::size_t index = 0; index < songs.size(); ++index) {
            const auto &song = songs[index];
            if (needle.empty() || lower(song.title + " " + song.artist + " " + utf8(song.path.filename())).find(needle)
                != std::string::npos) filtered.push_back(static_cast<int>(index));
        }
        const int maximumScroll = std::max(0, static_cast<int>(filtered.size()) - rows);
        window.scroll = std::clamp(window.scroll, 0, maximumScroll);
        window.fill(bounds, skinned ? SDL_Color{246, 248, 250, 255} : SDL_Color{15, 26, 39, 255});
        if (filtered.empty()) window.text(tr("没有歌曲，打开文件或拖入目录", "No tracks. Open audio or drop a folder."),
                                          bounds.x + 10, bounds.y + 12, skinned ? 11 : 15,
                                          skinned ? SDL_Color{64, 83, 100, 255} : muted, bounds.w - 20);
        for (int row = 0; row < rows && window.scroll + row < static_cast<int>(filtered.size()); ++row) {
            const int index = filtered[static_cast<std::size_t>(window.scroll + row)];
            const auto &song = songs[static_cast<std::size_t>(index)];
            const bool playing = !nowSong_.path.empty() && nowSong_.path == song.path;
            const SDL_FRect rect{bounds.x, bounds.y + row * rowHeight, bounds.w - 14, rowHeight};
            if (index == selected_) window.fill(
                rect, skinned ? SDL_Color{194, 224, 244, 255} : SDL_Color{29, 68, 83, 255});
            else if (row % 2) window.fill(rect, skinned ? SDL_Color{237, 242, 247, 255} : SDL_Color{18, 30, 44, 255});
            const int size = skinned ? 11 : 14;
            const auto textColor = skinned
                                       ? playing
                                             ? SDL_Color{17, 135, 120, 255}
                                             : SDL_Color{51, 67, 84, 255}
                                       : playing
                                             ? accent
                                             : SDL_Color{214, 228, 240, 255};
            const float titleWidth = skinned ? bounds.w - 68 : bounds.w * 0.56F;
            window.text((playing ? "> " : "") + std::to_string(index + 1) + ". " + song.title, rect.x + 5,
                        rect.y + (skinned ? 0 : 5), size, textColor, titleWidth);
            if (!skinned) window.text(song.artist, bounds.x + bounds.w * 0.6F, rect.y + 5, size, muted,
                                      bounds.w * 0.24F);
            if (song.duration > 0) window.text(timeText(song.duration), bounds.x + bounds.w - 61,
                                               rect.y + (skinned ? 0 : 5), size,
                                               skinned ? SDL_Color{90, 110, 125, 255} : muted, 48);
            window.hits.push_back({
                rect, [this, index] {
                    selected_ = selectionAnchor_ = index;
                    selectedSongs_ = {index};
                    if (clickCount_ >= 2) playSong(settings_.playlist, index);
                },
                {}, false
            });
        }
        if (maximumScroll > 0) window.slider({bounds.x + bounds.w - 10, bounds.y, 8, bounds.h},
                                             1 - static_cast<float>(window.scroll) / static_cast<float>(maximumScroll),
                                             [&window, maximumScroll](const float ratio) {
                                                 window.scroll = static_cast<int>((1 - ratio) * maximumScroll);
                                             }, true);
    }

    void Application::renderEqualizer(UiWindow &window) {
        window.text(tr("10 段图形均衡器", "10-BAND EQUALIZER"), 20, 12, 20, accent);
        window.button({static_cast<float>(window.width - 290), 10, 80, 32},
                      settings_.equalizer.enabled ? tr("已启用", "Enabled") : tr("已关闭", "Bypass"),
                      [this] { command("enabled"); }, settings_.equalizer.enabled);
        window.button({static_cast<float>(window.width - 200), 10, 80, 32}, tr("预设", "Preset"),
                      [this] { command("profile"); });
        window.button({static_cast<float>(window.width - 110), 10, 90, 32}, tr("重置", "Reset"),
                      [this] { command("reset"); });
        window.text("+12 dB", 18, 66, 11, muted);
        window.text("0 dB", 28, 128, 11, muted);
        window.text("-12 dB", 18, 190, 11, muted);
        const float step = static_cast<float>(window.width - 100) / 11;
        constexpr const char *labels[]{"Pre", "60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"};
        for (int index = 0; index < 11; ++index) {
            const auto name = index == 0 ? std::string("preamp") : "band" + std::to_string(index - 1);
            const float left = 76 + index * step;
            window.slider({left, 66, 15, 142}, sliderValue(name),
                          [this, name](const float ratio) { changeSlider(name, ratio); }, true);
            window.text(labels[index], left - 10, 213, 11, muted, step, true);
            const float gain = index == 0
                                   ? settings_.equalizer.preamp
                                   : settings_.equalizer.bands[static_cast<std::size_t>(index - 1)];
            window.text(std::to_string(static_cast<int>(std::round(gain))), left - 2, 44, 11, accent);
        }
        const float bottom = static_cast<float>(window.height - 56);
        window.text(tr("声道平衡", "Balance"), 20, bottom - 2, 13, muted);
        window.slider({108, bottom + 5, 150, 10}, sliderValue("balance"), [this](const float ratio) {
            changeSlider("balance", ratio);
        });
        window.text(tr("立体声扩展", "Stereo width"), 290, bottom - 2, 13, muted);
        window.slider({410, bottom + 5, 150, 10}, sliderValue("surround"), [this](const float ratio) {
            changeSlider("surround", ratio);
        });
        window.text(
            tr("60 / 170 / 310 / 600 / 1K / 3K / 6K / 12K / 14K / 16K Hz · ±12 dB",
               "Peaking biquads · 48 kHz stereo · ±12 dB · Output safety limiter"), 20,
            static_cast<float>(window.height - 24), 11, muted, static_cast<float>(window.width - 40));
    }

    void Application::renderLyrics(UiWindow &window, const SDL_FRect bounds, const bool desktop) {
        if (!desktop && skin_ && window.name == "lyric" && skin_->window("lyric_window")) {
            renderClassicLyrics(window, bounds);
            return;
        }
        if (desktop) {
            const auto position = engine_.positionMs();
            const int current = lyrics_.active(position);
            const auto metrics = window.measure("歌词", 24);
            const float top = bounds.y + bounds.h * (settings_.desktopDualLine ? 1.0F / 3 : 0.5F) - metrics.y / 2;
            if (current < 0 || lyrics_.lines.empty()) {
                window.text(message("lyric.none"), bounds.x, bounds.y + (bounds.h - metrics.y) / 2, 24,
                            {255, 255, 255, 200}, bounds.w, true);
            } else {
                const auto &line = lyrics_.lines[static_cast<std::size_t>(current)];
                if (!settings_.desktopDualLine && karaoke_ && !line.words.empty()) window.karaoke(
                    line.text, {bounds.x, top, bounds.w, metrics.y},
                    lyrics_.progress(current, position, engine_.durationMs()), 24, {255, 255, 255, 200},
                    {0, 255, 128, 255});
                else window.text(line.text, bounds.x, top, 24, {0, 255, 128, 255}, bounds.w, true);
                if (settings_.desktopDualLine && current + 1 < static_cast<int>(lyrics_.lines.size())) window.text(
                    lyrics_.lines[static_cast<std::size_t>(current + 1)].text, bounds.x,
                    bounds.y + bounds.h * 2 / 3 - metrics.y / 2, 24, {255, 255, 255, 120}, bounds.w, true);
            }
            return;
        }
        if (lyrics_.lines.empty()) {
            window.text(nowSong_.title.empty() ? "TTPlayer" : nowSong_.title, bounds.x, bounds.y + bounds.h * 0.3F,
                        desktop ? 30 : 20, accent, bounds.w, true);
            window.text(tr("暂无歌词 · F3 在线搜索 / 拖入 LRC", "No lyrics · F3 search / Drop an LRC file"), bounds.x,
                        bounds.y + bounds.h * 0.65F, desktop ? 16 : 13, muted, bounds.w, true);
            return;
        }
        const auto position = engine_.positionMs();
        const int current = lyrics_.active(position);
        const bool small = bounds.h < 160;
        const float rowHeight = small ? 22 : 38;
        const int radius = std::max(1, static_cast<int>(bounds.h / rowHeight) / 2);
        const float center = bounds.y + bounds.h / 2;
        SDL_Rect previous{};
        const bool clipped = SDL_RenderClipEnabled(window.renderer);
        SDL_GetRenderClipRect(window.renderer, &previous);
        const SDL_Rect clip{
            static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.w),
            static_cast<int>(bounds.h)
        };
        SDL_SetRenderClipRect(window.renderer, &clip);
        for (int delta = -radius; delta <= radius; ++delta) {
            const int index = std::max(0, current) + delta;
            if (index < 0 || index >= static_cast<int>(lyrics_.lines.size())) continue;
            const auto &line = lyrics_.lines[static_cast<std::size_t>(index)];
            const SDL_FRect row{bounds.x, center + delta * rowHeight - rowHeight / 2, bounds.w, rowHeight};
            if (delta == 0) window.karaoke(line.text, row,
                                           current < 0 ? 0 : lyrics_.progress(index, position, engine_.durationMs()),
                                           small ? 13 : 23);
            else window.text(line.text, row.x, row.y + 3, small ? 11 : 16, muted, row.w, true);
            if (row.y >= bounds.y && row.y + row.h <= bounds.y + bounds.h) window.hits.push_back({
                row, [this, time = line.time] { engine_.seek(std::max<std::int64_t>(0, time - lyrics_.offset)); }, {},
                false
            });
        }
        SDL_SetRenderClipRect(window.renderer, clipped ? &previous : nullptr);
    }

    void Application::drawInput(UiWindow &window, const SDL_FRect bounds, const std::string_view placeholder,
                                const int field) {
        const auto &value = field == 1 ? query_ : filter_;
        window.fill(bounds, {17, 28, 42, 255});
        window.outline(bounds, inputField_ == field ? accent : SDL_Color{61, 83, 102, 255});
        renderInputText(window, bounds, value, field, placeholder, false);
        window.hits.push_back({
            bounds, [this, &window, field] {
                inputField_ = field;
                inputCaret_ = std::string::npos;
                inputPreedit_.clear();
                inputSelectedAll_ = false;
                SDL_StartTextInput(window.window);
            },
            {}, false
        });
    }

    void Application::renderSettings(UiWindow &window) {
        const float width = static_cast<float>(window.width);
        const char *zh[]{"音乐库", "皮肤", "在线歌词", "偏好设置", "帮助 / 关于"};
        const char *en[]{"Library", "Skins", "Online lyrics", "Preferences", "Help / About"};
        window.text(tr("设置与工具", "SETTINGS & TOOLS"), 22, 16, 22, accent);
        for (int index = 0; index < 5; ++index) window.button({22.0F + index * 160, 58, 150, 34},
                                                              tr(zh[index], en[index]), [this, &window, index] {
                                                                  page_ = index;
                                                                  window.scroll = 0;
                                                                  inputField_ = 0;
                                                                  SDL_StopTextInput(window.window);
                                                              }, page_ == index);
        if (page_ == 0) {
            window.button({22, 114, 120, 34}, tr("打开歌曲", "Open audio"), [this] { command("open"); });
            window.button({152, 114, 120, 34}, tr("扫描目录", "Scan folder"), [this] { command("folder"); });
            window.button({282, 114, 120, 34}, tr("导入 XML/M3U", "Import list"), [this] { command("import"); });
            window.button({412, 114, 120, 34}, tr("导出 M3U8", "Export M3U8"), [this] { command("export"); });
            if (importer_.busy()) window.button({542, 114, 120, 34}, tr("取消导入", "Cancel import"),
                                                [this] { importer_.cancel(); });
            drawInput(window, {22, 170, 350, 34}, tr("输入新建或重命名的列表名称", "New / renamed playlist name"), 1);
            window.button({384, 170, 100, 34}, tr("新建", "New"), [this] {
                if (query_.empty()) return;
                settings_.lists.push_back({query_, {}});
                settings_.playlist = static_cast<int>(settings_.lists.size()) - 1;
                settings_.song = -1;
                selected_ = selectionAnchor_ = -1;
                selectedSongs_.clear();
                view("playlist").scroll = 0;
                dirty_ = true;
            });
            window.button({494, 170, 100, 34}, tr("重命名", "Rename"), [this] {
                if (!query_.empty()) {
                    playlist().name = query_;
                    dirty_ = true;
                }
            });
            window.button({604, 170, 100, 34}, tr("删除列表", "Delete list"), [this] {
                if (importer_.busy()) {
                    status_ = tr("导入期间不能删除列表", "Wait for the import to finish");
                    return;
                }
                if (settings_.lists.size() <= 1) {
                    status_ = tr("至少保留一个列表", "Keep at least one playlist");
                    return;
                }
                const int removed = settings_.playlist;
                settings_.lists.erase(settings_.lists.begin() + removed);
                if (nowList_ > removed) --nowList_;
                else if (nowList_ == removed) {
                    autoAdvance_ = false;
                    nowList_ = 0;
                }
                settings_.playlist = std::min(removed, static_cast<int>(settings_.lists.size()) - 1);
                selected_ = selectionAnchor_ = -1;
                selectedSongs_.clear();
                settings_.song = -1;
                dirty_ = true;
            });
            window.text(tr("点击列表切换 · 播放中的歌曲不受浏览影响", "Select a playlist; browsing does not interrupt playback"), 22, 226,
                        13, muted);
            const int rows = std::max(1, (window.height - 326) / 38);
            window.scroll = std::clamp(window.scroll, 0, std::max(0, static_cast<int>(settings_.lists.size()) - rows));
            for (int row = 0; row < rows && row + window.scroll < static_cast<int>(settings_.lists.size()); ++row) {
                const int index = row + window.scroll;
                const auto &list = settings_.lists[static_cast<std::size_t>(index)];
                window.button({22, 252.0F + row * 38, width - 44, 32},
                              list.name + "  (" + std::to_string(list.songs.size()) + ")", [this, index] {
                                  settings_.playlist = index;
                                  selected_ = selectionAnchor_ = -1;
                                  selectedSongs_.clear();
                                  settings_.song = -1;
                                  view("playlist").scroll = 0;
                                  dirty_ = true;
                              }, index == settings_.playlist);
            }
        } else if (page_ == 1) {
            window.button({22, 112, 160, 34}, tr("现代默认界面", "Modern default"), [this] { applySkin({}); }, !skin_);
            window.button({194, 112, 160, 34}, tr("打开 SKN 文件", "Open SKN file"),
                          [this] { showDialog(FileDialogKind::OpenSkin); });
            window.text(tr("直接读取原版 ZIP/BMP/XML 皮肤，无需解压 · 滚轮翻页",
                           "Original ZIP/BMP/XML skins · No extraction · Scroll to browse"), 22, 164, 13, muted,
                        width - 44);
            const int rows = std::max(1, (window.height - 275) / 36);
            window.scroll = std::clamp(window.scroll, 0, std::max(0, static_cast<int>(skins_.size()) - rows));
            for (int row = 0; row < rows && row + window.scroll < static_cast<int>(skins_.size()); ++row) {
                const auto path = skins_[static_cast<std::size_t>(row + window.scroll)];
                window.button({22, 198.0F + row * 36, width - 44, 31}, utf8(path.stem()), [this, path] {
                    try {
                        applySkin(path);
                        status_ = "Skin: " + skin_->name();
                    } catch (const std::exception &error) { status_ = error.what(); }
                }, skin_ && path == skin_->path());
            }
        } else if (page_ == 2) {
            drawInput(window, {22, 114, width - 300, 34},
                      tr("输入歌名 / 歌手，按 Enter 搜索", "Song / artist; press Enter to search"), 1);
            window.button({width - 266, 114, 110, 34}, tr("搜索歌词", "Search"), [this] { startSearch(); });
            window.button({width - 144, 114, 122, 34}, tr("打开本地 LRC", "Open LRC"),
                          [this] { showDialog(FileDialogKind::OpenLyrics); });
            window.text(tr("使用与 Java 版相同的歌词服务；搜索/下载异步执行并校验 HTTPS",
                           "Uses the Java version's lyric service; asynchronous HTTPS requests"), 22, 166, 13, muted,
                        width - 44);
            window.button({22, 194, 160, 30}, tr("导出当前歌词", "Export lyrics"),
                          [this] { showDialog(FileDialogKind::ExportLyrics); });
            window.button({194, 194, 160, 30}, tr("打开歌词窗口", "Lyric window"), [this] { command("lyric"); });
            window.button({366, 194, 160, 30}, tr("桌面歌词", "Desktop lyrics"), [this] { command("desktop"); });
            const int rows = std::max(1, (window.height - 325) / 44);
            window.scroll = std::clamp(window.scroll, 0, std::max(0, static_cast<int>(searchResults_.size()) - rows));
            for (int row = 0; row < rows && row + window.scroll < static_cast<int>(searchResults_.size()); ++row) {
                const int index = row + window.scroll;
                const auto &item = searchResults_[static_cast<std::size_t>(index)];
                window.button({22, 248.0F + row * 44, width - 44, 38},
                              item.title + " — " + item.artist + " · " + item.album,
                              [this, index] { fetchLyric(index); });
            }
            if (nowSong_.path.empty()) window.text(
                tr("请先选择播放歌曲，才能匹配并缓存歌词", "Play a track first to attach downloaded lyrics"), 22,
                static_cast<float>(window.height - 88), 13, accent, width - 44);
        } else if (page_ == 3) {
            window.button({22, 116, 235, 36}, message("menu.language") + ": " + settings_.language,
                          [this, &window] { openContext(window, 13, 22, 152); });
            window.button({274, 116, 235, 36}, tr("窗口置顶", "Always on top"), [this] { command("ontop"); },
                          settings_.topmost);
            window.button({526, 116, 235, 36}, tr("锁定桌面歌词（鼠标穿透）", "Lock desktop lyrics"), [this] { command("lock"); },
                          settings_.desktopLocked);
            window.text(tr("播放模式", "Playback mode"), 22, 184, 16, accent);
            const char *modeZh[]{"单曲停止", "单曲循环", "顺序播放", "列表循环", "随机播放"};
            const char *modeEn[]{"Single", "Repeat one", "In order", "Repeat all", "Shuffle"};
            for (int index = 0; index < 5; ++index) window.button({22.0F + index * 158, 220, 148, 34},
                                                                  tr(modeZh[index], modeEn[index]), [this, index] {
                                                                      settings_.mode = index;
                                                                      dirty_ = true;
                                                                  }, settings_.mode == index);
            window.text(tr("可视化效果（点击主界面频谱也可切换）", "Visualization (also click the player spectrum)"), 22, 278, 16,
                        accent);
            const char *visualZh[]{"频谱", "镜像", "折线", "面积", "LED", "波形", "星空"};
            const char *visualEn[]{"Spectrum", "Mirror", "Line", "Area", "LED", "Waveform", "Starfield"};
            for (int index = 0; index < 7; ++index) window.button(
                {22.0F + (index % 4) * 198, 316.0F + (index / 4) * 44, 186, 34}, tr(visualZh[index], visualEn[index]), [
                    this, index] {
                    settings_.visual = index;
                    dirty_ = true;
                }, settings_.visual == index);
            window.button({22, 424, 186, 34}, tr("完整播放器", "Full player"), [this] { command("show"); });
            window.button({220, 424, 186, 34}, tr("迷你播放器", "Mini player"), [this] { command("minimode"); });
            window.button({418, 424, 186, 34}, tr("保存设置", "Save settings"), [this] {
                try {
                    save();
                    status_ = tr("配置已保存", "Settings saved");
                } catch (const std::exception &error) { status_ = error.what(); }
            });
        } else {
            const std::vector<std::string> help{
                "TTPlayer C++ · FFmpeg 8 / SDL3 / SDL3_ttf · Native Windows player",
                tr("Ctrl+O 打开歌曲    Ctrl+Shift+O 添加目录    Ctrl+I 导入    Ctrl+E 导出",
                   "Ctrl+O open    Ctrl+Shift+O folder    Ctrl+I import    Ctrl+E export"),
                tr("Space 播放/暂停    S 停止    P/N 上一首/下一首    ←/→ 跳转 5 秒",
                   "Space play/pause    S stop    P/N previous/next    Left/Right seek 5s"),
                tr("↑/↓ 调节音量（列表内为选曲）    Enter 播放选中    Delete 移除歌曲",
                   "Up/Down volume (list: select)    Enter play selection    Delete remove"),
                tr("F2 皮肤    F3 在线歌词    F4 可视化    F5 歌词    F6 桌面歌词",
                   "F2 skins    F3 online lyrics    F4 visual    F5 lyrics    F6 desktop"),
                tr("F7 均衡器    F8 播放列表    F9 迷你模式    F10 设置    Ctrl+Q 退出",
                   "F7 equalizer    F8 playlist    F9 mini    F10 tools    Ctrl+Q quit"),
                tr("Ctrl+F 过滤歌曲    Ctrl+L 锁定/解锁桌面歌词    +/- 歌词偏移 100ms",
                   "Ctrl+F filter    Ctrl+L lock/unlock desktop    +/- lyric offset 100ms"),
                tr("支持拖放：音频文件、目录、M3U/M3U8、Java XML、SKN 和 LRC",
                   "Drop audio, folders, M3U/M3U8, Java XML, SKN and LRC files"),
                tr("原 Java 配置只读取；C++ 配置独立保存，退出时自动保存",
                   "Java configuration is read-only; C++ settings are saved separately"),
                tr("网络服务不可用时可导入本地 LRC；不上传音乐文件",
                   "If the lyric server is unavailable, use local LRC; audio is never uploaded"),
                tr("音乐窗按钮用于打开歌曲；移动同步仅有皮肤定义",
                   "Music browser opens audio files; mobile sync has skin definitions only")
            };
            for (std::size_t index = 0; index < help.size(); ++index) window.text(
                help[index], 22, 114 + static_cast<float>(index) * 31, index == 0 ? 16 : 13,
                index == 0 ? accent : muted, width - 44);
            window.text("Config: " + utf8(config_), 22, static_cast<float>(window.height - 100), 12, muted, width - 44);
        }
        window.fill({0, static_cast<float>(window.height - 52), width, 52}, {17, 29, 43, 255});
        window.text(
            status_.empty() ? tr("就绪 · 右键任何播放器窗口打开工具", "Ready · Right click a player window to open tools") : status_,
            22, static_cast<float>(window.height - 37), 13, muted, width - 44);
    }

    void Application::smokeStep() {
        const auto now = SDL_GetTicks();
        if (now - lastSmoke_ < 350) return;
        lastSmoke_ = now;
        if (smokeStage_ == 0) {
            for (auto &[name, window]: windows_) {
                static_cast<void>(name);
                window->show(true);
            }
            smokeStage_ = 1;
        } else if (smokeStage_ == 1) {
            if (!skins_.empty()) applySkin(skins_.front());
            smokeStage_ = 2;
        } else if (smokeStage_ < 10) {
            settings_.visual = (smokeStage_ - 2) % 7;
            if (smokeStage_ >= 4) { page_ = (smokeStage_ - 4) % 5; }
            ++smokeStage_;
        } else {
            SDL_Log("UI smoke completed: %s", utf8(smokeOutput_).c_str());
            running_ = false;
        }
    }
}
