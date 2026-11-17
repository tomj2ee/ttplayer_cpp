#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ttplayer::app {
    class WindowsTray final {
    public:
        WindowsTray(const std::vector<std::pair<std::string, std::string> > &items,
                    std::function<void(const std::string &)> action);

        ~WindowsTray();

        bool available() const;

        void setTooltip(const std::string &text);

    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}
