#include "app/Ui.hpp"

#include <algorithm>
#include <cmath>

namespace ttplayer::app {
    void UiWindow::classicVisual(const SDL_FRect bounds, const std::vector<float> &bars, const int mode,
                                 const std::vector<float> &waveform, const XmlNode &style) {
        const int canvasWidth = static_cast<int>(bounds.w), canvasHeight = static_cast<int>(bounds.h);
        if (canvasWidth < 1 || canvasHeight < 1 || canvasWidth > 4096 || canvasHeight > 2160 || bars.empty()) return;
        const auto pack = [](const SDL_Color color) {
            return static_cast<Uint32>(color.a) << 24 | static_cast<Uint32>(color.r) << 16 | static_cast<Uint32>(color.
                       g) << 8 | color.b;
        };
        const Uint32 background = 0;
        if (!visualTexture_ || canvasWidth != visualWidth_ || canvasHeight != visualHeight_) {
            if (visualTexture_) SDL_DestroyTexture(visualTexture_);
            visualTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                               canvasWidth, canvasHeight);
            if (!visualTexture_) return;
            SDL_SetTextureBlendMode(visualTexture_, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(visualTexture_, SDL_SCALEMODE_NEAREST);
            visualWidth_ = canvasWidth;
            visualHeight_ = canvasHeight;
            visualMode_ = -1;
            visualPixels_.assign(static_cast<std::size_t>(canvasWidth) * canvasHeight, background);
            visualPeaks_.assign(48, 0);
            stars_.clear();
        }
        const auto topColor = parseColor(style.value("spectrumtopcolor"), {18, 114, 175, 255});
        const auto midColor = parseColor(style.value("spectrummidcolor"), {40, 143, 189, 255});
        const auto bottomColor = parseColor(style.value("spectrumbtmcolor"), {16, 215, 255, 255});
        const auto peakColor = pack(parseColor(style.value("spectrumpeakcolor"), {255, 255, 255, 255}));
        auto blend = [&](const SDL_Color first, const SDL_Color second, const float ratio) {
            return pack({
                static_cast<Uint8>(first.r + (second.r - first.r) * ratio),
                static_cast<Uint8>(first.g + (second.g - first.g) * ratio),
                static_cast<Uint8>(first.b + (second.b - first.b) * ratio), 255
            });
        };
        auto gradient = [&](const float ratio) {
            return ratio < 0.5F
                       ? blend(topColor, midColor, ratio * 2)
                       : blend(midColor, bottomColor, (ratio - 0.5F) * 2);
        };
        int blurSpeed = 3;
        try { blurSpeed = std::clamp(std::stoi(style.value("blurspeed", "3")), 1, 10); } catch (const std::exception &
        ) {
        }
        const bool blur = style.value("blur", "1") == "1";
        if (visualMode_ != mode || !blur) std::fill(visualPixels_.begin(), visualPixels_.end(), background);
        else {
            const float keep = 1 - std::clamp(0.18F + blurSpeed * 0.09F, 0.05F, 1.0F);
            for (auto &pixel: visualPixels_) {
                const auto alpha = static_cast<Uint32>((pixel >> 24) * keep);
                pixel = alpha ? (pixel & 0x00ffffff) | alpha << 24 : background;
            }
        }
        visualMode_ = mode;
        auto put = [&](const int left, const int top, const Uint32 color) {
            if (left >= 0 && left < canvasWidth && top >= 0 && top < canvasHeight)
                visualPixels_[static_cast<std::size_t>(top) * canvasWidth + left] = color;
        };
        auto line = [&](int left, int top, const int right, const int bottom, const Uint32 color) {
            const int deltaX = std::abs(right - left), deltaY = -std::abs(bottom - top);
            const int stepX = left < right ? 1 : -1, stepY = top < bottom ? 1 : -1;
            int error = deltaX + deltaY;
            for (;;) {
                put(left, top, color);
                if (left == right && top == bottom) break;
                const int twice = 2 * error;
                if (twice >= deltaY) {
                    error += deltaY;
                    left += stepX;
                }
                if (twice <= deltaX) {
                    error += deltaX;
                    top += stepY;
                }
            }
        };
        auto level = [&](const float ratio) {
            const float position = std::clamp(ratio, 0.0F, 1.0F) * (bars.size() - 1);
            const auto index = static_cast<std::size_t>(position), next = std::min(index + 1, bars.size() - 1);
            return std::clamp(bars[index] + (bars[next] - bars[index]) * (position - index), 0.0F, 1.0F);
        };
        for (std::size_t index = 0; index < visualPeaks_.size(); ++index) visualPeaks_[index] = std::max(
                                                                              level(
                                                                                  static_cast<float>(index) / (
                                                                                      visualPeaks_.size() - 1)),
                                                                              visualPeaks_[index] - 0.012F);
        auto peak = [&](const float ratio) {
            const float position = std::clamp(ratio, 0.0F, 1.0F) * (visualPeaks_.size() - 1);
            const auto index = static_cast<std::size_t>(position), next = std::min(index + 1, visualPeaks_.size() - 1);
            return visualPeaks_[index] + (visualPeaks_[next] - visualPeaks_[index]) * (position - index);
        };
        auto dim = [](const Uint32 color, const float ratio) {
            return 0xff000000 | static_cast<Uint32>(((color >> 16) & 255) * ratio) << 16 | static_cast<Uint32>(
                       ((color >> 8) & 255) * ratio) << 8 | static_cast<Uint32>((color & 255) * ratio);
        };
        const bool wide = style.value("spectrumwide") == "1";
        if (mode == 0 || mode == 1) {
            const bool mirror = mode == 1;
            const int count = std::clamp(canvasWidth / (wide ? 7 : 9), 2, 48);
            const float step = static_cast<float>(canvasWidth) / count;
            const float gap = std::max(1.0F, step * (wide ? 0.12F : 0.3F));
            const int span = mirror ? canvasHeight / 2 : canvasHeight;
            for (int index = 0; index < count; ++index) {
                const float ratio = static_cast<float>(index) / (count - 1), amplitude = level(ratio);
                int height = static_cast<int>(amplitude * span);
                if (height < 1 && amplitude > 0.008F) height = 1;
                const int left = static_cast<int>(std::round(index * step + gap / 2)), width = std::max(
                    1, static_cast<int>(std::round(step - gap)));
                for (int row = 0; row < height; ++row) {
                    const auto color = row == 0
                                           ? peakColor
                                           : gradient(static_cast<float>(row) / std::max(1, height - 1));
                    for (int column = 0; column < width; ++column) put(left + column, span - height + row, color);
                }
                const int peakHeight = static_cast<int>(peak(ratio) * span), peakWidth = width >= 6 ? 2 : 1;
                if (peakHeight > height + 1) for (int column = 0; column < peakWidth; ++column) put(
                    left + (width - peakWidth) / 2 + column, span - peakHeight, peakColor);
                if (mirror) {
                    const int down = static_cast<int>(height * 0.85F);
                    for (int row = 0; row < down; ++row) for (int column = 0; column < width; ++column) put(
                        left + column, span + row,
                        dim(gradient(1 - static_cast<float>(row) / std::max(1, down - 1)), 0.55F));
                }
            }
        } else if (mode == 2) {
            const int count = std::min(48, std::max(8, canvasWidth / 4));
            int previousX = 0, previousY = canvasHeight;
            for (int index = 0; index < count; ++index) {
                const float ratio = static_cast<float>(index) / (count - 1);
                const int left = static_cast<int>(ratio * canvasWidth), top =
                        canvasHeight - static_cast<int>(level(ratio) * canvasHeight * 0.95F);
                line(previousX, previousY, left, top, pack(topColor));
                previousX = left;
                previousY = top;
            }
            line(previousX, previousY, canvasWidth - 1, canvasHeight, pack(topColor));
        } else if (mode == 3 && canvasWidth >= 2) {
            const float step = static_cast<float>(canvasWidth) / 48;
            for (int index = 0; index < 48; ++index) {
                const float ratio = index / 47.0F;
                const int left = static_cast<int>(std::round(index * step)), right = static_cast<int>(std::round(
                    (index + 1) * step));
                const int top = std::max(0, static_cast<int>(canvasHeight - level(ratio) * canvasHeight * 0.95F));
                for (int row = top; row < canvasHeight; ++row) for (
                    int column = left; column < left + std::max(1, right - left); ++column) put(
                    column, row, gradient(static_cast<float>(row) / std::max(1, canvasHeight - 1)));
            }
            for (int index = 0; index < 48; ++index) {
                const float ratio = index / 47.0F;
                const int top = static_cast<int>(canvasHeight - level(ratio) * canvasHeight * 0.95F), peakTop =
                        canvasHeight - static_cast<int>(peak(ratio) * canvasHeight);
                const int left = static_cast<int>(std::round(index * step + step / 2));
                if (top - peakTop >= 1) for (int row = std::max(0, peakTop); row < top; ++row) put(
                    left, row, dim(peakColor, 0.88F));
            }
        } else if (mode == 4) {
            const int cell = wide ? 5 : 6, gap = 2, columns = std::max(2, canvasWidth / (cell + gap)), rows = std::max(
                2, canvasHeight / (cell + gap));
            for (int column = 0; column < columns; ++column) {
                const int filled = static_cast<int>(level(static_cast<float>(column) / (columns - 1)) * rows);
                for (int row = 0; row < rows; ++row) {
                    const auto color = row >= filled
                                           ? background
                                           : row == filled - 1
                                                 ? peakColor
                                                 : gradient(1 - static_cast<float>(row) / rows);
                    for (int offsetY = 0; offsetY < cell; ++offsetY) for (int offsetX = 0; offsetX < cell; ++offsetX)
                        put(column * (cell + gap) + offsetX, canvasHeight - (row + 1) * (cell + gap) + offsetY, color);
                }
            }
        } else if (mode == 5 && waveform.size() > 1) {
            const int count = std::max(16, std::min(canvasWidth, static_cast<int>(waveform.size())));
            int previousX = 0, previousY = canvasHeight / 2;
            for (int index = 1; index < count; ++index) {
                const auto sample = std::min(static_cast<std::size_t>(index) * waveform.size() / count,
                                             waveform.size() - 1);
                const int left = index * canvasWidth / count, top =
                        canvasHeight / 2 + static_cast<int>(waveform[sample] * canvasHeight * 0.42F);
                line(previousX, previousY, left, top, pack(topColor));
                previousX = left;
                previousY = top;
            }
        } else if (mode == 6) starVisual(bars);
        SDL_UpdateTexture(visualTexture_, nullptr, visualPixels_.data(), canvasWidth * sizeof(Uint32));
        SDL_RenderTexture(renderer, visualTexture_, nullptr, &bounds);
    }

