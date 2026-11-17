#include "app/WindowsTray.hpp"
#include "app/Data.hpp"

#include <exception>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>

namespace ttplayer::app {
    struct WindowsTray::State {
        HWND window = nullptr;
        NOTIFYICONDATAW icon{};
        HICON loadedIcon = nullptr;
        HMENU menu = nullptr;
        bool added = false;
        UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        std::vector<std::string> commands;
        std::function<void(const std::string &)> action;
        static constexpr UINT notification = WM_APP + 31;

        void updateIcon() {
            auto replacement = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1),
                                                            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                            GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
            if (!replacement) return;
            const auto previous = loadedIcon;
            icon.hIcon = loadedIcon = replacement;
            if (added) Shell_NotifyIconW(NIM_MODIFY, &icon);
            if (previous) DestroyIcon(previous);
        }

        void addIcon() {
            added = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
            if (added) {
                icon.uVersion = NOTIFYICON_VERSION_4;
                Shell_NotifyIconW(NIM_SETVERSION, &icon);
            }
        }

        static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM parameter, LPARAM data) {
            auto *state = reinterpret_cast<State *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE) {
                state = static_cast<State *>(reinterpret_cast<CREATESTRUCTW *>(data)->lpCreateParams);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            }
            if (!state) return DefWindowProcW(window, message, parameter, data);
            if (message == notification) {
                const auto event = LOWORD(data);
                if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) {
                    PostMessageW(window, WM_APP + 32, 0, 0);
                } else if (event == WM_CONTEXTMENU) {
                    POINT point{GET_X_LPARAM(parameter), GET_Y_LPARAM(parameter)};
                    if (point.x == -1 && point.y == -1) GetCursorPos(&point);
                    SetForegroundWindow(window);
                    const auto selected = TrackPopupMenuEx(state->menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x,
                                                           point.y, window, nullptr);
                    PostMessageW(window, WM_NULL, 0, 0);
                    if (selected > 0 && static_cast<std::size_t>(selected) <= state->commands.size()) state->dispatch(
                        state->commands[selected - 1]);
                }
                return 0;
            }
            if (message == WM_APP + 32) {
                state->dispatch("show");
                return 0;
            }
            if (state->taskbarCreated != 0 && message == state->taskbarCreated) {
                state->added = false;
                state->updateIcon();
                state->addIcon();
                return 0;
            }
            if (message == WM_SETTINGCHANGE || message == WM_DISPLAYCHANGE || message == WM_DPICHANGED) {
                if (state->icon.cbSize != 0) state->updateIcon();
                return 0;
            }
            return DefWindowProcW(window, message, parameter, data);
        }

        void dispatch(const std::string &command) {
            try { action(command); } catch (const std::exception &error) { OutputDebugStringA(error.what()); }
        }
    };

    WindowsTray::WindowsTray(const std::vector<std::pair<std::string, std::string> > &items,
                             std::function<void(const std::string &)> action)
        : state_(std::make_unique<State>()) {
        state_->action = std::move(action);
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW definition{};
        definition.hInstance = instance;
        definition.lpfnWndProc = State::procedure;
        definition.lpszClassName = L"TTPlayerCpp.Notification";
        if (!RegisterClassW(&definition) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        state_->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, definition.lpszClassName,
                                         L"TTPlayer notification", WS_POPUP,
                                         0, 0, 0, 0, nullptr, nullptr, instance, state_.get());
        if (!state_->window) return;
        state_->menu = CreatePopupMenu();
        if (!state_->menu) return;
        for (const auto &[command, label]: items) {
            state_->commands.push_back(command);
            AppendMenuW(state_->menu, MF_STRING, state_->commands.size(), fromUtf8(label).c_str());
        }
        state_->icon.cbSize = sizeof(state_->icon);
        state_->icon.hWnd = state_->window;
        state_->icon.uID = 1;
        state_->icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        state_->icon.uCallbackMessage = State::notification;
        state_->updateIcon();
        if (!state_->icon.hIcon) state_->icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
        lstrcpynW(state_->icon.szTip, L"TTPlayer C++", ARRAYSIZE(state_->icon.szTip));
        state_->addIcon();
    }

    WindowsTray::~WindowsTray() {
        if (state_->added) Shell_NotifyIconW(NIM_DELETE, &state_->icon);
        if (state_->window) DestroyWindow(state_->window);
        if (state_->menu) DestroyMenu(state_->menu);
        if (state_->loadedIcon) DestroyIcon(state_->loadedIcon);
    }

    bool WindowsTray::available() const { return state_->added; }

    void WindowsTray::setTooltip(const std::string &text) {
        if (!state_->added) return;
        const auto wide = fromUtf8(text).wstring();
        lstrcpynW(state_->icon.szTip, wide.c_str(), ARRAYSIZE(state_->icon.szTip));
        Shell_NotifyIconW(NIM_MODIFY, &state_->icon);
    }
}
