#include "app/LyricsRepository.hpp"

#include <utility>

namespace ttplayer::app {
    LyricsRepository::LyricsRepository(std::filesystem::path cacheDirectory)
        : cacheDirectory_(std::move(cacheDirectory)) {
    }

    std::filesystem::path LyricsRepository::cachePath(const std::filesystem::path &song) const {
        std::uint64_t hash = 14695981039346656037ULL;
        const auto normalized = std::filesystem::absolute(song).lexically_normal();
        for (const unsigned char character: utf8(normalized)) {
            hash ^= character;
            hash *= 1099511628211ULL;
        }
        return cacheDirectory_ / (std::to_string(hash) + ".lrc");
    }

    Lyrics LyricsRepository::load(const std::filesystem::path &song, const std::filesystem::path &explicitFile) const {
        auto local = song;
        local.replace_extension(".lrc");
        const auto file = !explicitFile.empty()
                              ? explicitFile
                              : std::filesystem::exists(local)
                                    ? local
                                    : song.empty()
                                          ? std::filesystem::path{}
                                          : cachePath(song);
        Lyrics lyrics;
        if (!file.empty() && std::filesystem::exists(file)) lyrics.parse(readFile(file, 4 * 1024 * 1024));
        return lyrics;
    }

    void LyricsRepository::store(const std::filesystem::path &song, const std::string_view content,
                                 const std::filesystem::path &destination, const bool associate) const {
        const auto cache = cachePath(song);
        writeFileAtomic(cache, content);
        if (!destination.empty() && destination != cache) writeFileAtomic(destination, content);
        if (associate) {
            auto local = song;
            local.replace_extension(".lrc");
            if (!std::filesystem::exists(local)) writeFileAtomic(local, content);
        }
    }
}
