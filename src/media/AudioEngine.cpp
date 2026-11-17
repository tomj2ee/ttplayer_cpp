#include "media/AudioEngine.hpp"

#include <SDL3/SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace ttplayer::media {
    namespace {
        struct ScopeExit {
            std::function<void()> callback;
            ~ScopeExit() { callback(); }
        };

        std::string ffmpegError(const int code) {
            std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
            av_strerror(code, buffer.data(), buffer.size());
            return buffer.data();
        }

        std::string pathToUtf8(const std::filesystem::path &path) {
            const auto utf8 = path.u8string();
            return {utf8.begin(), utf8.end()};
        }
    }

    AudioEngine::~AudioEngine() {
        stop();
    }

    bool AudioEngine::play(const std::filesystem::path &file_path) {
        stop();
        std::error_code file_error;
        const auto name = pathToUtf8(file_path);
        const bool network = name.starts_with("https://") || name.starts_with("http://");
        if (file_path.empty() || (!network && !std::filesystem::is_regular_file(file_path, file_error))) {
            setError("The selected audio file does not exist.");
            return false;
        }

        {
            std::scoped_lock lock(state_mutex_);
            current_file_ = file_path;
            error_message_.clear();
        }

        stop_requested_ = false;
        pause_requested_ = false;
        seek_request_ms_ = -1;
        decoded_position_ms_ = 0;
        duration_ms_ = 0;
        spectrum_.reset();
        equalizer_.reset();
        state_ = PlaybackState::loading;
        try {
            playback_thread_ = std::thread([this, file_path] {
                try {
                    decodeLoop(file_path);
                } catch (const std::exception &error) {
                    setError(error.what());
                }
            });
        } catch (const std::exception &error) {
            setError(error.what());
            return false;
        }
        return true;
    }

    void AudioEngine::stop() {
        stop_requested_ = true;
        pause_requested_ = false;
        state_changed_.notify_all();

        {
            std::scoped_lock lock(stream_mutex_);
            if (audio_stream_ != nullptr) {
                SDL_ClearAudioStream(audio_stream_);
            }
        }

        if (playback_thread_.joinable()) {
            playback_thread_.join();
        }

        state_ = PlaybackState::stopped;
        decoded_position_ms_ = 0;
        duration_ms_ = 0;
        seek_request_ms_ = -1;
        spectrum_.reset();
    }

    void AudioEngine::togglePause() {
        const auto current_state = state_.load();
        if (current_state == PlaybackState::playing) {
            pause_requested_ = true;
            state_ = PlaybackState::paused;
            std::scoped_lock lock(stream_mutex_);
            if (audio_stream_ != nullptr) {
                SDL_PauseAudioStreamDevice(audio_stream_);
            }
            return;
        }

        if (current_state == PlaybackState::paused) {
            pause_requested_ = false;
            {
                std::scoped_lock lock(stream_mutex_);
                if (audio_stream_ != nullptr) {
                    SDL_ResumeAudioStreamDevice(audio_stream_);
                }
            }
            state_ = PlaybackState::playing;
            state_changed_.notify_all();
        }
    }

    void AudioEngine::seek(std::int64_t target_ms) {
        if (state_ == PlaybackState::stopped || state_ == PlaybackState::error) {
            return;
        }

        const auto known_duration = duration_ms_.load();
        target_ms = known_duration > 0
                        ? std::clamp<std::int64_t>(target_ms, 0, known_duration)
                        : std::max<std::int64_t>(target_ms, 0);
        seek_request_ms_ = target_ms;
        state_changed_.notify_all();
    }

    void AudioEngine::setVolume(const float volume) {
        volume_ = std::clamp(volume, 0.0F, 1.0F);
    }

    PlaybackState AudioEngine::state() const noexcept {
        return state_.load();
    }

    std::int64_t AudioEngine::positionMs() const {
        const auto pending_seek = seek_request_ms_.load();
        if (pending_seek >= 0) {
            return pending_seek;
        }
        const auto decoded = decoded_position_ms_.load();
        const int queued_bytes = queuedAudioBytes();
        const auto queued_frames = queued_bytes / (output_channels * bytes_per_sample);
        const auto queued_ms = static_cast<std::int64_t>(queued_frames) * 1000 / output_sample_rate;
        return std::max<std::int64_t>(0, decoded - queued_ms);
    }

    std::int64_t AudioEngine::durationMs() const noexcept {
        return duration_ms_.load();
    }

    float AudioEngine::volume() const noexcept {
        return volume_.load();
    }

    std::string AudioEngine::errorMessage() const {
        std::scoped_lock lock(state_mutex_);
        return error_message_;
    }

    std::filesystem::path AudioEngine::currentFile() const {
        std::scoped_lock lock(state_mutex_);
        return current_file_;
    }

    std::vector<float> AudioEngine::spectrumBars(const std::size_t count) {
        return spectrum_.bars(count);
    }

    void AudioEngine::setError(std::string message) {
        {
            std::scoped_lock lock(state_mutex_);
            error_message_ = std::move(message);
        }
        state_ = PlaybackState::error;
    }

    int AudioEngine::queuedAudioBytes() const {
        std::scoped_lock lock(stream_mutex_);
        return audio_stream_ == nullptr ? 0 : std::max(0, SDL_GetAudioStreamQueued(audio_stream_));
    }

    void AudioEngine::decodeLoop(std::filesystem::path file_path) {
        AVFormatContext *format_context = nullptr;
        AVCodecContext *codec_context = nullptr;
        SwrContext *resampler = nullptr;
        AVPacket *packet = nullptr;
        AVFrame *frame = nullptr;

        auto cleanup = [&] {
            av_frame_free(&frame);
            av_packet_free(&packet);
            swr_free(&resampler);
            avcodec_free_context(&codec_context);
            avformat_close_input(&format_context);

            std::scoped_lock lock(stream_mutex_);
            if (audio_stream_ != nullptr) {
                SDL_DestroyAudioStream(audio_stream_);
                audio_stream_ = nullptr;
            }
        };
        ScopeExit resource_guard{cleanup};

        auto fail = [&](std::string message) {
            if (!stop_requested_) {
                setError(std::move(message));
            }
        };

        const std::string input_name = pathToUtf8(file_path);
        format_context = avformat_alloc_context();
        if (format_context == nullptr) {
            fail("FFmpeg could not allocate the format context.");
            return;
        }
        format_context->interrupt_callback.callback = [](void *context) -> int {
            return static_cast<AudioEngine *>(context)->stop_requested_.load() ? 1 : 0;
        };
        format_context->interrupt_callback.opaque = this;
        AVDictionary *options = nullptr;
        av_dict_set(&options, "rw_timeout", "10000000", 0);
        av_dict_set(&options, "tls_verify", "1", 0);
        av_dict_set(&options, "protocol_whitelist",
                    input_name.starts_with("http://") || input_name.starts_with("https://")
                        ? "http,https,tcp,tls"
                        : "file", 0);
        int result = avformat_open_input(&format_context, input_name.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (result < 0) {
            fail("FFmpeg could not open the file: " + ffmpegError(result));
            return;
        }

        result = avformat_find_stream_info(format_context, nullptr);
        if (result < 0) {
            fail("FFmpeg could not read stream information: " + ffmpegError(result));
            return;
        }

        const AVCodec *decoder = nullptr;
        const int audio_stream_index = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
        if (audio_stream_index < 0 || decoder == nullptr) {
            fail("No supported audio stream was found.");
            return;
        }

        AVStream *audio_stream = format_context->streams[audio_stream_index];
        codec_context = avcodec_alloc_context3(decoder);
        if (codec_context == nullptr) {
            fail("FFmpeg could not allocate the decoder context.");
            return;
        }

        result = avcodec_parameters_to_context(codec_context, audio_stream->codecpar);
        if (result >= 0) {
            result = avcodec_open2(codec_context, decoder, nullptr);
        }
        if (result < 0) {
            fail("FFmpeg could not initialize the decoder: " + ffmpegError(result));
            return;
        }
        if (codec_context->sample_rate <= 0 || codec_context->ch_layout.nb_channels <= 0) {
            fail("The audio stream has an invalid sample rate or channel layout.");
            return;
        }

        AVChannelLayout input_layout{};
        if (codec_context->ch_layout.nb_channels > 0) {
            result = av_channel_layout_copy(&input_layout, &codec_context->ch_layout);
            if (result < 0) {
                fail("Could not copy the audio channel layout: " + ffmpegError(result));
                return;
            }
        } else {
            av_channel_layout_default(&input_layout, 2);
        }
        AVChannelLayout output_layout{};
        av_channel_layout_default(&output_layout, output_channels);
        result = swr_alloc_set_opts2(&resampler, &output_layout, AV_SAMPLE_FMT_FLT, output_sample_rate,
                                     &input_layout, codec_context->sample_fmt, codec_context->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&input_layout);
        av_channel_layout_uninit(&output_layout);

        if (result >= 0 && resampler != nullptr) {
            result = swr_init(resampler);
        }
        if (result < 0 || resampler == nullptr) {
            fail("FFmpeg could not initialize audio resampling: " + ffmpegError(result));
            return;
        }

        packet = av_packet_alloc();
        frame = av_frame_alloc();
        if (packet == nullptr || frame == nullptr) {
            fail("FFmpeg could not allocate decode buffers.");
            return;
        }

        SDL_AudioSpec output_spec{};
        output_spec.format = SDL_AUDIO_F32;
        output_spec.channels = output_channels;
        output_spec.freq = output_sample_rate;
        SDL_AudioStream *opened_stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &output_spec, nullptr, nullptr);
        if (opened_stream == nullptr) {
            fail("SDL could not open the audio device: " + std::string(SDL_GetError()));
            return;
        }
        {
            std::scoped_lock lock(stream_mutex_);
            audio_stream_ = opened_stream;
        }
        if (!SDL_ResumeAudioStreamDevice(opened_stream)) {
            fail("SDL could not start the audio device: " + std::string(SDL_GetError()));
            return;
        }

        if (format_context->duration != AV_NOPTS_VALUE && format_context->duration > 0) {
            duration_ms_ = format_context->duration / (AV_TIME_BASE / 1000);
        } else if (audio_stream->duration != AV_NOPTS_VALUE && audio_stream->duration > 0) {
            duration_ms_ = av_rescale_q(audio_stream->duration, audio_stream->time_base, AVRational{1, 1000});
        }

        if (stop_requested_) {
            return;
        }
        state_ = PlaybackState::playing;
        constexpr int maximum_queued_bytes = output_sample_rate * output_channels * bytes_per_sample / 8;
        SDL_AudioSpec device_spec{};
        int device_buffer_frames = 0;
        SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(opened_stream), &device_spec, &device_buffer_frames);
        const Uint64 drain_grace_ms = device_spec.freq > 0
                                          ? static_cast<Uint64>(std::max(1, device_buffer_frames)) * 2000 / device_spec.
                                            freq + 10
                                          : 50;
        Uint64 drain_deadline = 0;
        const std::int64_t stream_start = audio_stream->start_time == AV_NOPTS_VALUE ? 0 : audio_stream->start_time;
        std::int64_t discard_before_ms = 0;

        auto convertAndQueue = [&](AVFrame *decoded_frame) -> bool {
            const int output_capacity = std::max(1, static_cast<int>(av_rescale_rnd(
                                                     swr_get_delay(resampler, codec_context->sample_rate) +
                                                     decoded_frame->nb_samples,
                                                     output_sample_rate, codec_context->sample_rate, AV_ROUND_UP)));
            std::vector<float> converted(static_cast<std::size_t>(output_capacity) * output_channels);
            std::array<std::uint8_t *, 1> output_data{reinterpret_cast<std::uint8_t *>(converted.data())};
            const int input_plane_count = av_sample_fmt_is_planar(codec_context->sample_fmt)
                                              ? std::max(1, codec_context->ch_layout.nb_channels)
                                              : 1;
            std::vector<const std::uint8_t *> input_data(static_cast<std::size_t>(input_plane_count));
            for (int plane = 0; plane < input_plane_count; ++plane) {
                input_data[static_cast<std::size_t>(plane)] = decoded_frame->extended_data[plane];
            }
            const int converted_frames = swr_convert(resampler, output_data.data(), output_capacity,
                                                     input_data.data(), decoded_frame->nb_samples);
            if (converted_frames < 0) {
                setError("FFmpeg audio conversion failed: " + ffmpegError(converted_frames));
                return false;
            }

            if (decoded_frame->best_effort_timestamp != AV_NOPTS_VALUE) {
                const auto start_ms = av_rescale_q(decoded_frame->best_effort_timestamp - stream_start,
                                                   audio_stream->time_base, AVRational{1, 1000});
                decoded_position_ms_ = start_ms + static_cast<std::int64_t>(converted_frames) * 1000 /
                                       output_sample_rate;
            } else {
                decoded_position_ms_ += static_cast<std::int64_t>(converted_frames) * 1000 / output_sample_rate;
            }

            if (decoded_position_ms_.load() <= discard_before_ms) {
                return true;
            }
            const auto frame_start_ms = decoded_position_ms_.load()
                                        - static_cast<std::int64_t>(converted_frames) * 1000 / output_sample_rate;
            const int skip_frames = static_cast<int>(std::clamp<std::int64_t>(
                (discard_before_ms - frame_start_ms) * output_sample_rate / 1000, 0, converted_frames));
            const int remaining_frames = converted_frames - skip_frames;
            float *output_samples = converted.data() + skip_frames * output_channels;

            equalizer_.process(output_samples, static_cast<std::size_t>(remaining_frames));
            spectrum_.push(output_samples, remaining_frames, output_channels);
            const float gain = volume_.load();
            if (gain < 0.999F) {
                const std::size_t sample_count = static_cast<std::size_t>(converted_frames) * output_channels;
                for (std::size_t index = 0; index < sample_count; ++index) {
                    converted[index] *= gain;
                }
            }

            const int byte_count = remaining_frames * output_channels * bytes_per_sample;
            std::scoped_lock lock(stream_mutex_);
            if (audio_stream_ == nullptr || !SDL_PutAudioStreamData(audio_stream_, output_samples, byte_count)) {
                setError("SDL could not queue audio: " + std::string(SDL_GetError()));
                return false;
            }
            return true;
        };

        auto receiveFrames = [&]() -> bool {
            while (!stop_requested_) {
                const int receive_result = avcodec_receive_frame(codec_context, frame);
                if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) {
                    return true;
                }
                if (receive_result < 0) {
                    setError("FFmpeg decoding failed: " + ffmpegError(receive_result));
                    return false;
                }
                const bool queued = convertAndQueue(frame);
                av_frame_unref(frame);
                if (!queued) {
                    return false;
                }
            }
            return true;
        };

        bool reached_end = false;
        while (!stop_requested_) {
            const std::int64_t seek_target_ms = seek_request_ms_.exchange(-1);
            if (seek_target_ms >= 0) {
                const auto timestamp = stream_start + av_rescale_q(seek_target_ms, AVRational{1, 1000},
                                                                   audio_stream->time_base);
                if (avformat_seek_file(format_context, audio_stream_index,
                                       std::numeric_limits<std::int64_t>::min(), timestamp,
                                       std::numeric_limits<std::int64_t>::max(), AVSEEK_FLAG_BACKWARD) >= 0) {
                    avcodec_flush_buffers(codec_context);
                    swr_close(resampler);
                    equalizer_.reset();
                    result = swr_init(resampler);
                    if (result < 0) {
                        setError("Could not reset audio resampling: " + ffmpegError(result));
                        break;
                    }
                    spectrum_.reset();
                    discard_before_ms = seek_target_ms;
                    reached_end = false;
                    drain_deadline = 0;
                    decoded_position_ms_ = seek_target_ms;
                    std::scoped_lock lock(stream_mutex_);
                    if (audio_stream_ != nullptr) {
                        SDL_ClearAudioStream(audio_stream_);
                    }
                }
            }

            if (pause_requested_) {
                drain_deadline = 0;
                std::unique_lock lock(state_mutex_);
                state_changed_.wait_for(lock, std::chrono::milliseconds(10), [&] {
                    return !pause_requested_ || stop_requested_ || seek_request_ms_.load() >= 0;
                });
                continue;
            }

            if (reached_end) {
                if (queuedAudioBytes() == 0) {
                    if (drain_deadline == 0) {
                        drain_deadline = SDL_GetTicks() + drain_grace_ms;
                    } else if (SDL_GetTicks() >= drain_deadline) {
                        state_ = PlaybackState::stopped;
                        spectrum_.reset();
                        break;
                    }
                }
                SDL_Delay(5);
                continue;
            }

            if (queuedAudioBytes() >= maximum_queued_bytes) {
                SDL_Delay(5);
                continue;
            }

            result = av_read_frame(format_context, packet);
            if (result == AVERROR_EOF) {
                result = avcodec_send_packet(codec_context, nullptr);
                if ((result < 0 && result != AVERROR_EOF) || !receiveFrames()) {
                    if (state_ != PlaybackState::error) {
                        setError("Could not drain the audio decoder: " + ffmpegError(result));
                    }
                    break;
                }
                const int capacity = static_cast<int>(av_rescale_rnd(
                    swr_get_delay(resampler, codec_context->sample_rate),
                    output_sample_rate, codec_context->sample_rate, AV_ROUND_UP));
                if (capacity > 0) {
                    std::vector<float> tail(static_cast<std::size_t>(capacity) * output_channels);
                    std::uint8_t *tail_data = reinterpret_cast<std::uint8_t *>(tail.data());
                    const int tail_frames = swr_convert(resampler, &tail_data, capacity, nullptr, 0);
                    if (tail_frames < 0) {
                        setError("Could not drain the audio resampler: " + ffmpegError(tail_frames));
                        break;
                    }
                    equalizer_.process(tail.data(), static_cast<std::size_t>(tail_frames));
                    const float gain = volume_.load();
                    for (float &sample: tail) {
                        sample *= gain;
                    }
                    std::scoped_lock lock(stream_mutex_);
                    if (!SDL_PutAudioStreamData(audio_stream_, tail.data(),
                                                tail_frames * output_channels * bytes_per_sample)) {
                        setError("Could not queue the audio tail: " + std::string(SDL_GetError()));
                        break;
                    }
                    decoded_position_ms_ += static_cast<std::int64_t>(tail_frames) * 1000 / output_sample_rate;
                }
                {
                    std::scoped_lock lock(stream_mutex_);
                    if (!SDL_FlushAudioStream(audio_stream_)) {
                        setError("Could not flush the audio stream: " + std::string(SDL_GetError()));
                        break;
                    }
                }
                reached_end = true;
                continue;
            }
            if (result < 0) {
                setError("FFmpeg could not continue reading the file: " + ffmpegError(result));
                break;
            }

            if (packet->stream_index == audio_stream_index) {
                result = avcodec_send_packet(codec_context, packet);
                if (result == AVERROR(EAGAIN)) {
                    if (!receiveFrames()) {
                        av_packet_unref(packet);
                        break;
                    }
                    result = avcodec_send_packet(codec_context, packet);
                }
                if (result >= 0 && !receiveFrames()) {
                    av_packet_unref(packet);
                    break;
                }
                if (result < 0) {
                    setError("FFmpeg rejected an audio packet: " + ffmpegError(result));
                    av_packet_unref(packet);
                    break;
                }
            }
            av_packet_unref(packet);
        }
    }
}
