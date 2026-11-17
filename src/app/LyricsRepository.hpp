#pragma once

#include "app/Data.hpp"

namespace ttplayer::app {
    class LyricsRepository final {
    public:
        explicit LyricsRepository(std::filesystem::path cacheDirectory = {});

        [[nodiscard]] std::filesystem::path cachePath(const std::filesystem::path &song) const;

        [[nodiscard]] Lyrics load(const std::filesystem::path &song,
                                  const std::filesystem::path &explicitFile = {}) const;

        void store(const std::filesystem::path &song, std::string_view content,
                   const std::filesystem::path &destination, bool associate) const;

    private:
        std::filesystem::path cacheDirectory_;
    };
}
