#include "media/SpectrumAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace ttplayer::media {
    namespace {
        void fft(std::vector<std::complex<float> > &values) {
            const std::size_t size = values.size();

            for (std::size_t index = 1, reversed = 0; index < size; ++index) {
                std::size_t bit = size >> 1U;
                for (; reversed & bit; bit >>= 1U) {
                    reversed ^= bit;
                }
                reversed ^= bit;
                if (index < reversed) {
                    std::swap(values[index], values[reversed]);
                }
            }

            for (std::size_t length = 2; length <= size; length <<= 1U) {
                const float angle = -2.0F * std::numbers::pi_v<float> / static_cast<float>(length);
                const std::complex<float> step(std::cos(angle), std::sin(angle));
                for (std::size_t offset = 0; offset < size; offset += length) {
                    std::complex<float> weight(1.0F, 0.0F);
                    for (std::size_t index = 0; index < length / 2; ++index) {
                        const auto even = values[offset + index];
                        const auto odd = values[offset + index + length / 2] * weight;
                        values[offset + index] = even + odd;
                        values[offset + index + length / 2] = even - odd;
                        weight *= step;
                    }
                }
            }
        }
    }

    void SpectrumAnalyzer::push(const float *samples, const int frame_count, const int channels) {
        if (samples == nullptr || frame_count <= 0 || channels <= 0) {
            return;
        }

        std::scoped_lock lock(mutex_);
        for (int frame = 0; frame < frame_count; ++frame) {
            float mono = 0.0F;
            for (int channel = 0; channel < channels; ++channel) {
                mono += samples[frame * channels + channel];
            }
            ring_[write_position_] = mono / static_cast<float>(channels);
            write_position_ = (write_position_ + 1) % ring_size;
            valid_samples_ = std::min(valid_samples_ + 1, ring_size);
        }
    }

    void SpectrumAnalyzer::reset() {
        std::scoped_lock lock(mutex_);
        std::fill(ring_.begin(), ring_.end(), 0.0F);
        std::fill(smoothed_.begin(), smoothed_.end(), 0.0F);
        write_position_ = 0;
        valid_samples_ = 0;
    }

    std::vector<float> SpectrumAnalyzer::waveform(const std::size_t count) {
        std::scoped_lock lock(mutex_);
        const auto points = std::min(count, fft_size);
        std::vector<float> result(points, 0);
        const auto available = std::min(valid_samples_, fft_size);
        const auto start = (write_position_ + ring_size - available) % ring_size;
        for (std::size_t index = 0; index < points && available > 0; ++index)
            result[index] = ring_[(start + index * available / points) % ring_size];
        return result;
    }

    std::vector<float> SpectrumAnalyzer::bars(const std::size_t bar_count) {
        if (bar_count == 0) {
            return {};
        }

        std::vector<std::complex<float> > samples(fft_size);
        std::scoped_lock lock(mutex_);
        const std::size_t available = std::min(valid_samples_, fft_size);
        const std::size_t start = (write_position_ + ring_size - available) % ring_size;
        const std::size_t padding = fft_size - available;

        for (std::size_t index = 0; index < available; ++index) {
            const float phase = static_cast<float>(index + padding) / static_cast<float>(fft_size - 1);
            const float window = 0.5F - 0.5F * std::cos(2.0F * std::numbers::pi_v<float> * phase);
            samples[index + padding] = ring_[(start + index) % ring_size] * window;
        }

        fft(samples);
        if (smoothed_.size() != bar_count) {
            smoothed_.assign(bar_count, 0.0F);
        }

        std::vector<float> result(bar_count, 0.0F);
        constexpr float minimum_bin = 1.0F;
        constexpr float maximum_bin = static_cast<float>(fft_size / 2 - 1);

        for (std::size_t bar = 0; bar < bar_count; ++bar) {
            const float begin_ratio = static_cast<float>(bar) / static_cast<float>(bar_count);
            const float end_ratio = static_cast<float>(bar + 1) / static_cast<float>(bar_count);
            const auto begin_bin = static_cast<std::size_t>(minimum_bin * std::pow(maximum_bin / minimum_bin,
                                                                begin_ratio));
            const auto end_bin = std::max(begin_bin + 1,
                                          static_cast<std::size_t>(minimum_bin * std::pow(maximum_bin / minimum_bin,
                                                                       end_ratio)));
            float magnitude = 0.0F;
            for (std::size_t bin = begin_bin; bin < std::min(end_bin, fft_size / 2); ++bin) {
                magnitude = std::max(magnitude, std::abs(samples[bin]));
            }
            const float level = std::clamp(std::log1p(magnitude * 0.08F) / 3.5F, 0.0F, 1.0F);
            const float smoothing = level > smoothed_[bar] ? 0.55F : 0.12F;
            smoothed_[bar] += (level - smoothed_[bar]) * smoothing;
            result[bar] = smoothed_[bar];
        }
        return result;
    }
}