    void UiWindow::starVisual(const std::vector<float> &bars) {
        auto random = [this] {
            starRandom_ = (starRandom_ * 0x5deece66dULL + 11) & ((1ULL << 48) - 1);
            return static_cast<float>(starRandom_ >> 24) / 16777216.0F;
        };
        const int centerX = visualWidth_ / 2, centerY = visualHeight_ / 2;
        std::fill(visualPixels_.begin(), visualPixels_.end(), 0);
        const int count = 100 + visualWidth_ * visualHeight_ / 1000;
        auto assign = [&](Star &star) {
            star.baseX = random() * visualWidth_;
            star.baseY = random() * visualHeight_;
            star.band = std::clamp(static_cast<int>(star.baseX / visualWidth_ * bars.size()), 0,
                                   static_cast<int>(bars.size()) - 1);
        };
        if (stars_.size() != static_cast<std::size_t>(count)) {
            stars_.resize(count);
            for (auto &star: stars_) {
                assign(star);
                star.depth = random() * 0.8F;
                random();
                star.phase = random() * 6.283185307F;
            }
        }
        auto mixPixel = [&](const int left, const int top, const Uint32 color, const float opacity) {
            if (left < 0 || top < 0 || left >= visualWidth_ || top >= visualHeight_ || opacity <= 0) return;
            auto &pixel = visualPixels_[static_cast<std::size_t>(top) * visualWidth_ + left];
            const float alpha = std::min(1.0F, opacity);
            const float previousAlpha = (pixel >> 24) / 255.0F;
            const float outputAlpha = alpha + previousAlpha * (1 - alpha);
            Uint32 output = static_cast<Uint32>(outputAlpha * 255) << 24;
            for (const int shift: {0, 8, 16}) {
                const int previous = (pixel >> shift) & 255, next = (color >> shift) & 255;
                output |= static_cast<Uint32>((next * alpha + previous * previousAlpha * (1 - alpha)) /
                                              outputAlpha) << shift;
            }
            pixel = output;
        };
        auto disc = [&](const int left, const int top, const int radius, const float limit, const float denominator,
                        const float strength, const float threshold, const Uint32 color) {
            for (int offsetY = -radius; offsetY <= radius; ++offsetY)
                for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
                    const float distance = std::hypot(static_cast<float>(offsetX), static_cast<float>(offsetY));
                    if (distance > limit) continue;
                    const float alpha = strength * (1 - distance / denominator);
                    if (alpha >= threshold) mixPixel(left + offsetX, top + offsetY, color, alpha);
                }
        };
        float average = 0;
        for (const auto energy: bars) average += energy;
        average /= bars.size();
        for (auto &star: stars_) {
            const float energy = bars[star.band];
            star.depth += 0.008F + average * 0.015F + energy * 0.005F;
            if (star.depth > 1.2F) {
                star.depth = -0.1F;
                assign(star);
            }
            if (star.depth <= 0) continue;
            const float perspective = star.depth * star.depth;
            const float brightness = std::clamp(0.1F + star.depth * 0.8F + energy * 0.4F + std::sin(star.phase) * 0.2F,
                                                0.05F, 1.0F);
            star.phase += 0.04F + energy * 0.1F;
            const float jitter = energy * 0.3F * star.depth;
            const int left = static_cast<int>(centerX + (star.baseX - centerX) * perspective + (random() - 0.5F) *
                                              jitter);
            const int top = static_cast<int>(centerY + (star.baseY - centerY) * perspective + (random() - 0.5F) *
                                             jitter);
            const int size = std::max(
                1, static_cast<int>(std::min(8.0F, 0.3F + star.depth * 4 + energy * star.depth * 2)));
            const int halo = size + static_cast<int>(star.depth * 2);
            const float tint = std::min(1.0F, brightness + energy * 0.3F), ratio = tint < 0.5F
                ? tint * 2
                : (tint - 0.5F) * 2;
            const auto red = static_cast<Uint32>(tint < 0.5F ? 60 + 195 * ratio : 255);
            const auto green = static_cast<Uint32>(tint < 0.5F ? 80 + 120 * ratio : 200 + 55 * ratio);
            const auto blue = static_cast<Uint32>(tint < 0.5F ? 180 - 80 * ratio : 100 + 155 * ratio);
            const Uint32 color = 0xff000000 | red << 16 | green << 8 | blue;
            disc(left, top, halo, halo + 1, halo + 1, brightness * star.depth * 0.6F, 0.01F, color);
            disc(left, top, size, size, size + 0.5F, brightness, 0.1F, color);
        }
        for (std::size_t band = 0; band < std::min(bars.size(), visualPeaks_.size()); ++band) {
            const float intensity = visualPeaks_[band];
            if (intensity <= 0.8F || random() >= 0.05F) continue;
            const float angle = static_cast<float>(band) / bars.size() * 6.283185307F, length = 60 + intensity * 80;
            for (int segment = 0; segment < 12; ++segment) {
                const float ratio = segment / 12.0F;
                const int left = static_cast<int>(centerX + std::cos(angle) * length * ratio), top = static_cast<int>(
                    centerY + std::sin(angle) * length * ratio);
                const int size = static_cast<int>(2 + (1 - ratio) * 2);
                disc(left, top, size, size, size + 1, (1 - ratio) * intensity * 0.5F, 0, 0xffffc864);
            }
        }
    }
}
