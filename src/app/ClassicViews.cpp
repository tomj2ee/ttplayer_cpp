#include "app/Application.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace ttplayer::app {
    namespace {
        class Clip final {
        public:
            Clip(SDL_Renderer *renderer, const SDL_FRect bounds) : renderer_(renderer),
                                                                   enabled_(SDL_RenderClipEnabled(renderer)) {
                SDL_GetRenderClipRect(renderer, &previous_);
                const SDL_Rect clip{
                    static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.w),
                    static_cast<int>(bounds.h)
                };
                SDL_SetRenderClipRect(renderer, &clip);
            }

            ~Clip() { SDL_SetRenderClipRect(renderer_, enabled_ ? &previous_ : nullptr); }

        private:
            SDL_Renderer *renderer_;
            bool enabled_;
            SDL_Rect previous_{};
        };

        int frames(SDL_Surface *surface) {
            return !surface ? 1 : surface->w % 3 == 0 ? 3 : surface->w % 2 == 0 ? 2 : 1;
        }
    }

    void Application::renderSkinScrollbar(UiWindow &window, const SDL_FRect bounds, const int count, const int rows,
                                          int &scroll) {
        if (!skin_ || bounds.w <= 0 || bounds.h <= 0 || count <= rows) return;
        const auto *definition = skin_->window("playlist_window");
        const auto *control = definition ? definition->control("scrollbar") : nullptr;
        auto *buttons = control ? skin_->image(control->value("buttons_image")) : nullptr;
        auto *thumb = control ? skin_->image(control->value("thumb_image")) : nullptr;
        auto *bar = control ? skin_->image(control->value("bar_image")) : nullptr;
        const int maximum = std::max(1, count - rows);
        const float buttonHeight = buttons
                                       ? std::min(static_cast<float>(buttons->h / 2), bounds.h / 3)
                                       : std::min(12.0F, bounds.h / 3);
        const SDL_FRect top{bounds.x, bounds.y, bounds.w, buttonHeight};
        const SDL_FRect bottom{bounds.x, bounds.y + bounds.h - buttonHeight, bounds.w, buttonHeight};
        for (int half = 0; half < 2; ++half) {
            const auto target = half == 0 ? top : bottom;
            if (buttons) {
                const int columns = frames(buttons);
                const int state = contains(target, window.mouseX, window.mouseY) ? std::min(1, columns - 1) : 0;
                const SDL_FRect source{
                    static_cast<float>(state * buttons->w / columns), static_cast<float>(half * buttons->h / 2),
                    static_cast<float>(buttons->w / columns), static_cast<float>(buttons->h / 2)
                };
                window.bitmap(buttons, target, &source);
            } else {
                window.fill(target, {121, 148, 165, 255});
                window.text(half == 0 ? "^" : "v", target.x + 2, target.y - 1, 9, {240, 246, 250, 255}, bounds.w - 2);
            }
            window.hits.push_back({
                target, [&scroll, maximum, half] { scroll = std::clamp(scroll + (half == 0 ? -1 : 1), 0, maximum); },
                {}, false
            });
        }
        const SDL_FRect track{bounds.x, bounds.y + buttonHeight, bounds.w, std::max(1.0F, bounds.h - buttonHeight * 2)};
        if (bar) window.bitmap(bar, track);
        else window.fill(track, {61, 85, 105, 255});
        const float minimumThumb = thumb ? static_cast<float>(thumb->h) : 12;
        const float thumbHeight = std::min(track.h, std::max(minimumThumb, track.h * static_cast<float>(rows) / count));
        const float travel = std::max(1.0F, track.h - thumbHeight);
        const SDL_FRect target{track.x, track.y + travel * static_cast<float>(scroll) / maximum, track.w, thumbHeight};
        if (thumb) {
            const int columns = frames(thumb);
            const int state = contains(target, window.mouseX, window.mouseY) ? std::min(1, columns - 1) : 0;
            const SDL_FRect source{
                static_cast<float>(state * thumb->w / columns), 0, static_cast<float>(thumb->w / columns),
                static_cast<float>(thumb->h)
            };
            window.bitmap(thumb, target, &source);
        } else window.fill(target, {170, 194, 209, 255});
        window.hits.push_back({
            track, {},
            [&scroll, maximum](const float ratio) { scroll = static_cast<int>(std::round((1 - ratio) * maximum)); },
            true,
            {track.x, track.y + thumbHeight / 2, track.w, travel}
        });
    }

    void Application::renderClassicPlaylist(UiWindow &window, const SDL_FRect bounds) {
        if (bounds.w < 20 || bounds.h < 5) return;
        const auto &style = skin_->style("playlist");
        const auto background = parseColor(style.value("color_bkgnd", "#000000"));
        const auto alternate = parseColor(style.value("color_bkgnd2", "#202020"));
        const auto foreground = parseColor(style.value("color_text", "#0080ff"));
        const auto selectedColor = parseColor(style.value("color_hilight", "#00ff00"));
        const auto selection = parseColor(style.value("color_select", "#3269c8"));
        const auto numberColor = parseColor(style.value("color_number", "#008000"));
        const auto durationColor = parseColor(style.value("color_duration", "#c08020"));
        const float rowHeight = std::max(18.0F, window.measure("中文", 12, "SimSun").y + 4);
        const int rows = std::max(1, static_cast<int>(bounds.h / rowHeight));
        const float minimum = std::min(70.0F, bounds.w * 0.3F);
        const float leftWidth = std::clamp(bounds.w * settings_.playlistSplit, minimum, bounds.w - minimum);
        playlistDividerX_ = bounds.x + leftWidth;
        const SDL_FRect left{bounds.x, bounds.y, leftWidth - 2, bounds.h};
        const SDL_FRect right{bounds.x + leftWidth + 2, bounds.y, bounds.w - leftWidth - 2, bounds.h};
        window.fill(bounds, background);
        playlistScroll_ = std::clamp(playlistScroll_, 0, std::max(0, static_cast<int>(settings_.lists.size()) - rows));
        const auto *scrollControl = skin_->window("playlist_window")->control("scrollbar");
        auto *scrollBar = scrollControl ? skin_->image(scrollControl->value("bar_image")) : nullptr;
        const float scrollbarWidth = scrollBar ? static_cast<float>(scrollBar->w) : 12;
        const bool leftScrollbar = static_cast<int>(settings_.lists.size()) > rows;
        {
            const Clip clip(window.renderer, left);
            for (int row = 0; row < rows + 1 && row + playlistScroll_ < static_cast<int>(settings_.lists.size()); ++
                 row) {
                const int index = row + playlistScroll_;
                const SDL_FRect rect{
                    left.x, left.y + row * rowHeight, left.w - (leftScrollbar ? scrollbarWidth : 0), rowHeight
                };
                if (index == settings_.playlist) window.fill(rect, selection);
                window.text(settings_.lists[static_cast<std::size_t>(index)].name, rect.x + 2, rect.y + 1, 12,
                            index == settings_.playlist ? selectedColor : foreground, std::max(1.0F, rect.w - 4), false,
                            "SimSun");
                auto hitBounds = rect;
                hitBounds.h = std::max(0.0F, std::min(rect.h, left.y + left.h - rect.y));
                window.hits.push_back({
                    hitBounds, [this, &window, index] {
                        settings_.playlist = index;
                        selected_ = -1;
                        selectedSongs_.clear();
                        selectionAnchor_ = -1;
                        settings_.song = -1;
                        window.scroll = 0;
                        dirty_ = true;
                    },
                    {}, false
                });
            }
        }
        if (leftScrollbar) renderSkinScrollbar(
            window, {left.x + left.w - scrollbarWidth, left.y, scrollbarWidth, left.h},
            static_cast<int>(settings_.lists.size()), rows, playlistScroll_);
        const auto &songs = playlist().songs;
        std::vector<int> visible;
        const auto needle = lower(filter_);
        for (std::size_t index = 0; index < songs.size(); ++index) {
            if (needle.empty() || lower(
                    songs[index].title + " " + songs[index].artist + " " + utf8(songs[index].path.filename())).
                find(needle) != std::string::npos) visible.push_back(static_cast<int>(index));
        }
        const int maximum = std::max(0, static_cast<int>(visible.size()) - rows);
        window.scroll = std::clamp(window.scroll, 0, maximum);
        const float contentWidth = right.w - (maximum > 0 ? scrollbarWidth : 0);
        {
            const Clip clip(window.renderer, {right.x, right.y, contentWidth, right.h});
            for (int row = 0; row < rows + 1 && row + window.scroll < static_cast<int>(visible.size()); ++row) {
                const int index = visible[static_cast<std::size_t>(row + window.scroll)];
                const auto &song = songs[static_cast<std::size_t>(index)];
                const bool selected = selectedSongs_.contains(index);
                const SDL_FRect rect{right.x, right.y + row * rowHeight, contentWidth, rowHeight};
                window.fill(rect, selected ? selection : index % 2 ? alternate : background);
                const auto prefix = std::to_string(index + 1) + ". ";
                const auto duration = song.duration > 0 ? "(" + timeText(song.duration) + ")" : "";
                const auto title = songTitle(song, index);
                const float prefixWidth = window.measure(prefix, 12, "SimSun").x;
                const float durationWidth = window.measure(duration, 12, "SimSun").x;
                window.text(prefix, rect.x + 5, rect.y + 2, 12, selected ? selectedColor : numberColor,
                            std::max(1.0F, rect.w - 5), false, "SimSun");
                window.text(title, rect.x + 5 + prefixWidth, rect.y + 2, 12, selected ? selectedColor : foreground,
                            std::max(1.0F, rect.w - prefixWidth - durationWidth - 12), false, "SimSun");
                if (!duration.empty() && rect.w > durationWidth + 10) window.text(
                    duration, rect.x + rect.w - durationWidth - 5, rect.y + 2, 12,
                    selected ? selectedColor : durationColor, durationWidth, false, "SimSun");
                auto hitBounds = rect;
                hitBounds.h = std::max(0.0F, std::min(rect.h, right.y + right.h - rect.y));
                if (songDragging_ &&songDropIndex_ == index) window.fill(
                    {rect.x, rect.y, rect.w, 2}, {22, 119, 255, 255});
                window.tip(hitBounds, title + "\n" + song.artist + "  " + song.album + "\n" + utf8(song.path));
                window.hits.push_back({
                    hitBounds, [this, index] { if (clickCount_ >= 2) playSong(settings_.playlist, index); }, {}, false,
                    {}, index
                });
            }
        }
        if (maximum > 0) renderSkinScrollbar(
            window, {right.x + right.w - scrollbarWidth, right.y, scrollbarWidth, right.h},
            static_cast<int>(visible.size()), rows, window.scroll);
        const SDL_FRect divider{playlistDividerX_ - 2, bounds.y, 5, bounds.h};
        window.fill(divider, contains(divider, window.mouseX, window.mouseY)
                                 ? SDL_Color{22, 119, 255, 170}
                                 : SDL_Color{0, 0, 0, 70});
        window.hits.push_back({
            divider, {}, [this](const float ratio) {
                settings_.playlistSplit = std::clamp(ratio, 0.15F, 0.75F);
                dirty_ = true;
            },
            false, bounds
        });
    }

    void Application::renderClassicLyrics(UiWindow &window, const SDL_FRect bounds) {
        const auto &style = skin_->style("lyric");
        const auto textColor = parseColor(style.value("textcolor", "#8bbac6"));
        const auto currentColor = parseColor(style.value("hilightcolor", "#ffffff"));
        auto wordColor = parseColor(style.value("hilightwordcolor", "#ffffff"));
        auto background = parseColor(style.value("bkgndcolor", "#000000"));
        background.a = 179;
        int fontSize = 14;
        std::string family;
        const auto fontSpec = style.value("font");
        if (!fontSpec.empty()) {
            try { fontSize = std::clamp(std::abs(std::stoi(fontSpec)), 12, 28); } catch (const std::exception &) {
            }
            std::istringstream stream(fontSpec);
            std::string part;
            for (int index = 0; std::getline(stream, part, ','); ++index) if (index == 13) family = part;
        }
        if (wordColor.r == currentColor.r && wordColor.g == currentColor.g && wordColor.b == currentColor.b)
            wordColor = {255, 220, 80, 255};
        window.fill(bounds, background);
        const Clip clip(window.renderer, bounds);
        if (lyrics_.lines.empty()) {
            const auto message = tr("暂无歌词", "No lyrics");
            const auto size = window.measure(message, fontSize, family);
            window.text(message, bounds.x, bounds.y + (bounds.h - size.y) / 2, fontSize, {120, 130, 150, 255}, bounds.w,
                        true, family);
            return;
        }
        const auto position = engine_.positionMs();
        const int current = lyrics_.active(position);
        const float rowHeight = std::max(26.0F, window.measure("歌词", fontSize, family).y);
        lyricRowHeight_ = rowHeight;
        const float centerLine = lyricDragging_ ? lyricPreviewLine_ : static_cast<float>(std::max(0, current));
        const int centerIndex = static_cast<int>(std::round(centerLine));
        const int radius = static_cast<int>(bounds.h / rowHeight) / 2 + 1;
        for (int delta = -radius; delta <= radius; ++delta) {
            const int index = centerIndex + delta;
            if (index < 0 || index >= static_cast<int>(lyrics_.lines.size())) continue;
            const auto &line = lyrics_.lines[static_cast<std::size_t>(index)];
            if (line.text.empty()) continue;
            const SDL_FRect row{
                bounds.x, bounds.y + bounds.h / 2 + (index - centerLine) * rowHeight - rowHeight / 2, bounds.w,
                rowHeight
            };
            auto color = textColor;
            color.a = static_cast<Uint8>(255 * std::max(0.15F, 1 - std::abs(delta) * 0.22F));
            if (delta == 0) window.karaoke(line.text, row,
                                           lyricDragging_ || !karaoke_ || current < 0
                                               ? 0
                                               : lyrics_.progress(index, position, engine_.durationMs()), fontSize + 2,
                                           currentColor, wordColor, family);
            else {
                const auto metrics = window.measure(line.text, fontSize, family);
                window.text(line.text, row.x, row.y + (row.h - metrics.y) / 2, fontSize, color, row.w, true, family);
            }
        }
        if (lyricDragging_) {
            const auto index = std::clamp(centerIndex, 0, static_cast<int>(lyrics_.lines.size()) - 1);
            window.fill({bounds.x, bounds.y + bounds.h / 2, bounds.w, 1}, {22, 119, 255, 180});
            window.text(timeText(lyrics_.lines[index].time), bounds.x + 4, bounds.y + bounds.h / 2 + 4, 12,
                        {22, 119, 255, 255}, bounds.w - 8);
        }
        window.hits.push_back({bounds, {}, {}, false, {}, -1, true});
    }
}
