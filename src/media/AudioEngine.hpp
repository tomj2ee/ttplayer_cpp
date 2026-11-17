#pragma once

#include "media/SpectrumAnalyzer.hpp"
#include "media/Equalizer.hpp"

#include <SDL3/SDL_audio.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ttplayer::media {
    enum class PlaybackState {
        stopped,
        loading,
        playing,
        paused,
        error
    };

    class AudioEngine final {
    public:
        AudioEngine() = default;

        ~AudioEngine();

        AudioEngine(const AudioEngine &) = delete;

        AudioEngine &operator=(const AudioEngine &) = delete;

        bool play(const std::filesystem::path &file_path);

        void stop();

        void togglePause();

        void seek(std::int64_t target_ms);

        void setVolume(float volume);

        [[nodiscard]] PlaybackState state() const noexcept;

        [[nodiscard]] std::int64_t positionMs() const;

        [[nodiscard]] std::int64_t durationMs() const noexcept;

        [[nodiscard]] float volume() const noexcept;

        [[nodiscard]] std::string errorMessage() const;

        [[nodiscard]] std::filesystem::path currentFile() const;

        [[nodiscard]] std::vector<float> spectrumBars(std::size_t count);

        [[nodiscard]] std::vector<float> waveform(std::size_t count) { return spectrum_.waveform(count); }
        Equalizer &equalizer() noexcept { return equalizer_; }

    private:
        void decodeLoop(std::filesystem::path file_path);

        void setError(std::string message);

        [[nodiscard]] int queuedAudioBytes() const;

        static constexpr int output_sample_rate = 48'000;
        static constexpr int output_channels = 2;
        static constexpr int bytes_per_sample = sizeof(float);

        std::atomic<PlaybackState> state_ = PlaybackState::stopped;
        std::atomic<bool> stop_requested_ = false;
        std::atomic<bool> pause_requested_ = false;
        std::atomic<std::int64_t> seek_request_ms_ = -1;
        std::atomic<std::int64_t> decoded_position_ms_ = 0;
        std::atomic<std::int64_t> duration_ms_ = 0;
        std::atomic<float> volume_ = 0.70F;

        mutable std::mutex stream_mutex_;
        SDL_AudioStream *audio_stream_ = nullptr;

        mutable std::mutex state_mutex_;
        std::condition_variable state_changed_;
        std::thread playback_thread_;
        std::filesystem::path current_file_;
        std::string error_message_;

        SpectrumAnalyzer spectrum_;
        Equalizer equalizer_;
    };
}
