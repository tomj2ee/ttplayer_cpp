#include "app/Ui.hpp"
#ifdef __APPLE__
#include "app/PlatformMac.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace ttplayer::app {
    bool contains(const SDL_FRect rect, const float left, const float top) {
        return left >= rect.x && top >= rect.y && left < rect.x + rect.w && top < rect.y + rect.h;
    }

    SDL_HitTestResult windowHitTest(UiWindow &window, const SDL_Point point) {
        float left = 0, top = 0;
        SDL_RenderCoordinatesFromWindow(window.renderer, static_cast<float>(point.x), static_cast<float>(point.y),
                                        &left, &top);
        if (window.name != "desktop" && (SDL_GetWindowFlags(window.window) & SDL_WINDOW_RESIZABLE)) {
            int width = 0, height = 0;
            SDL_GetWindowSize(window.window, &width, &height);
            const bool nearLeft = point.x < 6, nearRight = point.x >= width - 6;
            const bool nearTop = point.y < 6, nearBottom = point.y >= height - 6;
            if (nearTop && nearLeft) return SDL_HITTEST_RESIZE_TOPLEFT;
            if (nearTop && nearRight) return SDL_HITTEST_RESIZE_TOPRIGHT;
            if (nearBottom && nearLeft) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
            if (nearBottom && nearRight) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
            if (nearLeft) return SDL_HITTEST_RESIZE_LEFT;
            if (nearRight) return SDL_HITTEST_RESIZE_RIGHT;
            if (nearTop) return SDL_HITTEST_RESIZE_TOP;
            if (nearBottom) return SDL_HITTEST_RESIZE_BOTTOM;
        }
        for (const auto &hit: window.hits) if (contains(hit.bounds, left, top)) return SDL_HITTEST_NORMAL;
        if (window.name == "desktop") return SDL_HITTEST_DRAGGABLE;
        return top < 26 ? SDL_HITTEST_DRAGGABLE : SDL_HITTEST_NORMAL;
    }

    Fonts::~Fonts() {
        for (const auto &[key, font]: familyFonts_) {
            static_cast<void>(key);
            TTF_CloseFont(font);
        }
        for (const auto &[size, font]: fonts_) {
            static_cast<void>(size);
            TTF_CloseFont(font);
        }
        for (const auto &[size, fonts]: fallbackFonts_) {
            static_cast<void>(size);
            for (auto *font: fonts) TTF_CloseFont(font);
        }
    }

    void Fonts::locate(const std::filesystem::path &assets) {
        std::vector<std::filesystem::path> candidates{assets / "font.ttf", assets / "font.ttc"};
#ifdef _WIN32
        const char *windows = SDL_getenv("WINDIR");
        const auto directory = fromUtf8(windows ? windows : "C:/Windows") / "Fonts";
        for (const auto *name: {"msyh.ttc", "msyh.ttf", "simhei.ttf", "simsun.ttc", "arial.ttf"}) candidates.push_back(
            directory / name);
#elif defined(__APPLE__)
        for (const auto *name: {
                 "/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Supplemental/Songti.ttc",
                 "/System/Library/Fonts/STHeiti Light.ttc", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
                 "/System/Library/Fonts/Helvetica.ttc"
             })
            candidates.emplace_back(name);
#else
        for (const auto *name: {
                 "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                 "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/System/Library/Fonts/PingFang.ttc"
             }) candidates.emplace_back(name);
#endif
        for (const auto &candidate: candidates) {
            if (std::filesystem::is_regular_file(candidate)) {
                path_ = candidate;
                if (get(16)) return;
            }
        }
        throw std::runtime_error("No usable font found; place a CJK font as assets/font.ttf");
    }

    TTF_Font *Fonts::get(const int size, const std::string_view family) {
        const int clamped = std::clamp(size, 8, 72);
#ifdef _WIN32
        if (!family.empty()) {
            const auto name = lower(std::string(family));
            const std::map<std::string, std::string> systemFonts{
                {"simsun", "simsun.ttc"}, {"宋体", "simsun.ttc"}, {"tahoma", "tahoma.ttf"}, {"arial", "arial.ttf"},
                {"microsoft yahei", "msyh.ttc"}, {"微软雅黑", "msyh.ttc"}, {"simhei", "simhei.ttf"}
            };
            const auto file = systemFonts.find(name);
            if (file != systemFonts.end()) {
                const auto key = file->second + ":" + std::to_string(clamped);
                if (const auto existing = familyFonts_.find(key); existing != familyFonts_.end()) return existing->
                        second;
                const char *windows = SDL_getenv("WINDIR");
                const auto path = fromUtf8(windows ? windows : "C:/Windows") / "Fonts" / file->second;
                if (auto *font = TTF_OpenFont(utf8(path).c_str(), static_cast<float>(clamped))) {
                    if (auto *fallback = get(clamped)) TTF_AddFallbackFont(font, fallback);
                    familyFonts_[key] = font;
                    return font;
                }
            }
        }
#elif defined(__APPLE__)
        if (!family.empty()) {
            const auto key = std::string(family) + ":" + std::to_string(clamped);
            if (const auto existing = familyFonts_.find(key); existing != familyFonts_.end()) return existing->second;
            if (auto *font = openMacFont(family, clamped)) {
                if (auto *fallback = get(clamped)) TTF_AddFallbackFont(font, fallback);
                familyFonts_[key] = font;
                return font;
            }
        }
#endif
        if (const auto found = fonts_.find(clamped); found != fonts_.end()) return found->second;
        auto *font = TTF_OpenFont(utf8(path_).c_str(), static_cast<float>(clamped));
        if (font) {
            fonts_[clamped] = font;
#ifdef _WIN32
            const char *windows = SDL_getenv("WINDIR");
            const auto directory = fromUtf8(windows ? windows : "C:/Windows") / "Fonts";
            for (const auto *file: {"malgun.ttf", "msgothic.ttc", "meiryo.ttc"}) {
                const auto path = directory / file;
                if (!std::filesystem::is_regular_file(path)) continue;
                if (auto *fallback = TTF_OpenFont(utf8(path).c_str(), static_cast<float>(clamped))) {
                    TTF_AddFallbackFont(font, fallback);
                    fallbackFonts_[clamped].push_back(fallback);
                }
            }
#elif defined(__APPLE__)
            for (const auto *family: {"Hiragino Sans", "Apple SD Gothic Neo", "Arial Unicode MS"}) {
                if (auto *fallback = openMacFont(family, clamped)) {
                    TTF_AddFallbackFont(font, fallback);
                    fallbackFonts_[clamped].push_back(fallback);
                }
            }
#endif
        }
        return font;
    }

    UiWindow::UiWindow(std::string nameValue, std::string title, const int initialWidth, const int initialHeight,
                       const SDL_WindowFlags flags, Fonts &fonts, SDL_Window *parent, const SDL_Point offset)
        : name(std::move(nameValue)), fonts_(fonts) {
        const auto creationFlags = flags | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
        if (parent) {
            window = SDL_CreatePopupWindow(parent, offset.x, offset.y, initialWidth, initialHeight, creationFlags);
            if (!window) throw std::runtime_error(SDL_GetError());
            renderer = SDL_CreateRenderer(window, nullptr);
            if (!renderer) {
                const std::string error = SDL_GetError();
                SDL_DestroyWindow(window);
                window = nullptr;
                throw std::runtime_error(error);
            }
        } else if (!SDL_CreateWindowAndRenderer(title.c_str(), initialWidth, initialHeight, creationFlags, &window,
                                                &renderer)) throw std::runtime_error(SDL_GetError());
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        if (!parent) SDL_SetWindowMinimumSize(window, std::min(initialWidth, 280), std::min(initialHeight, 100));
    }

    UiWindow::~UiWindow() {
        clearCache();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }

    void UiWindow::clearCache() {
        if (visualTexture_) SDL_DestroyTexture(visualTexture_);
        visualTexture_ = nullptr;
        visualPixels_.clear();
        visualPeaks_.clear();
        visualMode_ = -1;
        stars_.clear();
        starRandom_ = 42ULL ^ 0x5deece66dULL;
        carouselValues_.clear();
        for (const auto &[key, entry]: textCache_) {
            static_cast<void>(key);
            SDL_DestroyTexture(entry.value);
        }
        for (const auto &[surface, texture]: imageCache_) {
            static_cast<void>(surface);
            SDL_DestroyTexture(texture);
        }
        textCache_.clear();
        imageCache_.clear();
    }

    void UiWindow::begin(const int logicalWidth, const int logicalHeight, const bool transparent) {
        SDL_GetWindowSize(window, &width, &height);
        if (logicalWidth > 0 && logicalHeight > 0) {
            width = logicalWidth;
            height = logicalHeight;
        }
        SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderDrawColor(renderer, 12, 19, 30, transparent ? 0 : 255);
        SDL_RenderClear(renderer);
        hits.clear();
        tips_.clear();
    }

    void UiWindow::fill(const SDL_FRect rect, const SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &rect);
    }

    void UiWindow::outline(const SDL_FRect rect, const SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderRect(renderer, &rect);
    }

    void UiWindow::roundedRect(const SDL_FRect rect, const SDL_Color color, const float radius) {
        const float corner = std::clamp(radius, 0.0F, std::min(rect.w, rect.h) / 2);
        for (int row = 0; row < static_cast<int>(rect.h); ++row) {
            const float center = row + 0.5F;
            const float distance = center < corner
                                       ? corner - center
                                       : center > rect.h - corner
                                             ? center - (rect.h - corner)
                                             : 0;
            const float inset = distance > 0
                                    ? corner - std::sqrt(std::max(0.0F, corner * corner - distance * distance))
                                    : 0;
            fill({rect.x + inset, rect.y + row, rect.w - inset * 2, 1}, color);
        }
    }

    void UiWindow::tip(const SDL_FRect bounds, std::string label) { tips_.emplace_back(bounds, std::move(label)); }

    void UiWindow::renderTooltip() {
        std::string label;
        if (SDL_GetMouseFocus() == window && !(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) {
            for (auto entry = tips_.rbegin(); entry != tips_.rend(); ++entry) if (
                contains(entry->first, mouseX, mouseY)) {
                label = entry->second;
                break;
            }
        }
        if (label != hoverTip_) {
            hoverTip_ = label;
            hoverSince_ = SDL_GetTicks();
        }
        if (label.empty() || SDL_GetTicks() - hoverSince_ < 650) return;
        std::istringstream stream(label);
        std::string line;
        std::vector<std::string> lines;
        float textWidth = 0;
        while (std::getline(stream, line) && lines.size() < 5) {
            textWidth = std::max(textWidth, measure(line, 12).x);
            lines.push_back(line);
        }
        const float tooltipWidth = std::min(static_cast<float>(width - 8), textWidth + 12), tooltipHeight =
                8 + lines.size() * 19;
        const float left = std::clamp(mouseX + 12, 4.0F, std::max(4.0F, width - tooltipWidth - 4));
        const float top = std::clamp(mouseY + 20, 4.0F, std::max(4.0F, height - tooltipHeight - 4));
        const SDL_FRect bounds{left, top, tooltipWidth, tooltipHeight};
        fill(bounds, {255, 255, 225, 255});
        outline(bounds, {118, 118, 118, 255});
        for (std::size_t index = 0; index < lines.size(); ++index) text(lines[index], left + 6, top + 4 + index * 19,
                                                                        12, {25, 25, 25, 255}, tooltipWidth - 12);
    }

    void UiWindow::carouselText(const SDL_FRect bounds, const std::vector<std::string> &values, const int size,
                                const SDL_Color color, const std::string_view family,
                                const std::string_view alignment) {
        if (values.empty()) return;
        const auto now = SDL_GetTicks();
        if (carouselValues_ != values) {
            carouselValues_ = values;
            carouselIndex_ = 0;
            carouselPhase_ = 0;
            carouselSince_ = now;
        }
        const auto next = (carouselIndex_ + 1) % values.size();
        const auto metrics = measure(values[carouselIndex_], size, family);
        Uint64 elapsed = now - carouselSince_;
        if (carouselPhase_ == 0 && elapsed >= 1500) {
            carouselPhase_ = 1;
            carouselSince_ = now;
            elapsed = 0;
        } else if (carouselPhase_ == 1 && elapsed >= static_cast<Uint64>(bounds.h * 50)) {
            carouselIndex_ = next;
            carouselPhase_ = measure(values[next], size, family).x > bounds.w ? 2 : 0;
            carouselSince_ = now;
            elapsed = 0;
        } else if (carouselPhase_ == 2 && elapsed >= static_cast<Uint64>((metrics.x + bounds.w) * 40)) {
            carouselPhase_ = 0;
            carouselSince_ = now;
            elapsed = 0;
        }
        const auto currentMetrics = measure(values[carouselIndex_], size, family);
        SDL_Rect previous{};
        const bool clipped = SDL_RenderClipEnabled(renderer);
        SDL_GetRenderClipRect(renderer, &previous);
        const SDL_Rect clip{
            static_cast<int>(bounds.x), static_cast<int>(bounds.y), static_cast<int>(bounds.w),
            static_cast<int>(bounds.h)
        };
        SDL_SetRenderClipRect(renderer, &clip);
        const float top = bounds.y + (bounds.h - currentMetrics.y) / 2;
        if (carouselPhase_ == 1) {
            const float offset = static_cast<float>(elapsed / 50), nextWidth = measure(values[next], size, family).x;
            text(values[carouselIndex_], bounds.x + (bounds.w - currentMetrics.x) / 2, top - offset, size, color, 0,
                 false, family);
            text(values[next], bounds.x + (bounds.w - nextWidth) / 2, top + bounds.h - offset, size, color, 0, false,
                 family);
        } else if (carouselPhase_ == 2) {
            const float left = bounds.x + bounds.w - static_cast<float>(elapsed / 40);
            text(values[carouselIndex_], left, top, size, color, 0, false, family);
            if (left + currentMetrics.x < bounds.x + bounds.w) text(values[carouselIndex_], left + currentMetrics.x,
                                                                    top, size, color, 0, false, family);
        } else {
            const float left = bounds.x + (alignment.find("right") != std::string_view::npos
                                               ? std::max(0.0F, bounds.w - currentMetrics.x)
                                               : 0);
            text(values[carouselIndex_], left, top, size, color, bounds.x + bounds.w - left,
                 alignment.find("center") != std::string_view::npos, family);
        }
        SDL_SetRenderClipRect(renderer, clipped ? &previous : nullptr);
    }

    SDL_FPoint UiWindow::measure(const std::string_view value, const int size, const std::string_view family) {
        int width = 0, height = 0;
        if (auto *font = fonts_.get(size, family)) TTF_GetStringSize(font, value.data(), value.size(), &width, &height);
        return {static_cast<float>(width), static_cast<float>(height)};
    }

    void UiWindow::text(const std::string_view value, const float left, const float top, const int size,
                        const SDL_Color color, const float maximumWidth, const bool centered,
                        const std::string_view family) {
        if (value.empty()) return;
        const std::string key = std::string(family) + ":" + std::to_string(size) + ":" + std::to_string(color.r) + ":" +
                                std::to_string(color.g) + ":" + std::to_string(color.b) + ":" + std::to_string(color.a)
                                + ":" + std::string(value);
        auto found = textCache_.find(key);
        if (found == textCache_.end()) {
            if (textCache_.size() > 512) {
                for (const auto &[cacheKey, entry]: textCache_) {
                    static_cast<void>(cacheKey);
                    SDL_DestroyTexture(entry.value);
                }
                textCache_.clear();
            }
            auto *font = fonts_.get(size, family);
            auto *surface = font ? TTF_RenderText_Blended(font, value.data(), value.size(), color) : nullptr;
            if (!surface) return;
            auto *texture = SDL_CreateTextureFromSurface(renderer, surface);
            const Texture entry{texture, surface->w, surface->h};
            SDL_DestroySurface(surface);
            if (!texture) return;
            found = textCache_.emplace(key, entry).first;
        }
        const auto &entry = found->second;
        SDL_FRect target{left, top, static_cast<float>(entry.width), static_cast<float>(entry.height)};
        if (centered && maximumWidth > 0 && target.w < maximumWidth) target.x += (maximumWidth - target.w) / 2;
        SDL_FRect source{0, 0, target.w, target.h};
        if (maximumWidth > 0 && target.w > maximumWidth) {
            source.w = maximumWidth;
            target.w = maximumWidth;
        }
        SDL_RenderTexture(renderer, entry.value, &source, &target);
    }

    void UiWindow::karaoke(const std::string_view value, const SDL_FRect bounds, const float ratio, const int size,
                           const SDL_Color base, const SDL_Color highlight, const std::string_view family) {
        int textWidth = 0, textHeight = 0;
        if (auto *font = fonts_.get(size, family)) TTF_GetStringSize(font, value.data(), value.size(), &textWidth,
                                                                     &textHeight);
        const float scale = textWidth > bounds.w ? bounds.w / static_cast<float>(std::max(1, textWidth)) : 1;
        const int actualSize = std::max(8, static_cast<int>(size * scale));
        if (auto *font = fonts_.get(actualSize, family)) TTF_GetStringSize(
            font, value.data(), value.size(), &textWidth, &textHeight);
        const float left = bounds.x + std::max(0.0F, (bounds.w - textWidth) / 2);
        const float top = bounds.y + (bounds.h - textHeight) / 2;
        text(value, left + 1, top + 1, actualSize, {0, 0, 0, 90}, bounds.w, false, family);
        text(value, left, top, actualSize, base, bounds.w, false, family);
        SDL_Rect previous{};
        const bool hadClip = SDL_RenderClipEnabled(renderer);
        SDL_GetRenderClipRect(renderer, &previous);
        const SDL_Rect clip{
            static_cast<int>(left), static_cast<int>(bounds.y),
            static_cast<int>(std::min(bounds.w, static_cast<float>(textWidth)) * std::clamp(ratio, 0.0F, 1.0F)),
            static_cast<int>(bounds.h)
        };
        SDL_SetRenderClipRect(renderer, &clip);
        text(value, left, top, actualSize, highlight, bounds.w, false, family);
        SDL_SetRenderClipRect(renderer, hadClip ? &previous : nullptr);
    }

    void UiWindow::button(const SDL_FRect bounds, const std::string_view label, std::function<void()> action,
                          const bool selected) {
        const bool hover = contains(bounds, mouseX, mouseY);
        fill(bounds, selected
                         ? SDL_Color{28, 113, 123, 255}
                         : hover
                               ? SDL_Color{43, 65, 87, 255}
                               : SDL_Color{27, 42, 60, 255});
        outline(bounds, selected ? SDL_Color{67, 208, 184, 255} : SDL_Color{52, 73, 92, 255});
        text(label, bounds.x + 5, bounds.y + (bounds.h - 23) / 2, 14, {227, 238, 247, 255}, bounds.w - 10, true);
        hits.push_back({bounds, std::move(action), {}, false});
    }

    void UiWindow::slider(const SDL_FRect bounds, const float value, std::function<void(float)> change,
                          const bool vertical) {
        const float ratio = std::clamp(value, 0.0F, 1.0F);
        fill(bounds, {25, 43, 61, 255});
        auto filled = bounds;
        if (vertical) {
            filled.h *= ratio;
            filled.y += bounds.h - filled.h;
        } else filled.w *= ratio;
        fill(filled, {47, 180, 162, 255});
        outline(bounds, {73, 103, 122, 255});
        hits.push_back({bounds, {}, std::move(change), vertical});
    }

    void UiWindow::bitmap(SDL_Surface *surface, const SDL_FRect target, const SDL_FRect *source) {
        if (!surface || target.w <= 0 || target.h <= 0) return;
        auto found = imageCache_.find(surface);
        if (found == imageCache_.end()) {
            auto *texture = SDL_CreateTextureFromSurface(renderer, surface);
            if (!texture) return;
            SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
            found = imageCache_.emplace(surface, texture).first;
        }
        SDL_RenderTexture(renderer, found->second, source, &target);
    }

    void UiWindow::skinBackground(const Skin &skin, const SkinWindow &definition) {
        auto *image = skin.image(definition.image);
        if (!image) return;
        const auto center = definition.resize;
        if (center.w <= 0 || center.h <= 0 || (width == image->w && height == image->h)) {
            bitmap(image, {0, 0, static_cast<float>(width), static_cast<float>(height)});
            return;
        }
        const float sourceX[]{0, center.x, center.x + center.w, static_cast<float>(image->w)};
        const float sourceY[]{0, center.y, center.y + center.h, static_cast<float>(image->h)};
        const float targetX[]{
            0, center.x, std::max(center.x, width - (image->w - center.x - center.w)), static_cast<float>(width)
        };
        const float targetY[]{
            0, center.y, std::max(center.y, height - (image->h - center.y - center.h)), static_cast<float>(height)
        };
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) {
                const SDL_FRect source{
                    sourceX[column], sourceY[row], sourceX[column + 1] - sourceX[column],
                    sourceY[row + 1] - sourceY[row]
                };
                const SDL_FRect target{
                    targetX[column], targetY[row], targetX[column + 1] - targetX[column],
                    targetY[row + 1] - targetY[row]
                };
                bitmap(image, target, &source);
            }
    }

    void UiWindow::visual(const SDL_FRect bounds, const std::vector<float> &bars, const int mode,
                          const std::vector<float> &waveform, const XmlNode *style) {
        const XmlNode defaults;
        classicVisual(bounds, bars, mode, waveform, style ? *style : defaults);
    }

    bool UiWindow::visible() const {
        return pendingVisibility_ ? *pendingVisibility_ : (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN) == 0;
    }

    void UiWindow::show(const bool showWindow) {
        if (showWindow && !visible()) carouselValues_.clear();
        if (pendingVisibility_) {
            pendingVisibility_ = showWindow;
            return;
        }
        if (showWindow) {
            SDL_ShowWindow(window);
            SDL_RaiseWindow(window);
        } else SDL_HideWindow(window);
    }

    void UiWindow::deferPresentation() {
        if (pendingVisibility_) return;
        pendingVisibility_ = visible();
        SDL_HideWindow(window);
    }

    void UiWindow::commitPresentation() {
        if (!pendingVisibility_) return;
        const bool requested = *pendingVisibility_;
        pendingVisibility_.reset();
        show(requested);
    }

    SDL_FPoint UiWindow::pointer() const { return {mouseX, mouseY}; }
}
