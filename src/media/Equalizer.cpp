#include "media/Equalizer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ttplayer::media {
    void Equalizer::configure(EqualizerSettings settings) {
        std::scoped_lock lock(mutex_);
        if (!std::isfinite(settings.preamp)) settings.preamp = 0;
        if (!std::isfinite(settings.balance)) settings.balance = 0;
        if (!std::isfinite(settings.surround)) settings.surround = 0;
        settings.preamp = std::clamp(settings.preamp, -12.0F, 12.0F);
        settings.balance = std::clamp(settings.balance, -1.0F, 1.0F);
        settings.surround = std::clamp(settings.surround, 0.0F, 1.0F);
        for (std::size_t index = 0; index < filters_.size(); ++index) {
            if (!std::isfinite(settings.bands[index])) settings.bands[index] = 0;
            settings.bands[index] = std::clamp(settings.bands[index], -12.0F, 12.0F);
            const double amplitude = std::pow(10.0, settings.bands[index] / 40.0);
            const double omega = 2 * std::numbers::pi * frequencies[index] / 48000.0;
            const double alpha = std::sin(omega) / 2.0;
            const double divisor = 1 + alpha / amplitude;
            auto &filter = filters_[index];
            filter.numerator0 = (1 + alpha * amplitude) / divisor;
            filter.numerator1 = -2 * std::cos(omega) / divisor;
            filter.numerator2 = (1 - alpha * amplitude) / divisor;
            filter.denominator1 = filter.numerator1;
            filter.denominator2 = (1 - alpha / amplitude) / divisor;
            if (settings.enabled != settings_.enabled) {
                filter.delay1 = {};
                filter.delay2 = {};
            }
        }
        settings_ = settings;
    }

    EqualizerSettings Equalizer::settings() const {
        std::scoped_lock lock(mutex_);
        return settings_;
    }

    void Equalizer::reset() {
        std::scoped_lock lock(mutex_);
        for (auto &filter: filters_) {
            filter.delay1 = {};
            filter.delay2 = {};
        }
    }

    void Equalizer::process(float *samples, const std::size_t frames) {
        std::scoped_lock lock(mutex_);
        const double gain = settings_.enabled ? std::pow(10.0, settings_.preamp / 20.0) : 1;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            std::array < double, 2 > stereo{samples[frame * 2] * gain, samples[frame * 2 + 1] * gain};
            if (settings_.enabled) {
                for (auto &filter: filters_) {
                    for (std::size_t channel = 0; channel < stereo.size(); ++channel) {
                        const double output = filter.numerator0 * stereo[channel] + filter.delay1[channel];
                        filter.delay1[channel] =
                                filter.numerator1 * stereo[channel] - filter.denominator1 * output + filter.delay2[
                                    channel];
                        filter.delay2[channel] = filter.numerator2 * stereo[channel] - filter.denominator2 * output;
                        stereo[channel] = output;
                    }
                }
                const double mid = (stereo[0] + stereo[1]) * 0.5;
                const double side = (stereo[0] - stereo[1]) * (0.5 + settings_.surround * 0.5);
                stereo = {mid + side, mid - side};
            }
            if (settings_.dolby) {
                const double left = stereo[0];
                stereo[0] += stereo[1] * 0.35;
                stereo[1] += left * 0.35;
            }
            stereo[0] *= 1 - std::max(0.0F, settings_.balance);
            stereo[1] *= 1 + std::min(0.0F, settings_.balance);
            for (std::size_t channel = 0; channel < stereo.size(); ++channel) {
                samples[frame * 2 + channel] = static_cast<float>(std::clamp(stereo[channel], -1.0, 1.0));
            }
        }
    }
}
