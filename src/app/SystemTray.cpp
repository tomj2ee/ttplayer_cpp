#include "app/SystemTray.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

#ifdef _WIN32
#include "app/WindowsTray.hpp"
#endif

namespace ttplayer::app {
#ifdef _WIN32
    struct SystemTray::State {
        WindowsTray tray;

        State(SDL_Surface *, const Entries &entries, Handler handler) : tray(entries, std::move(handler)) {
        }

        bool available() const { return tray.available(); }
        void setTooltip(const std::string &text) { tray.setTooltip(text); }
    };
#else
    struct SystemTray::State {
        struct Action {
            State *owner;
            std::string command;
        };

        SDL_Tray *tray = nullptr;
        Handler handler;
        std::vector<std::unique_ptr<Action> > actions;

        State(SDL_Surface *icon, const Entries &entries, Handler callback) : handler(std::move(callback)) {
            tray = SDL_CreateTray(icon, "TTPlayer C++");
            if (!tray) return;
            auto *menu = SDL_CreateTrayMenu(tray);
            if (!menu) {
                SDL_DestroyTray(tray);
                tray = nullptr;
                return;
            }
            try {
                for (const auto &[command, label]: entries) {
                    auto action = std::make_unique<Action>(Action{this, command});
                    auto *entry = SDL_InsertTrayEntryAt(menu, -1, label.c_str(), SDL_TRAYENTRY_BUTTON);
                    if (!entry) throw std::runtime_error(SDL_GetError());
                    actions.push_back(std::move(action));
                    SDL_SetTrayEntryCallback(entry, [](void *userdata, SDL_TrayEntry *) {
                        auto &action = *static_cast<Action *>(userdata);
                        try { action.owner->handler(action.command); } catch (const std::exception &error) {
                            SDL_Log("Tray command: %s", error.what());
                        }
                    }, actions.back().get());
                }
            } catch (...) {
                SDL_DestroyTray(tray);
                tray = nullptr;
                throw;
            }
        }

        ~State() { if (tray) SDL_DestroyTray(tray); }
        bool available() const { return tray != nullptr; }
        void setTooltip(const std::string &text) { if (tray) SDL_SetTrayTooltip(tray, text.c_str()); }
    };
#endif

    SystemTray::SystemTray(SDL_Surface *icon, const Entries &entries, Handler handler)
        : state_(std::make_unique<State>(icon, entries, std::move(handler))) {
    }

    SystemTray::~SystemTray() = default;

    bool SystemTray::available() const { return state_->available(); }
    void SystemTray::setTooltip(const std::string &text) { state_->setTooltip(text); }
}
