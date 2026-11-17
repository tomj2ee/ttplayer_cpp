#pragma once

#include "app/Ui.hpp"

#include <memory>

namespace ttplayer::app {
    struct MenuEntry {
        std::string label, action;
        bool checked = false;
        std::vector<MenuEntry> children{};
    };

    class PopupMenu final {
    public:
        ~PopupMenu();

        void open(UiWindow &owner, Fonts &fonts, std::vector<MenuEntry> entries, float left, float top);

        void close();

        bool isOpen() const;

        bool handleEvent(const SDL_Event &event);

        void update();

        void raise();

        std::optional<std::string> takeAction();

    private:
        struct Level {
            std::unique_ptr<UiWindow> view;
            std::vector<MenuEntry> entries;
            int selected = -1, first = 0, rows = 0, opened = -1;
            float inset = 4;
        };

        void append(SDL_Window *parent, std::vector<MenuEntry> entries, SDL_Point offset);

        void trim(std::size_t count);

        void draw(Level &level);

        void select(std::size_t depth, int index, bool hover);

        void navigate(std::size_t depth, int direction);

        void activate(std::size_t depth);

        void expand(std::size_t depth, bool keyboard);

        int rowAt(const Level &level, float left, float top) const;

        bool owns(SDL_Window *window) const;

        Fonts *fonts_ = nullptr;
        SDL_Window *owner_ = nullptr;
        std::vector<std::unique_ptr<Level> > levels_;
        std::optional<std::string> action_;
        std::size_t hoverDepth_ = 0;
        Uint64 hoverAt_ = 0, openedAt_ = 0;
    };
}
