#pragma once

#include <SDL3/SDL_surface.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ttplayer::app {
    class SystemTray final {
    public:
        using Entries = std::vector<std::pair<std::string, std::string> >;
        using Handler = std::function<void(const std::string &)>;

        SystemTray(SDL_Surface *icon, const Entries &entries, Handler handler);

        ~SystemTray();

        SystemTray(const SystemTray &) = delete;

        SystemTray &operator=(const SystemTray &) = delete;

        bool available() const;

        void setTooltip(const std::string &text);

    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}
