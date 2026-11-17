#pragma once

#include "app/Store.hpp"

#include <future>
#include <memory>
#include <optional>

namespace ttplayer::app {
    class LyricSource {
    public:
        virtual ~LyricSource() = default;

        virtual std::vector<LyricResult> search(std::string_view query) = 0;

        virtual std::string download(std::string_view id) = 0;
    };

    class LyricRequestService final {
    public:
        struct DownloadRequest {
            std::string id;
            std::filesystem::path song;
            std::filesystem::path destination;
            bool associate = true;
        };

        struct Result {
            std::vector<LyricResult> results;
            std::string lyrics;
            std::string error;
            std::filesystem::path song;
            std::filesystem::path destination;
            bool downloaded = false;
            bool associate = true;
        };

        LyricRequestService();

        explicit LyricRequestService(std::shared_ptr<LyricSource> source);

        ~LyricRequestService();

        LyricRequestService(const LyricRequestService &) = delete;

        LyricRequestService &operator=(const LyricRequestService &) = delete;

        bool search(std::string query);

        bool download(DownloadRequest request);

        bool findAutomatically(std::filesystem::path song, std::string query);

        [[nodiscard]] bool busy() const;

        [[nodiscard]] std::optional<Result> poll();

        void wait() const;

    private:
        std::shared_ptr<LyricSource> source_;
        std::future<Result> job_;
    };
}
