#pragma once

#include "app/WindowLayout.hpp"

#include <map>
#include <set>
#include <string>

namespace ttplayer::app {
    class WindowDragSession final {
    public:
        using WindowBounds = std::map<std::string, SDL_Rect>;

        enum class PointerMode { None, Captured, Native };

        void begin(std::string root, SDL_Rect bounds, const WindowBounds &candidates, std::vector<SDL_Rect> workAreas);

        void reset();

        void capturePointer(SDL_FPoint origin, PointerMode mode);

        void setResizeEdges(unsigned edges, bool bypass = false);

        [[nodiscard]] const std::string &root() const;

        [[nodiscard]] const WindowBounds &members() const;

        [[nodiscard]] bool contains(std::string_view name) const;

        [[nodiscard]] bool usesCapturedPointer() const;

        [[nodiscard]] bool usesNativePointer() const;

        [[nodiscard]] unsigned resizeEdges() const;

        [[nodiscard]] SDL_Rect pointerBounds(SDL_FPoint position) const;

        SDL_Rect constrainMove(SDL_Rect proposed, const std::set<std::string> &activeMembers,
                               const std::vector<SDL_Rect> &peers, bool bypass);

        SDL_Rect constrainResize(SDL_Rect proposed, SDL_Point minimum, const std::vector<SDL_Rect> &peers, bool bypass);

    private:
        std::string root_;
        WindowBounds members_;
        std::vector<SDL_Rect> workAreas_;
        layout::SnapLocks locks_;
        unsigned resizeEdges_ = 0;
        PointerMode pointerMode_ = PointerMode::None;
        SDL_FPoint pointerOrigin_{};
    };
}
