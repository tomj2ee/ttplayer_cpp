#include "app/PopupMenu.hpp"
#ifdef __APPLE__
#include "app/PlatformMac.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ttplayer::app {
    namespace {
        constexpr int rowHeight = 26;
        constexpr int padding = 4;
        constexpr SDL_Color foreground{230, 241, 249, 255};
        constexpr SDL_Color muted{132, 159, 180, 255};

        bool selectable(const MenuEntry &entry) {
            return !entry.label.empty() && (!entry.action.empty() || !entry.children.empty());
        }

        SDL_Point screenPosition(SDL_Window *window) {
            SDL_Point result{};
            for (auto *current = window; current; current = SDL_GetWindowParent(current)) {
                int left = 0, top = 0;
                SDL_GetWindowPosition(current, &left, &top);
                result.x += left;
                result.y += top;
            }
            return result;
        }
    }

    PopupMenu::~PopupMenu() { close(); }
    bool PopupMenu::isOpen() const { return !levels_.empty(); }

    void PopupMenu::raise() {
        if (!isOpen()) return;
        const char *previous = SDL_GetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED);
        const auto activation = previous ? std::optional<std::string>(previous) : std::nullopt;
        SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0", SDL_HINT_OVERRIDE);
        for (const auto &level: levels_) {
#ifdef __APPLE__
            raiseMacPopup(level->view->window);
#else
            SDL_RaiseWindow(level->view->window);
#endif
        }
        if (activation) SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, activation->c_str(),
                                                SDL_HINT_OVERRIDE);
        else SDL_ResetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED);
    }

    void PopupMenu::trim(const std::size_t count) {
        while (levels_.size() > count) levels_.pop_back();
    }

    void PopupMenu::close() {
        trim(0);
        owner_ = nullptr;
        fonts_ = nullptr;
        hoverAt_ = 0;
    }

    void PopupMenu::open(UiWindow &owner, Fonts &fonts, std::vector<MenuEntry> entries, const float left,
                         const float top) {
        close();
        action_.reset();
        if (entries.empty()) return;
        owner_ = owner.window;
        fonts_ = &fonts;
        float windowX = left, windowY = top;
        SDL_RenderCoordinatesToWindow(owner.renderer, left, top, &windowX, &windowY);
        try {
            append(owner_, std::move(entries), {
                       static_cast<int>(std::round(windowX)), static_cast<int>(std::round(windowY))
                   });
            openedAt_ = SDL_GetTicks();
        } catch (...) {
            close();
            throw;
        }
    }

    void PopupMenu::append(SDL_Window *parent, std::vector<MenuEntry> entries, SDL_Point offset) {
        SDL_Rect workArea{0, 0, 1280, 720};
        SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(parent), &workArea);
        const int maximumWidth = std::max(80, std::min(520, workArea.w - 16));
        const int maximumRows = std::max(1, (workArea.h - 16 - 40) / rowHeight);
        auto level = std::make_unique<Level>();
        level->entries = std::move(entries);
        level->rows = std::min(maximumRows, static_cast<int>(level->entries.size()));
        const bool scrolling = level->rows < static_cast<int>(level->entries.size());
        level->inset = scrolling ? 20.0F : static_cast<float>(padding);
        const int height = level->rows * rowHeight + static_cast<int>(level->inset * 2);
        int width = std::min(200, maximumWidth);
        for (const auto &entry: level->entries) {
            int labelWidth = 0;
            if (auto *font = fonts_->get(14)) TTF_GetStringSize(font, entry.label.c_str(), entry.label.size(),
                                                                &labelWidth, nullptr);
            width = std::max(width, std::min(maximumWidth, labelWidth + 72));
        }
        const auto origin = screenPosition(parent);
        if (parent != owner_ && origin.x + offset.x + width > workArea.x + workArea.w) offset.x = -width + 2;
        offset.x = std::clamp(origin.x + offset.x, workArea.x + 4,
                              std::max(workArea.x + 4, workArea.x + workArea.w - width - 4)) - origin.x;
        offset.y = std::clamp(origin.y + offset.y, workArea.y + 4,
                              std::max(workArea.y + 4, workArea.y + workArea.h - height - 4)) - origin.y;
        level->view = std::make_unique<UiWindow>("menu", "TTPlayer menu", width, height,
                                                 SDL_WINDOW_POPUP_MENU | SDL_WINDOW_ALWAYS_ON_TOP, *fonts_,
                                                 parent, offset);
        draw(*level);
        if (!SDL_ShowWindow(level->view->window)) throw std::runtime_error(SDL_GetError());
        levels_.push_back(std::move(level));
        raise();
    }

    void PopupMenu::draw(Level &level) {
        auto &view = *level.view;
        view.begin();
        const float width = static_cast<float>(view.width), height = static_cast<float>(view.height);
        view.fill({0, 0, width, height}, {20, 34, 50, 255});
        view.outline({0, 0, width, height}, {83, 139, 155, 255});
        if (level.inset > padding) {
            view.text(level.first > 0 ? "^" : "", width / 2 - 4, 1, 12, muted);
            view.text(level.first + level.rows < static_cast<int>(level.entries.size()) ? "v" : "", width / 2 - 4,
                      height - 18, 12, muted);
        }
        for (int index = level.first; index < std::min(level.first + level.rows, static_cast<int>(level.entries.size()))
             ; ++index) {
            const auto &entry = level.entries[index];
            const float top = level.inset + static_cast<float>((index - level.first) * rowHeight);
            const SDL_FRect row{4, top, width - 8, static_cast<float>(rowHeight)};
            if (entry.label.empty()) {
                view.fill({10, top + rowHeight / 2, width - 20, 1}, {59, 78, 95, 255});
                continue;
            }
            const bool enabled = selectable(entry);
            if (index == level.selected && enabled) view.fill(row, {38, 80, 94, 255});
            view.text(entry.label, 30, top + 3, 14, enabled ? foreground : muted, width - 62);
            SDL_SetRenderDrawColor(view.renderer, foreground.r, foreground.g, foreground.b, foreground.a);
            if (entry.checked) {
                SDL_RenderLine(view.renderer, 11, top + 13, 15, top + 17);
                SDL_RenderLine(view.renderer, 15, top + 17, 23, top + 8);
            }
            if (!entry.children.empty()) {
                SDL_RenderLine(view.renderer, width - 17, top + 9, width - 12, top + 13);
                SDL_RenderLine(view.renderer, width - 12, top + 13, width - 17, top + 17);
            }
        }
        SDL_RenderPresent(view.renderer);
    }

    bool PopupMenu::owns(SDL_Window *window) const {
        return std::any_of(levels_.begin(), levels_.end(), [window](const auto &level) {
            return level->view->window == window;
        });
    }

    int PopupMenu::rowAt(const Level &level, const float left, const float top) const {
        if (left < padding || left >= level.view->width - padding || top < level.inset || top >= level.inset + level.
            rows * rowHeight) return -1;
        const int index = level.first + static_cast<int>((top - level.inset) / rowHeight);
        return index < static_cast<int>(level.entries.size()) && selectable(level.entries[index]) ? index : -1;
    }

    void PopupMenu::select(const std::size_t depth, const int index, const bool hover) {
        auto &level = *levels_[depth];
        if (hover && hoverDepth_ != depth) hoverAt_ = 0;
        if (level.selected == index) return;
        level.selected = index;
        if (!hover) {
            trim(depth + 1);
            level.opened = -1;
        }
        hoverAt_ = hover ? SDL_GetTicks() + 180 : 0;
        hoverDepth_ = depth;
        draw(level);
    }

    void PopupMenu::navigate(const std::size_t depth, const int direction) {
        auto &level = *levels_[depth];
        const int count = static_cast<int>(level.entries.size());
        int index = level.selected;
        if (index < 0) index = direction > 0 ? -1 : 0;
        for (int attempt = 0; attempt < count; ++attempt) {
            index = (index + direction + count) % count;
            if (!selectable(level.entries[index])) continue;
            select(depth, index, false);
            if (index < level.first) level.first = index;
            if (index >= level.first + level.rows) level.first = index - level.rows + 1;
            draw(level);
            return;
        }
    }

    void PopupMenu::expand(const std::size_t depth, const bool keyboard) {
        auto &level = *levels_[depth];
        const int index = level.selected;
        hoverAt_ = 0;
        if (level.opened == index && levels_.size() > depth + 1) return;
        trim(depth + 1);
        level.opened = -1;
        if (index < 0 || level.entries[index].children.empty()) return;
        append(level.view->window, level.entries[index].children,
               {level.view->width - 2, static_cast<int>(level.inset) + (index - level.first) * rowHeight - padding});
        level.opened = index;
        if (keyboard) navigate(depth + 1, 1);
    }

    void PopupMenu::activate(const std::size_t depth) {
        auto &level = *levels_[depth];
        if (level.selected < 0) return;
        const auto &entry = level.entries[level.selected];
        if (!entry.children.empty()) {
            expand(depth, true);
            return;
        }
        if (entry.action.empty()) return;
        action_ = entry.action;
        close();
    }

    bool PopupMenu::handleEvent(const SDL_Event &event) {
        if (!isOpen()) return false;
        auto *target = SDL_GetWindowFromEvent(&event);
        if ((event.type == SDL_EVENT_WINDOW_HIDDEN || event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type ==
             SDL_EVENT_WINDOW_CLOSE_REQUESTED) && target == owner_) {
            close();
            return false;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !owns(target)) {
            close();
            return false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            const auto depth = levels_.size() - 1;
            switch (event.key.key) {
                case SDLK_ESCAPE: close();
                    break;
                case SDLK_DOWN: navigate(depth, 1);
                    break;
                case SDLK_UP: navigate(depth, -1);
                    break;
                case SDLK_HOME: select(depth, -1, false);
                    navigate(depth, 1);
                    break;
                case SDLK_END: select(depth, -1, false);
                    navigate(depth, -1);
                    break;
                case SDLK_PAGEDOWN: for (int count = 0; count < levels_[depth]->rows; ++count) navigate(depth, 1);
                    break;
                case SDLK_PAGEUP: for (int count = 0; count < levels_[depth]->rows; ++count) navigate(depth, -1);
                    break;
                case SDLK_RIGHT: expand(depth, true);
                    break;
                case SDLK_LEFT:
                    if (depth > 0) {
                        trim(depth);
                        levels_.back()->opened = -1;
                        hoverAt_ = 0;
                    }
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE: activate(depth);
                    break;
                default: break;
            }
            return true;
        }
        if (!owns(target)) return false;
        const auto found = std::find_if(levels_.begin(), levels_.end(), [target](const auto &level) {
            return level->view->window == target;
        });
        const auto depth = static_cast<std::size_t>(found - levels_.begin());
        auto &level = **found;
        if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            const float left = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
            const float top = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
            select(depth, rowAt(level, left, top), true);
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
                if (level.selected < 0 && level.inset > padding) {
                    trim(depth + 1);
                    level.opened = -1;
                    hoverAt_ = 0;
                    level.first = std::clamp(level.first + (top < level.inset ? -1 : 1), 0,
                                             static_cast<int>(level.entries.size()) - level.rows);
                    draw(level);
                } else activate(depth);
            }
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            const float delta = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
            trim(depth + 1);
            level.opened = -1;
            level.selected = -1;
            hoverAt_ = 0;
            level.first = std::clamp(level.first - static_cast<int>(std::round(delta * 3)), 0,
                                     static_cast<int>(level.entries.size()) - level.rows);
            draw(level);
        } else if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            draw(level);
        else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED || event.type == SDL_EVENT_WINDOW_HIDDEN) close();
        return true;
    }

    void PopupMenu::update() {
        if (!isOpen()) return;
        const auto now = SDL_GetTicks();
        if (now > openedAt_ + 200 && SDL_GetKeyboardFocus() != owner_ && !owns(SDL_GetKeyboardFocus())) {
            close();
            return;
        }
        if (hoverAt_ &&now >= hoverAt_ &&hoverDepth_<levels_.size()) expand(hoverDepth_, false);
    }

    std::optional<std::string> PopupMenu::takeAction() { return std::exchange(action_, std::nullopt); }
}
