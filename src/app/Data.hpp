#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ttplayer::app {
    std::filesystem::path fromUtf8(std::string_view value);

    std::string utf8(const std::filesystem::path &path);

    std::string readFile(const std::filesystem::path &path, std::size_t limit = 16 * 1024 * 1024);

    std::string decodeText(std::string_view bytes);

    void writeFileAtomic(const std::filesystem::path &path, std::string_view text);

    std::string lower(std::string value);

    std::map<std::string, std::string> parseProperties(std::string_view text);

    std::string timeText(std::int64_t milliseconds);

    bool isAudio(const std::filesystem::path &path);

    struct XmlNode {
        std::string name;
        std::map<std::string, std::string> attributes;
        std::vector<XmlNode> children;

        std::string value(std::string_view key, std::string fallback = {}) const;
    };

    XmlNode parseXml(std::string_view text);

    struct Song {
        std::filesystem::path path;
        std::string title, artist, album;
        std::int64_t duration = 0;
    };

    struct Playlist {
        std::string name;
        std::vector<Song> songs;
    };

    std::vector<Playlist> importPlaylists(const std::filesystem::path &path);

    std::vector<Song> importM3u(const std::filesystem::path &path);

    void exportM3u(const std::filesystem::path &path, const std::vector<Song> &songs);

    Song probeSong(const std::filesystem::path &path, const std::atomic<bool> *cancellation = nullptr);

    struct LyricWord {
        std::int64_t time = 0;
        std::string text;
    };

    struct LyricLine {
        std::int64_t time = 0;
        std::string text;
        std::vector<LyricWord> words;
    };

    class Lyrics {
    public:
        void parse(std::string_view content);

        [[nodiscard]] std::string serialize() const;

        [[nodiscard]] int active(std::int64_t position) const;

        [[nodiscard]] float progress(int index, std::int64_t position, std::int64_t duration) const;

        std::vector<LyricLine> lines;
        std::map<std::string, std::string> metadata;
        std::int64_t offset = 0;
    };
}
