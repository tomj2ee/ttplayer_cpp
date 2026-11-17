#pragma once

#include <array>
#include <cstddef>
#include <mutex>

namespace ttplayer::media {
    struct EqualizerSettings {
        bool enabled = false;
        bool dolby = false;
        float preamp = 0;
        float balance = 0;
        float surround = 0;
        std::array<float, 10> bands{};
    };

    class Equalizer final {
    public:
        void configure(EqualizerSettings settings);

        [[nodiscard]] EqualizerSettings settings() const;

        void reset();

        void process(float *samples, std::size_t frames);

        static constexpr std::array<float, 10> frequencies{60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};

    private:
        struct Filter {
            double numerator0 = 1, numerator1 = 0, numerator2 = 0, denominator1 = 0, denominator2 = 0;
            std::array<double, 2> delay1{}, delay2{};
        };

        mutable std::mutex mutex_;
        EqualizerSettings settings_;
        std::array<Filter, 10> filters_{};
    };
}
