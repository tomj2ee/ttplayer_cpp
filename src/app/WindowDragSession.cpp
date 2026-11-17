#include "app/WindowDragSession.hpp"

#include <cmath>
#include <utility>

namespace ttplayer::app {
    void WindowDragSession::begin(std::string root, const SDL_Rect bounds, const WindowBounds &candidates,
                                  std::vector<SDL_Rect> workAreas) {
        reset();
        root_ = std::move(root);
        workAreas_ = std::move(workAreas);
        members_.emplace(root_, bounds);
        if (root_ != "player") return;
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto &[name, candidate]: candidates) {
                if (contains(name) || !layout::coreWindow(name)) continue;
                for (const auto &[anchor, target]: members_) {
                    static_cast<void>(anchor);
                    if (!layout::docked(candidate, target)) continue;
                    members_.emplace(name, candidate);
                    changed = true;
                    break;
                }
            }
        }
    }

    void WindowDragSession::reset() {
        root_.clear();
        members_.clear();
        workAreas_.clear();
        locks_ = {};
        resizeEdges_ = 0;
        pointerMode_ = PointerMode::None;
        pointerOrigin_ = {};
    }

    void WindowDragSession::capturePointer(const SDL_FPoint origin, const PointerMode mode) {
        pointerOrigin_ = origin;
        pointerMode_ = mode;
    }

    void WindowDragSession::setResizeEdges(const unsigned edges, const bool bypass) {
        if (resizeEdges_ != edges || bypass) locks_ = {};
        resizeEdges_ = edges;
        if (edges && contains(root_)) {
            const auto origin = members_.at(root_);
            members_ = {{root_, origin}};
        }
    }

    const std::string &WindowDragSession::root() const { return root_; }
    const WindowDragSession::WindowBounds &WindowDragSession::members() const { return members_; }
    bool WindowDragSession::contains(const std::string_view name) const { return members_.contains(std::string(name)); }
    bool WindowDragSession::usesCapturedPointer() const { return pointerMode_ == PointerMode::Captured; }
    bool WindowDragSession::usesNativePointer() const { return pointerMode_ == PointerMode::Native; }
    unsigned WindowDragSession::resizeEdges() const { return resizeEdges_; }

    SDL_Rect WindowDragSession::pointerBounds(const SDL_FPoint position) const {
        const auto origin = members_.find(root_);
        if (origin == members_.end()) return {};
        auto proposed = origin->second;
        const int deltaX = static_cast<int>(std::lround(position.x - pointerOrigin_.x));
        const int deltaY = static_cast<int>(std::lround(position.y - pointerOrigin_.y));
        if (!resizeEdges_) {
            proposed.x += deltaX;
            proposed.y += deltaY;
            return proposed;
        }
        if (resizeEdges_ &layout::left) {
            proposed.x += deltaX;
            proposed.w -= deltaX;
        }
        if (resizeEdges_ &layout::right) proposed.w += deltaX;
        if (resizeEdges_ &layout::top) {
            proposed.y += deltaY;
            proposed.h -= deltaY;
        }
        if (resizeEdges_ &layout::bottom) proposed.h += deltaY;
        return proposed;
    }

    SDL_Rect WindowDragSession::constrainMove(SDL_Rect proposed, const std::set<std::string> &activeMembers,
                                              const std::vector<SDL_Rect> &peers, const bool bypass) {
        if (!contains(root_)) return proposed;
        const auto origin = members_.at(root_);
        const int deltaX = proposed.x - origin.x, deltaY = proposed.y - origin.y;
        std::vector<SDL_Rect> moving{proposed};
        for (const auto &[name, bounds]: members_) {
            if (name == root_ || !activeMembers.contains(name)) continue;
            moving.push_back({bounds.x + deltaX, bounds.y + deltaY, bounds.w, bounds.h});
        }
        if (bypass) {
            locks_ = {};
            return proposed;
        }
        const auto correction = layout::snapMove(moving, peers, workAreas_, locks_);
        proposed.x += correction.x;
        proposed.y += correction.y;
        return proposed;
    }

    SDL_Rect WindowDragSession::constrainResize(const SDL_Rect proposed, const SDL_Point minimum,
                                                const std::vector<SDL_Rect> &peers, const bool bypass) {
        if (bypass) locks_ = {};
        return layout::snapResize(proposed, resizeEdges_, minimum, bypass ? std::vector<SDL_Rect>{} : peers,
                                  bypass ? std::vector<SDL_Rect>{} : workAreas_, &locks_);
    }
}
