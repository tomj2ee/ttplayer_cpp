#include "app/WindowLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ttplayer::app::layout {
    namespace {
        bool overlap(int first, int length, int second, int otherLength, int padding = 0) {
            return first < second + otherLength + padding && first + length > second - padding;
        }

        struct Axis {
            int distance = snapDistance + 1;
            int correction = 0;
            std::optional<int> target;
            bool held = false;

            void consider(int difference, int origin, const std::optional<int> &locked = {}) {
                const int magnitude = std::abs(difference);
                const bool hold = locked && origin + difference == *locked && magnitude <= releaseDistance;
                if ((hold && !held) || (hold == held && (magnitude < distance ||
                                                         (magnitude == distance && difference < correction)))) {
                    if (!hold && magnitude > snapDistance) return;
                    distance = magnitude;
                    correction = difference;
                    target = origin + difference;
                    held = hold;
                }
            }
        };
    }

    bool coreWindow(const std::string_view name) {
        return name == "player" || name == "playlist" || name == "lyric" || name == "eq" || name == "mini";
    }

    bool docked(const SDL_Rect first, const SDL_Rect second) {
        return (overlap(first.y, first.h, second.y, second.h) &&
                (std::abs(first.x + first.w - second.x) <= dockDistance || std::abs(first.x - second.x - second.w) <=
                 dockDistance)) ||
               (overlap(first.x, first.w, second.x, second.w) &&
                (std::abs(first.y + first.h - second.y) <= dockDistance || std::abs(first.y - second.y - second.h) <=
                 dockDistance));
    }

    SDL_Point snapMove(const std::vector<SDL_Rect> &moving, const std::vector<SDL_Rect> &peers,
                       const std::vector<SDL_Rect> &workAreas, SnapLocks &locks) {
        if (moving.empty()) {
            locks = {};
            return {};
        }
        const auto root = moving.front();
        Axis horizontal, vertical;
        auto envelope = root;
        for (const auto bounds: moving) {
            const int farRight = std::max(envelope.x + envelope.w, bounds.x + bounds.w);
            const int farBottom = std::max(envelope.y + envelope.h, bounds.y + bounds.h);
            envelope.x = std::min(envelope.x, bounds.x);
            envelope.y = std::min(envelope.y, bounds.y);
            envelope.w = farRight - envelope.x;
            envelope.h = farBottom - envelope.y;
            for (const auto peer: peers) {
                if (overlap(bounds.y, bounds.h, peer.y, peer.h, releaseDistance)) {
                    for (const int difference: {
                             peer.x - bounds.x, peer.x + peer.w - bounds.x - bounds.w,
                             peer.x + peer.w - bounds.x, peer.x - bounds.x - bounds.w
                         })
                        horizontal.consider(difference, root.x, locks.horizontal);
                }
                if (overlap(bounds.x, bounds.w, peer.x, peer.w, releaseDistance)) {
                    for (const int difference: {
                             peer.y - bounds.y, peer.y + peer.h - bounds.y - bounds.h,
                             peer.y + peer.h - bounds.y, peer.y - bounds.y - bounds.h
                         })
                        vertical.consider(difference, root.y, locks.vertical);
                }
            }
        }
        for (const auto area: workAreas) {
            if (overlap(envelope.y, envelope.h, area.y, area.h)) {
                horizontal.consider(area.x - envelope.x, root.x, locks.horizontal);
                horizontal.consider(area.x + area.w - envelope.x - envelope.w, root.x, locks.horizontal);
            }
            if (overlap(envelope.x, envelope.w, area.x, area.w)) {
                vertical.consider(area.y - envelope.y, root.y, locks.vertical);
                vertical.consider(area.y + area.h - envelope.y - envelope.h, root.y, locks.vertical);
            }
        }
        locks.horizontal = horizontal.target;
        locks.vertical = vertical.target;
        return {horizontal.correction, vertical.correction};
    }

    SDL_Rect snapResize(SDL_Rect bounds, const unsigned edges, const SDL_Point minimum,
                        const std::vector<SDL_Rect> &peers, const std::vector<SDL_Rect> &workAreas, SnapLocks *locks) {
        Axis horizontal, vertical;
        const int movingX = edges & left ? bounds.x : bounds.x + bounds.w;
        const int movingY = edges & top ? bounds.y : bounds.y + bounds.h;
        auto considerX = [&](const int target) {
            const int width = edges & left ? bounds.x + bounds.w - target : target - bounds.x;
            if (width >= minimum.x) horizontal.consider(target - movingX, movingX,
                                                        locks ? locks->horizontal : std::optional<int>{});
        };
        auto considerY = [&](const int target) {
            const int height = edges & top ? bounds.y + bounds.h - target : target - bounds.y;
            if (height >= minimum.y) vertical.consider(target - movingY, movingY,
                                                       locks ? locks->vertical : std::optional<int>{});
        };
        auto consider = [&](const SDL_Rect target) {
            if (overlap(bounds.y, bounds.h, target.y, target.h, releaseDistance)) {
                considerX(target.x);
                considerX(target.x + target.w);
            }
            if (overlap(bounds.x, bounds.w, target.x, target.w, releaseDistance)) {
                considerY(target.y);
                considerY(target.y + target.h);
            }
        };
        for (const auto peer: peers) consider(peer);
        for (const auto area: workAreas) consider(area);
        if (locks) {
            locks->horizontal = edges & (left | right) ? horizontal.target : std::optional<int>{};
            locks->vertical = edges & (top | bottom) ? vertical.target : std::optional<int>{};
        }
        if (edges & left) {
            bounds.x += horizontal.correction;
            bounds.w -= horizontal.correction;
        } else if (edges & right) bounds.w += horizontal.correction;
        if (edges & top) {
            bounds.y += vertical.correction;
            bounds.h -= vertical.correction;
        } else if (edges & bottom) bounds.h += vertical.correction;
        if (bounds.w < minimum.x) {
            if (edges & left) bounds.x -= minimum.x - bounds.w;
            bounds.w = minimum.x;
        }
        if (bounds.h < minimum.y) {
            if (edges & top) bounds.y -= minimum.y - bounds.h;
            bounds.h = minimum.y;
        }
        return bounds;
    }

    SDL_Rect recover(SDL_Rect bounds, const std::vector<SDL_Rect> &workAreas) {
        if (workAreas.empty()) return bounds;
        const int titleHeight = std::min(26, bounds.h), titleWidth = std::min(80, bounds.w);
        auto nearest = workAreas.front();
        double bestDistance = std::numeric_limits<double>::max();
        for (const auto area: workAreas) {
            if (bounds.y >= area.y && bounds.y + titleHeight <= area.y + area.h &&
                std::min(bounds.x + bounds.w, area.x + area.w) - std::max(bounds.x, area.x) >= titleWidth)
                return bounds;
            const double deltaX = bounds.x + bounds.w / 2.0 - std::clamp(
                                      bounds.x + bounds.w / 2.0, double(area.x), double(area.x + area.w));
            const double deltaY = bounds.y - std::clamp(bounds.y, area.y, area.y + area.h);
            const double distance = deltaX * deltaX + deltaY * deltaY;
            if (distance < bestDistance) {
                bestDistance = distance;
                nearest = area;
            }
        }
        bounds.x = std::clamp(bounds.x, nearest.x, nearest.x + std::max(0, nearest.w - bounds.w));
        bounds.y = std::clamp(bounds.y, nearest.y, nearest.y + std::max(0, nearest.h - bounds.h));
        return bounds;
    }
}
