#pragma once

#include <cstddef>
#include <mutex>
#include <vector>

namespace ttplayer::media {
    class SpectrumAnalyzer final {
    public:
        void push(const float *interleaved_samples, int frame_count, int channels);

        void reset();

        [[nodiscard]] std::vector<float> bars(std::size_t bar_count);

        [[nodiscard]] std::vector<float> waveform(std::size_t count);

    private:
        static constexpr std::size_t fft_size = 1024;
        static constexpr std::size_t ring_size = 8192;

        std::mutex mutex_;
        std::vector<float> ring_ = std::vector<float>(ring_size, 0.0F);
        std::vector<float> smoothed_;
        std::size_t write_position_ = 0;
        std::size_t valid_samples_ = 0;
    };
}
