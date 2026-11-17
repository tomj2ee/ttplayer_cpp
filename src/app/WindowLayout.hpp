#pragma once

#include <SDL3/SDL_rect.h>

#include <optional>
#include <string_view>
#include <vector>

namespace ttplayer::app::layout {
    inline constexpr int snapDistance = 10;
    inline constexpr int releaseDistance = 16;
    inline constexpr int dockDistance = 2;

    struct SnapLocks {
        std::optional<int> horizontal, vertical;
    };

    enum ResizeEdge { left = 1, right = 2, top = 4, bottom = 8 };

    bool coreWindow(std::string_view name);

    bool docked(SDL_Rect first, SDL_Rect second);

    SDL_Point snapMove(const std::vector<SDL_Rect> &moving, const std::vector<SDL_Rect> &peers,
                       const std::vector<SDL_Rect> &workAreas, SnapLocks &locks);

    SDL_Rect snapResize(SDL_Rect bounds, unsigned edges, SDL_Point minimum,
                        const std::vector<SDL_Rect> &peers, const std::vector<SDL_Rect> &workAreas,
                        SnapLocks *locks = nullptr);

    SDL_Rect recover(SDL_Rect bounds, const std::vector<SDL_Rect> &workAreas);
}
