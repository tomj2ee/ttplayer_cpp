#include "app/LyricRequestService.hpp"

#include <chrono>
#include <stdexcept>

namespace ttplayer::app {
    namespace {
        class OnlineLyricSource final : public LyricSource {
        public:
            std::vector<LyricResult> search(const std::string_view query) override {
                return searchLyrics(query);
            }

            std::string download(const std::string_view id) override {
                return downloadLyrics(id);
            }
        };
    }

    LyricRequestService::LyricRequestService()
        : LyricRequestService(std::make_shared<OnlineLyricSource>()) {
    }

    LyricRequestService::LyricRequestService(std::shared_ptr<LyricSource> source)
        : source_(std::move(source)) {
        if (!source_) throw std::invalid_argument("A lyric source is required");
    }

    LyricRequestService::~LyricRequestService() {
        wait();
    }

    bool LyricRequestService::search(std::string query) {
        if (busy()) return false;
        job_ = std::async(std::launch::async, [query = std::move(query), source = source_] {
            Result result;
            try {
                result.results = source->search(query);
            } catch (const std::exception &error) {
                result.error = error.what();
            }
            return result;
        });
        return true;
    }

    bool LyricRequestService::download(DownloadRequest request) {
        if (busy()) return false;
        job_ = std::async(std::launch::async, [request = std::move(request), source = source_] {
            Result result;
            result.song = request.song;
            result.destination = request.destination;
            result.downloaded = true;
            result.associate = request.associate;
            try {
                result.lyrics = source->download(request.id);
            } catch (const std::exception &error) {
                result.error = error.what();
            }
            return result;
        });
        return true;
    }

    bool LyricRequestService::findAutomatically(std::filesystem::path song, std::string query) {
        if (busy()) return false;
        job_ = std::async(std::launch::async, [song = std::move(song), query = std::move(query), source = source_] {
            Result result;
            result.song = song;
            result.downloaded = true;
            result.associate = false;
            try {
                const auto found = source->search(query);
                if (!found.empty()) result.lyrics = source->download(found.front().id);
            } catch (const std::exception &error) {
                result.error = error.what();
            }
            return result;
        });
        return true;
    }

    bool LyricRequestService::busy() const {
        return job_.valid();
    }

    std::optional<LyricRequestService::Result> LyricRequestService::poll() {
        if (!busy() || job_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return std::nullopt;
        return job_.get();
    }

    void LyricRequestService::wait() const {
        if (job_.valid()) job_.wait();
    }
}
