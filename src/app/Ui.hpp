#pragma once

#include "app/Skin.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <functional>
#include <optional>
#include <unordered_map>

namespace ttplayer::app {
    struct Hit {
        SDL_FRect bounds;
        std::function<void()> click;
        std::function<void(float)> slide;
        bool vertical = false;
        SDL_FRect dragBounds{};
        int songIndex = -1;
        bool lyric = false;
    };

    class Fonts final {
    public:
        ~Fonts();

        void locate(const std::filesystem::path &assets);

        TTF_Font *get(int size, std::string_view family = {});

    private:
        std::filesystem::path path_;
        std::map<int, TTF_Font *> fonts_;
        std::map<std::string, TTF_Font *> familyFonts_;
        std::map<int, std::vector<TTF_Font *> > fallbackFonts_;
    };

    class UiWindow final {
    public:
        UiWindow(std::string name, std::string title, int width, int height, SDL_WindowFlags flags, Fonts &fonts,
                 SDL_Window *parent = nullptr, SDL_Point offset = {});

        ~UiWindow();

        UiWindow(const UiWindow &) = delete;

        UiWindow &operator=(const UiWindow &) = delete;

        void begin(int logicalWidth = 0, int logicalHeight = 0, bool transparent = false);

        void fill(SDL_FRect rect, SDL_Color color);

        void roundedRect(SDL_FRect rect, SDL_Color color, float radius);

        void tip(SDL_FRect bounds, std::string label);

        void renderTooltip();

        void carouselText(SDL_FRect bounds, const std::vector<std::string> &values, int size, SDL_Color color,
                          std::string_view family, std::string_view alignment);

        void outline(SDL_FRect rect, SDL_Color color);

        void text(std::string_view value, float left, float top, int size = 16, SDL_Color color = {224, 234, 246, 255},
                  float maximumWidth = 0, bool centered = false, std::string_view family = {});

        SDL_FPoint measure(std::string_view value, int size, std::string_view family = {});

        void karaoke(std::string_view value, SDL_FRect bounds, float ratio, int size,
                     SDL_Color base = {219, 232, 245, 255}, SDL_Color highlight = {78, 224, 181, 255},
                     std::string_view family = {});

        void button(SDL_FRect bounds, std::string_view label, std::function<void()> action, bool selected = false);

        void slider(SDL_FRect bounds, float value, std::function<void(float)> change, bool vertical = false);

        void bitmap(SDL_Surface *surface, SDL_FRect target, const SDL_FRect *source = nullptr);

        void skinBackground(const Skin &skin, const SkinWindow &definition);

        void visual(SDL_FRect bounds, const std::vector<float> &bars, int mode, const std::vector<float> &waveform = {},
                    const XmlNode *style = nullptr);

        void classicVisual(SDL_FRect bounds, const std::vector<float> &bars, int mode,
                           const std::vector<float> &waveform, const XmlNode &style);

        bool visible() const;

        void show(bool visible);

        void deferPresentation();

        void commitPresentation();

        void clearCache();

        SDL_FPoint pointer() const;

        std::string name;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        int width = 0, height = 0;
        std::vector<Hit> hits;
        float mouseX = -1, mouseY = -1;
        int scroll = 0;

    private:
        struct Texture {
            SDL_Texture *value;
            int width, height;
        };

        Fonts &fonts_;
        std::optional<bool> pendingVisibility_;
        std::unordered_map<std::string, Texture> textCache_;
        std::map<SDL_Surface *, SDL_Texture *> imageCache_;
        SDL_Texture *visualTexture_ = nullptr;
        std::vector<Uint32> visualPixels_;
        std::vector<float> visualPeaks_;
        int visualWidth_ = 0, visualHeight_ = 0, visualMode_ = -1;

        struct Star {
            float baseX = 0, baseY = 0, depth = 0, phase = 0;
            int band = 0;
        };

        std::vector<Star> stars_;
        std::uint64_t starRandom_ = 42ULL ^ 0x5deece66dULL;

        void starVisual(const std::vector<float> &bars);

        std::vector<std::pair<SDL_FRect, std::string> > tips_;
        std::string hoverTip_;
        Uint64 hoverSince_ = 0;
        std::vector<std::string> carouselValues_;
        std::size_t carouselIndex_ = 0;
        int carouselPhase_ = 0;
        Uint64 carouselSince_ = 0;
    };

    bool contains(SDL_FRect rect, float left, float top);

    SDL_HitTestResult windowHitTest(UiWindow &window, SDL_Point point);
}
