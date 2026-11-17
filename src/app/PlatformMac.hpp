#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace ttplayer::app {
    SDL_Surface *loadMacIcon(std::string_view content, int size = 16);

    TTF_Font *openMacFont(std::string_view family, int size);

    void raiseMacPopup(SDL_Window *window);

    void configureMacDesktop(SDL_Window *window, bool locked);

    bool macWindowBounds(SDL_Window *window, SDL_Rect &bounds);

    bool moveMacWindow(SDL_Window *window, int left, int top);

    void setMacWindowBounds(SDL_Window *window, SDL_Rect bounds);

    SDL_Point macWindowMinimumSize(SDL_Window *window);

    enum class MacWindowEvent { BeginMove, Move, BeginResize, Resize, EndResize, EndMove, Tick };

    class MacApplication final {
    public:
        MacApplication(Uint32 reopenEvent, std::function<void(SDL_Window *, MacWindowEvent)> windowEvent);
        ~MacApplication();
    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}
