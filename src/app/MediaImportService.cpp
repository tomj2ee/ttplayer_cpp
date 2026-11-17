#include "app/MediaImportService.hpp"

#include <chrono>
#include <iterator>
#include <unordered_set>

namespace ttplayer::app {
    MediaImportService::~MediaImportService() {
        cancel();
        wait();
    }

    bool MediaImportService::start(Request request) {
        if (busy()) return false;
        auto state = std::make_shared<JobState>();
        auto job = std::async(std::launch::async, [request = std::move(request), state]() mutable {
            return import(std::move(request), state);
        });
        state_ = std::move(state);
        job_ = std::move(job);
        return true;
    }

    bool MediaImportService::busy() const {
        return job_.valid();
    }

    std::optional<MediaImportService::Result> MediaImportService::poll() {
        if (!busy() || job_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return std::nullopt;
        return job_.get();
    }

    MediaImportService::Progress MediaImportService::progress() const {
        std::scoped_lock lock(state_->mutex);
        return {state_->total.load(), state_->loaded.load(), state_->filename};
    }

    void MediaImportService::cancel() {
        state_->cancelled.store(true);
    }

    void MediaImportService::wait() const {
        if (job_.valid()) job_.wait();
    }

    MediaImportService::Result MediaImportService::import(Request request, const std::shared_ptr<JobState> &state) {
        Result result;
        result.targetPlaylist = request.targetPlaylist;
        result.playAfterImport = request.playAfterImport;
        try {
            std::vector<std::filesystem::path> audio;
            auto addFile = [&](const std::filesystem::path &path) {
                const auto extension = lower(utf8(path.extension()));
                if (isAudio(path)) audio.push_back(path);
                else if (extension == ".m3u" || extension == ".m3u8" || extension == ".pls") {
                    result.lists.push_back({utf8(path.stem()), importM3u(path)});
                } else if (extension == ".xml") {
                    auto lists = importPlaylists(path);
                    result.lists.insert(result.lists.end(), std::make_move_iterator(lists.begin()),
                                        std::make_move_iterator(lists.end()));
                }
            };
            for (const auto &path: request.files) {
                if (state->cancelled.load()) break;
                std::error_code error;
                if (std::filesystem::is_directory(path, error)) {
                    std::filesystem::recursive_directory_iterator iterator(
                        path, std::filesystem::directory_options::skip_permission_denied, error), end;
                    for (; iterator != end && !state->cancelled.load() && audio.size() < 100000; iterator.
                           increment(error)) {
                        if (error) {
                            error.clear();
                            continue;
                        }
                        if (iterator->is_regular_file(error) && isAudio(iterator->path())) audio.push_back(
                            iterator->path());
                    }
                } else addFile(path);
            }
            std::unordered_set<std::string> seen;
            state->total.store(static_cast<int>(audio.size()));
            for (const auto &path: audio) {
                if (state->cancelled.load()) break;
                {
                    std::scoped_lock lock(state->mutex);
                    state->filename = utf8(path.filename());
                }
                const auto normalized = std::filesystem::absolute(path).lexically_normal();
                if (seen.insert(utf8(normalized)).second) result.songs.push_back(
                    probeSong(normalized, &state->cancelled));
                state->loaded.fetch_add(1);
            }
        } catch (const std::exception &error) {
            result.error = error.what();
        }
        return result;
    }
}
