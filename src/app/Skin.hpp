#pragma once

#include "app/Data.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <optional>

namespace ttplayer::app {
    struct SkinControl {
        std::string name;
        SDL_FRect bounds{};
        std::map<std::string, std::string> attributes;

        std::string value(std::string_view key, std::string fallback = {}) const;
    };

    struct SkinWindow {
        std::string image;
        std::vector<SkinControl> controls;
        SDL_FRect resize{};
        SDL_FRect position{};
        bool tiled = false;
        int width = 0, height = 0, interval = 0;

        const SkinControl *control(std::string_view name) const;
    };

    class Skin final {
    public:
        void load(const std::filesystem::path &path, const std::filesystem::path &fallbackIcon = {});

        [[nodiscard]] const SkinWindow *window(std::string_view name) const;

        [[nodiscard]] SDL_Surface *image(std::string_view name) const;

        [[nodiscard]] SDL_Surface *windowIcon() const { return windowIcon_.get(); }

        [[nodiscard]] const XmlNode &style(std::string_view name) const;

        [[nodiscard]] const std::string &name() const { return name_; }
        [[nodiscard]] const std::string &author() const { return author_; }
        [[nodiscard]] const std::filesystem::path &path() const { return path_; }

    private:
        std::string name_;
        std::string author_;
        std::filesystem::path path_;
        std::map<std::string, SkinWindow> windows_;
        std::map<std::string, std::shared_ptr<SDL_Surface> > images_;
        std::shared_ptr<SDL_Surface> windowIcon_;
        std::map<std::string, XmlNode> styles_;
    };

    SDL_Color parseColor(std::string_view text, SDL_Color fallback = {224, 234, 246, 255});

    SDL_FRect parseRect(std::string text);

    SDL_FRect controlBounds(const SkinWindow &window, const SkinControl &control, int width, int height,
                            float imageWidth = 0, float imageHeight = 0);
}
