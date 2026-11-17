#pragma once

#include "app/Data.hpp"
#include "media/Equalizer.hpp"

namespace ttplayer::app {
    struct Geometry {
        int x = -1, y = -1, width = 0, height = 0;
        bool visible = false;
    };

    struct Settings {
        std::vector<Playlist> lists{{"默认列表", {}}};
        int playlist = 0, song = -1, mode = 3, visual = 0;
        int appearanceVersion = 0;
        float playlistSplit = 0.35F;
        float volume = 0.7F;
        bool english = false, topmost = false, desktopLocked = false;
        std::string language = "zh_CN", playlistPrefix = "TTPlayer", titleFormat = "%artist% - %title%";
        bool pauseAfterPlay = false, miniOnStart = false, rememberPosition = true, desktopDualLine = false, karaoke =
                        true;
        bool autoLyrics = false;
        std::filesystem::path skin;
        media::EqualizerSettings equalizer;
        std::map<std::string, Geometry> windows;
        std::map<std::string, bool> miniVisibility;
        std::map<std::string, std::int64_t> lyricOffsets;
    };

    Settings loadSettings(const std::filesystem::path &path);

    void saveSettings(const std::filesystem::path &path, const Settings &settings);

    void importJavaAppearance(const std::filesystem::path &path, const std::filesystem::path &skins,
                              Settings &settings);

    struct LyricResult {
        std::string id, title, artist, album;
    };

    std::vector<LyricResult> searchLyrics(std::string_view query);

    std::string downloadLyrics(std::string_view id);
}
