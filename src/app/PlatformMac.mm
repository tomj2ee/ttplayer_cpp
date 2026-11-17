#include "app/Application.hpp"
#include <algorithm>

namespace ttplayer::app {
    void Application::applyMacDesktopLock() {
        configureMacDesktop(view("desktop").window, settings_.desktopLocked);
    }

    void Application::macWindowEvent(SDL_Window *native, const MacWindowEvent event) {
        auto found = std::find_if(windows_.begin(), windows_.end(), [native](const auto &entry) {
            return entry.second->window == native;
        });
        if (found == windows_.end()) return;
        auto &window = *found->second;
        if (event == MacWindowEvent::BeginMove || event == MacWindowEvent::BeginResize) {
            if (!windowDrag_.usesCapturedPointer()) beginWindowDrag(window);
        } else if (event == MacWindowEvent::EndResize || event == MacWindowEvent::EndMove) {
            if (windowDrag_.root() == window.name) endWindowDrag();
        } else if ((event == MacWindowEvent::Move || event == MacWindowEvent::Resize) &&
                   windowDrag_.root() == window.name && !windowDrag_.usesCapturedPointer()) {
            SDL_Rect bounds{};
            if (!macWindowBounds(native, bounds)) return;
            const bool bypass = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
            if (event == MacWindowEvent::Move) {
                bounds = moveWindowDrag(window, bounds, bypass);
                moveMacWindow(native, bounds.x, bounds.y);
            } else if (event == MacWindowEvent::Resize) {
                const auto origin = windowDrag_.members().at(window.name);
                unsigned edges = 0;
                if (bounds.w != origin.w || bounds.x != origin.x) edges |= bounds.x != origin.x ? layout::left : layout::right;
                if (bounds.h != origin.h || bounds.y != origin.y) edges |= bounds.y != origin.y ? layout::top : layout::bottom;
                windowDrag_.setResizeEdges(edges, bypass);
                const auto minimum = macWindowMinimumSize(native);
                bounds = windowDrag_.constrainResize(bounds, minimum, windowPeers(window), bypass);
                setMacWindowBounds(native, bounds);
            }
        }
        dirty_ = true;
        if (!windowDrag_.root().empty() && !liveWindowRendering_ && SDL_GetTicks() - lastLiveWindowRender_ >= 30) {
            liveWindowRendering_ = true;
            lastLiveWindowRender_ = SDL_GetTicks();
            try {
                spectrum_ = engine_.spectrumBars(48);
                waveform_ = engine_.waveform(256);
                for (const auto &[name, visible]: windows_) {
                    static_cast<void>(name);
                    if (visible->visible() && !(SDL_GetWindowFlags(visible->window) & SDL_WINDOW_MINIMIZED)) render(*visible);
                }
            } catch (...) {
                liveWindowRendering_ = false;
                throw;
            }
            liveWindowRendering_ = false;
        }
    }
}
