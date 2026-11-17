#include "app/Application.hpp"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ttplayer::app {
    namespace {
#ifdef _WIN32
        HWND nativeHandle(SDL_Window *window) {
            return static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                            SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        }
#endif

        SDL_Rect windowBounds(SDL_Window *window) {
#ifdef _WIN32
            RECT rectangle{};
            if (const auto handle = nativeHandle(window); handle && GetWindowRect(handle, &rectangle))
                return {
                    rectangle.left, rectangle.top, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top
                };
#elif defined(__APPLE__)
            SDL_Rect nativeBounds{};
            if (macWindowBounds(window, nativeBounds)) return nativeBounds;
#endif
            SDL_Rect bounds{};
            SDL_GetWindowPosition(window, &bounds.x, &bounds.y);
            SDL_GetWindowSize(window, &bounds.w, &bounds.h);
            return bounds;
        }

        void moveWindow(SDL_Window *window, const int left, const int top) {
            const auto current = windowBounds(window);
            if (current.x == left && current.y == top) return;
#ifdef _WIN32
            if (const auto handle = nativeHandle(window)) {
                SetWindowPos(handle, nullptr, left, top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
                return;
            }
#elif defined(__APPLE__)
            if (moveMacWindow(window, left, top)) return;
#endif
            SDL_SetWindowPosition(window, left, top);
        }

        std::vector<SDL_Rect> workAreas() {
            std::vector<SDL_Rect> areas;
            int count = 0;
            auto *displays = SDL_GetDisplays(&count);
            for (int index = 0; index < count; ++index) {
                SDL_Rect area{};
                if (SDL_GetDisplayUsableBounds(displays[index], &area) && area.w > 0 && area.h > 0) areas.
                        push_back(area);
            }
            SDL_free(displays);
            return areas;
        }

        bool available(const UiWindow &window) {
            return window.visible() && !(SDL_GetWindowFlags(window.window) & (
                                             SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN));
        }
    }

    void Application::initializeWindowManagement() {
        recoverWindows();
        for (const auto &[name, window]: windows_) {
            static_cast<void>(name);
            normalWindowBounds(*window);
        }
        if (!windowWatchInstalled_) {
            if (!SDL_AddEventWatch(windowEventWatch, this)) throw std::runtime_error(SDL_GetError());
            windowWatchInstalled_ = true;
        }
#ifdef _WIN32
        nativeWindowManagement_ = std::string_view(SDL_GetCurrentVideoDriver()) == "windows";
        if (!nativeWindowManagement_) return;
        SDL_SetWindowsMessageHook([](void *userdata, MSG *message) -> bool {
            auto &application = *static_cast<Application *>(userdata);
            if (message->message != WM_ENTERSIZEMOVE && message->message != WM_MOVING && message->message != WM_SIZING
                &&
                message->message != WM_EXITSIZEMOVE && message->message != WM_CANCELMODE)
                return true;
            UiWindow *window = nullptr;
            for (const auto &[name, candidate]: application.windows_) {
                static_cast<void>(name);
                if (nativeHandle(candidate->window) == message->hwnd) {
                    window = candidate.get();
                    break;
                }
            }
            if (!window) return true;
            if (message->message == WM_ENTERSIZEMOVE) {
                application.beginWindowDrag(*window);
                POINT pointer{};
                if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && GetCursorPos(&pointer)) {
                    application.windowDrag_.capturePointer(
                        {static_cast<float>(pointer.x), static_cast<float>(pointer.y)},
                        WindowDragSession::PointerMode::Native);
                }
            } else if (message->message == WM_EXITSIZEMOVE || message->message == WM_CANCELMODE) {
                if (application.windowDrag_.root() == window->name) application.endWindowDrag();
            } else if (message->lParam && available(*window)) {
                if (application.windowDrag_.root() != window->name) application.beginWindowDrag(*window);
                auto &rectangle = *reinterpret_cast<RECT *>(message->lParam);
                SDL_Rect bounds{
                    rectangle.left, rectangle.top, rectangle.right - rectangle.left, rectangle.bottom - rectangle.top
                };
                const bool bypass = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                if (message->message == WM_MOVING) {
                    POINT pointer{};
                    if (application.windowDrag_.usesNativePointer() && GetCursorPos(&pointer)) {
                        const auto proposed = application.windowDrag_.pointerBounds({
                            static_cast<float>(pointer.x), static_cast<float>(pointer.y)
                        });
                        bounds.x = proposed.x;
                        bounds.y = proposed.y;
                    }
                    bounds = application.moveWindowDrag(*window, bounds, bypass);
                } else {
                    unsigned edges = 0;
                    const auto edge = message->wParam;
                    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) edges |= layout::left;
                    if (edge == WMSZ_RIGHT || edge == WMSZ_TOPRIGHT || edge == WMSZ_BOTTOMRIGHT) edges |= layout::right;
                    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) edges |= layout::top;
                    if (edge == WMSZ_BOTTOM || edge == WMSZ_BOTTOMLEFT || edge == WMSZ_BOTTOMRIGHT)
                        edges |= layout::bottom;
                    application.windowDrag_.setResizeEdges(edges, bypass);
                    if (!bypass) {
                        SDL_Point minimum{}, client{};
                        SDL_GetWindowMinimumSize(window->window, &minimum.x, &minimum.y);
                        SDL_GetWindowSize(window->window, &client.x, &client.y);
                        const auto outer = windowBounds(window->window);
                        minimum.x += outer.w - client.x;
                        minimum.y += outer.h - client.y;
                        bounds = application.windowDrag_.constrainResize(
                            bounds, minimum, application.windowPeers(*window), false);
                    }
                }
                rectangle = {bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h};
                application.dirty_ = true;
            }
            return true;
        }, this);
