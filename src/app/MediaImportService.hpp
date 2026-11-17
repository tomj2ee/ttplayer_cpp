#pragma once

#include "app/Data.hpp"

#include <future>
#include <memory>
#include <mutex>
#include <optional>

namespace ttplayer::app {
    class MediaImportService final {
    public:
        struct Request {
            std::vector<std::filesystem::path> files;
            int targetPlaylist = 0;
            bool playAfterImport = false;
        };

        struct Result {
            std::vector<Song> songs;
            std::vector<Playlist> lists;
            std::string error;
            int targetPlaylist = 0;
            bool playAfterImport = false;
        };

        struct Progress {
            int total = -1;
            int loaded = 0;
            std::string filename;
        };

        MediaImportService() = default;

        ~MediaImportService();

        MediaImportService(const MediaImportService &) = delete;

        MediaImportService &operator=(const MediaImportService &) = delete;

        bool start(Request request);

        [[nodiscard]] bool busy() const;

        [[nodiscard]] std::optional<Result> poll();

        [[nodiscard]] Progress progress() const;

        void cancel();

        void wait() const;

    private:
        struct JobState {
            std::atomic<bool> cancelled{false};
            std::atomic<int> total{-1};
            std::atomic<int> loaded{0};
            std::mutex mutex;
            std::string filename;
        };

        static Result import(Request request, const std::shared_ptr<JobState> &state);

        std::shared_ptr<JobState> state_ = std::make_shared<JobState>();
        std::future<Result> job_;
    };
}