#endif
    }

    SDL_Rect Application::normalWindowBounds(UiWindow &window) {
        if (SDL_GetWindowFlags(window.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) {
            const auto previous = normalWindowBounds_.find(window.name);
            if (previous != normalWindowBounds_.end()) return previous->second;
        }
        SDL_Rect bounds{};
        SDL_GetWindowPosition(window.window, &bounds.x, &bounds.y);
        SDL_GetWindowSize(window.window, &bounds.w, &bounds.h);
        normalWindowBounds_[window.name] = bounds;
        return bounds;
    }

    bool SDLCALL Application::windowEventWatch(void *userdata, SDL_Event *pending) {
        if (!SDL_IsMainThread()) return true;
        auto &application = *static_cast<Application *>(userdata);
        if (pending->type != SDL_EVENT_WINDOW_MOVED && pending->type != SDL_EVENT_WINDOW_RESIZED &&
            pending->type != SDL_EVENT_WINDOW_RESTORED && pending->type != SDL_EVENT_WINDOW_SHOWN && pending->type !=
            SDL_EVENT_WINDOW_EXPOSED)
            return true;
        try {
            for (const auto &[name, window]: application.windows_) {
                static_cast<void>(name);
                if (SDL_GetWindowID(window->window) != pending->window.windowID) continue;
                if (pending->type != SDL_EVENT_WINDOW_EXPOSED) application.normalWindowBounds(*window);
                else if (application.nativeWindowManagement_ && !application.windowDrag_.root().empty() && !application.
                         liveWindowRendering_ &&
                         SDL_GetTicks() - application.lastLiveWindowRender_ >= 30) {
                    application.liveWindowRendering_ = true;
                    application.lastLiveWindowRender_ = SDL_GetTicks();
                    try {
                        application.spectrum_ = application.engine_.spectrumBars(48);
                        application.waveform_ = application.engine_.waveform(256);
                        for (const auto &[key, visible]: application.windows_) {
                            static_cast<void>(key);
                            if (visible->visible() && !(SDL_GetWindowFlags(visible->window) & SDL_WINDOW_MINIMIZED))
                                application.render(*visible);
                        }
                    } catch (...) {
                        application.liveWindowRendering_ = false;
                        throw;
                    }
                    application.liveWindowRendering_ = false;
                }
                break;
            }
        } catch (const std::exception &error) { SDL_Log("Window update failed: %s", error.what()); }
        return true;
    }

    void Application::restoreSuspendedWindows() {
        endWindowDrag();
        auto visibility = std::move(suspendedVisibility_);
        suspendedVisibility_.clear();
        for (const auto &[name, visible]: visibility) {
            if (name == "dialog") continue;
            auto &window = view(name);
            window.show(visible && (name != "progress" || importer_.busy()));
            if (window.visible() && (SDL_GetWindowFlags(window.window) & SDL_WINDOW_MINIMIZED)) SDL_RestoreWindow(
                window.window);
        }
        recoverWindows();
        SDL_RaiseWindow(view(view("mini").visible() ? "mini" : "player").window);
        if (const auto dialog = visibility.find("dialog"); dialog != visibility.end()) {
            view("dialog").show(dialog->second);
            if (dialog->second && (SDL_GetWindowFlags(view("dialog").window) & SDL_WINDOW_MINIMIZED)) SDL_RestoreWindow(
                view("dialog").window);
        }
        raiseVisibleWindows(view(view("mini").visible() ? "mini" : "player"));
    }

    void Application::raiseVisibleWindows(UiWindow &anchor) {
        if (!anchor.visible() || !suspendedVisibility_.empty() || (
                SDL_GetWindowFlags(anchor.window) & SDL_WINDOW_MINIMIZED)) return;
        const char *previous = SDL_GetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED);
        const auto activation = previous ? std::optional<std::string>(previous) : std::nullopt;
        SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0", SDL_HINT_OVERRIDE);
        SDL_RaiseWindow(anchor.window);
        for (const auto &[name, window]: windows_) {
            if (name == anchor.name || name == "player" || name == "mini" || name == "desktop" || name == "dialog" || !
                window->visible()) continue;
            if (SDL_GetWindowFlags(window->window) & SDL_WINDOW_MINIMIZED) continue;
            SDL_RaiseWindow(window->window);
        }
        if (view("dialog").visible()) SDL_RaiseWindow(view("dialog").window);
        popupMenu_.raise();
        if (activation) SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, activation->c_str(),
                                                SDL_HINT_OVERRIDE);
        else SDL_ResetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED);
        dirty_ = true;
    }

    void Application::centerWindowOn(UiWindow &window, UiWindow &owner) {
        SDL_Rect bounds{}, anchor{}, area{};
        SDL_GetWindowPosition(owner.window, &anchor.x, &anchor.y);
        SDL_GetWindowSize(owner.window, &anchor.w, &anchor.h);
        SDL_GetWindowSize(window.window, &bounds.w, &bounds.h);
        bounds.x = anchor.x + (anchor.w - bounds.w) / 2;
        bounds.y = anchor.y + (anchor.h - bounds.h) / 2;
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(owner.window), &area)) {
            bounds.x = std::clamp(bounds.x, area.x, area.x + std::max(0, area.w - bounds.w));
            bounds.y = std::clamp(bounds.y, area.y, area.y + std::max(0, area.h - bounds.h));
        }
        SDL_SetWindowPosition(window.window, bounds.x, bounds.y);
    }

    UiWindow &Application::dialogOwner() {
        if (view("dialog").visible()) return view("dialog");
        if (view("settings").visible()) return view("settings");
        return view(view("mini").visible() ? "mini" : "player");
    }

    void Application::updateDialogOwner(const bool visible) {
        auto &dialog = view("dialog");
        auto &owner = view(view("mini").visible() ? "mini" : "player");
        if (SDL_GetWindowParent(dialog.window) != owner.window) {
            dialog.show(false);
            SDL_SetWindowModal(dialog.window, false);
            SDL_SetWindowParent(dialog.window, owner.window);
            SDL_SetWindowModal(dialog.window, true);
            if (visible) centerWindowOn(dialog, owner);
        }
        if (visible) dialog.show(true);
    }

    void Application::resetWindowLayout() {
        if (!suspendedVisibility_.empty()) restoreSuspendedWindows();
        endWindowDrag();
        SDL_Rect area{};
        if (!SDL_GetDisplayUsableBounds(
            SDL_GetDisplayForWindow(view(view("mini").visible() ? "mini" : "player").window), &area)) return;
        for (const auto &[name, window]: windows_) if (layout::coreWindow(name)) SDL_RestoreWindow(window->window);
        applySkin(settings_.skin);
        std::map<std::string, SDL_Rect> positions;
        const auto player = windowBounds(view("player").window), list = windowBounds(view("playlist").window);
        const auto *playerDefinition = skin_ ? skin_->window("player_window") : nullptr;
        SDL_Rect envelope{0, 0, player.w, player.h};
        for (const auto *name: {"player", "playlist", "lyric", "eq"}) {
            auto bounds = windowBounds(view(name).window);
            const auto *definition = skin_
                                         ? skin_->window(
                                             std::string(name == std::string_view("eq") ? "equalizer" : name) +
                                             "_window")
                                         : nullptr;
            if (definition && playerDefinition) {
                bounds.x = static_cast<int>(definition->position.x - playerDefinition->position.x);
                bounds.y = static_cast<int>(definition->position.y - playerDefinition->position.y);
            } else {
                bounds.x = name == std::string_view("lyric") ? player.w : 0;
                bounds.y = name == std::string_view("playlist")
                               ? player.h
                               : name == std::string_view("eq")
                                     ? player.h + list.h
                                     : 0;
            }
            positions.emplace(name, bounds);
            if (view(name).visible()) SDL_GetRectUnion(&envelope, &bounds, &envelope);
        }
        const int offsetX = area.x + std::max(0, (area.w - envelope.w) / 2) - envelope.x;
        const int offsetY = area.y + std::max(0, (area.h - envelope.h) / 2) - envelope.y;
        for (const auto &[name, bounds]: positions) moveWindow(view(name).window, offsetX + bounds.x,
                                                               offsetY + bounds.y);
        const auto mini = windowBounds(view("mini").window);
        moveWindow(view("mini").window, area.x + std::max(0, (area.w - mini.w) / 2),
                   area.y + std::min(50, std::max(0, area.h - mini.h)));
        recoverWindows();
        SDL_RaiseWindow(view(view("mini").visible() ? "mini" : "player").window);
        dirty_ = true;
    }

    void Application::beginWindowDrag(UiWindow &window) {
        windowDrag_.reset();
        if (!available(window)) return;
        WindowDragSession::WindowBounds candidates;
        for (const auto &[name, candidate]: windows_) {
            if (layout::coreWindow(name) && available(*candidate)) candidates.emplace(
                name, windowBounds(candidate->window));
        }
        windowDrag_.begin(window.name, windowBounds(window.window), candidates, workAreas());
    }

    std::vector<SDL_Rect> Application::windowPeers(const UiWindow &window) const {
        std::vector<SDL_Rect> peers;
        if (!layout::coreWindow(window.name)) return peers;
        for (const auto &[name, candidate]: windows_)
            if (name != window.name && !windowDrag_.members().contains(name) && layout::coreWindow(name) && available(
                    *candidate))
                peers.push_back(windowBounds(candidate->window));
        return peers;
    }

    SDL_Rect Application::moveWindowDrag(UiWindow &window, SDL_Rect proposed, const bool bypass) {
        if (windowDrag_.root() != window.name || !windowDrag_.members().contains(window.name)) return proposed;
        const auto origin = windowDrag_.members().at(window.name);
        std::set<std::string> activeMembers;
        for (const auto &[name, bounds]: windowDrag_.members()) {
            static_cast<void>(bounds);
            if (available(view(name))) activeMembers.insert(name);
        }
        proposed = windowDrag_.constrainMove(proposed, activeMembers, windowPeers(window), bypass);
        for (const auto &[name, bounds]: windowDrag_.members()) {
            if (name == window.name || !available(view(name))) continue;
            moveWindow(view(name).window, bounds.x + proposed.x - origin.x, bounds.y + proposed.y - origin.y);
        }
        return proposed;
    }

    void Application::endWindowDrag() {
        const bool pointer = windowDrag_.usesCapturedPointer();
        if (!windowDrag_.root().empty() && windowDrag_.members().contains(windowDrag_.root()) && available(
                view(windowDrag_.root()))) {
            const auto current = windowBounds(view(windowDrag_.root()).window);
            const auto origin = windowDrag_.members().at(windowDrag_.root());
            for (const auto &[name, bounds]: windowDrag_.members())
                if (name != windowDrag_.root() && available(view(name)))
                    moveWindow(view(name).window, bounds.x + current.x - origin.x, bounds.y + current.y - origin.y);
        }
        windowDrag_.reset();
        if (pointer) SDL_CaptureMouse(false);
        dirty_ = true;
    }

    void Application::trackWindowMove(UiWindow &window) {
        if (nativeWindowManagement_ || windowDrag_.usesCapturedPointer() || windowDrag_.root() != window.name) return;
        const auto proposed = moveWindowDrag(window, windowBounds(window.window),
                                             (SDL_GetModState() & SDL_KMOD_ALT) != 0);
        moveWindow(window.window, proposed.x, proposed.y);
    }

    bool Application::beginPointerWindowDrag(UiWindow &window, const SDL_MouseButtonEvent &button) {
        if (button.button != SDL_BUTTON_LEFT || !(SDL_GetWindowFlags(window.window) & SDL_WINDOW_BORDERLESS)) return
                false;
        const auto hit = windowHitTest(window, {static_cast<int>(button.x), static_cast<int>(button.y)});
        if (hit == SDL_HITTEST_NORMAL) return false;
        beginWindowDrag(window);
        if (windowDrag_.root().empty()) return false;
        SDL_FPoint pointer{};
        SDL_GetGlobalMouseState(&pointer.x, &pointer.y);
        windowDrag_.capturePointer(pointer, WindowDragSession::PointerMode::Captured);
        unsigned edges = 0;
        if (hit == SDL_HITTEST_RESIZE_LEFT || hit == SDL_HITTEST_RESIZE_TOPLEFT || hit == SDL_HITTEST_RESIZE_BOTTOMLEFT)
            edges |= layout::left;
        if (hit == SDL_HITTEST_RESIZE_RIGHT || hit == SDL_HITTEST_RESIZE_TOPRIGHT || hit ==
            SDL_HITTEST_RESIZE_BOTTOMRIGHT) edges |= layout::right;
        if (hit == SDL_HITTEST_RESIZE_TOP || hit == SDL_HITTEST_RESIZE_TOPLEFT || hit == SDL_HITTEST_RESIZE_TOPRIGHT)
            edges |= layout::top;
        if (hit == SDL_HITTEST_RESIZE_BOTTOM || hit == SDL_HITTEST_RESIZE_BOTTOMLEFT || hit ==
            SDL_HITTEST_RESIZE_BOTTOMRIGHT) edges |= layout::bottom;
        windowDrag_.setResizeEdges(edges);
        contextWindow_.clear();
        dragging_.reset();
        SDL_CaptureMouse(true);
        return true;
    }

    void Application::updatePointerWindowDrag(const SDL_FPoint pointer, const bool bypass) {
        if (!windowDrag_.usesCapturedPointer() || windowDrag_.root().empty()) return;
        auto &window = view(windowDrag_.root());
        auto proposed = windowDrag_.pointerBounds(pointer);
        const auto edges = windowDrag_.resizeEdges();
        if (edges) {
            SDL_Point minimum{};
            SDL_GetWindowMinimumSize(window.window, &minimum.x, &minimum.y);
            proposed = windowDrag_.constrainResize(proposed, minimum, windowPeers(window), bypass);
            SDL_SetWindowSize(window.window, proposed.w, proposed.h);
        } else {
            proposed = moveWindowDrag(window, proposed, bypass);
        }
        moveWindow(window.window, proposed.x, proposed.y);
        dirty_ = true;
    }

    void Application::finishPointerWindowDrag(const bool cancel) {
        if (!windowDrag_.usesCapturedPointer() || windowDrag_.root().empty()) return;
        if (cancel) {
            for (const auto &[name, bounds]: windowDrag_.members()) {
                SDL_SetWindowSize(view(name).window, bounds.w, bounds.h);
                moveWindow(view(name).window, bounds.x, bounds.y);
            }
        }
        endWindowDrag();
        SDL_CaptureMouse(false);
    }

    void Application::recoverWindows() {
        endWindowDrag();
        const auto areas = workAreas();
        windowWorkAreas_ = areas;
        if (windows_.contains("player") && available(view("player"))) {
            const auto original = windowBounds(view("player").window);
            const auto recovered = layout::recover(original, areas);
            if (original.x != recovered.x || original.y != recovered.y) {
                beginWindowDrag(view("player"));
                for (const auto &[name, bounds]: windowDrag_.members())
                    moveWindow(view(name).window, bounds.x + recovered.x - original.x,
                               bounds.y + recovered.y - original.y);
                endWindowDrag();
            }
        }
        for (const auto &[name, window]: windows_) {
            static_cast<void>(name);
            if (SDL_GetWindowFlags(window->window) & (
                    SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED)) continue;
            const auto bounds = layout::recover(windowBounds(window->window), areas);
            moveWindow(window->window, bounds.x, bounds.y);
        }
    }

    void Application::refreshWindowWorkAreas() {
        if (!windowDrag_.root().empty()) return;
        const auto current = workAreas();
        const bool unchanged = current.size() == windowWorkAreas_.size() && std::equal(
                                   current.begin(), current.end(), windowWorkAreas_.begin(),
                                   [](const SDL_Rect &first, const SDL_Rect &second) {
                                       return SDL_RectsEqual(&first, &second);
                                   });
        if (!unchanged) recoverWindows();
    }
}
