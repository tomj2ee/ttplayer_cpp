#include "app/Store.hpp"

#include <cJSON.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace ttplayer::app {
    namespace {
        using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

        const cJSON *field(const cJSON *object, const char *key) {
            return cJSON_GetObjectItemCaseSensitive(object, key);
        }

        double number(const cJSON *object, const char *key, const double fallback) {
            const auto *value = field(object, key);
            return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) ? value->valuedouble : fallback;
        }

        std::string text(const cJSON *object, const char *key, std::string fallback = {}) {
            const auto *value = field(object, key);
            return cJSON_IsString(value) ? value->valuestring : std::move(fallback);
        }

        bool flag(const cJSON *object, const char *key, const bool fallback = false) {
            const auto *value = field(object, key);
            return cJSON_IsBool(value) ? cJSON_IsTrue(value) : fallback;
        }
    }

    Settings loadSettings(const std::filesystem::path &path) {
        const auto bytes = readFile(path, 64 * 1024 * 1024);
        Json root(cJSON_ParseWithLength(bytes.data(), bytes.size()), cJSON_Delete);
        if (!root || !cJSON_IsObject(root.get())) throw std::runtime_error("Invalid player settings JSON");
        Settings result;
        result.appearanceVersion = static_cast<int>(std::clamp(number(root.get(), "appearanceVersion", 0), 0.0, 100.0));
        result.playlistSplit = static_cast<float>(std::clamp(number(root.get(), "playlistSplit", 0.35), 0.15, 0.75));
        result.playlist = static_cast<int>(std::clamp(number(root.get(), "playlist", 0), 0.0, 10000.0));
        result.song = static_cast<int>(std::clamp(number(root.get(), "song", -1), -1.0, 1000000.0));
        result.mode = static_cast<int>(std::clamp(number(root.get(), "mode", 3), 0.0, 4.0));
        result.visual = static_cast<int>(std::clamp(number(root.get(), "visual", 0), 0.0, 6.0));
        result.volume = static_cast<float>(std::clamp(number(root.get(), "volume", 0.7), 0.0, 1.0));
        result.english = flag(root.get(), "english");
        result.language = text(root.get(), "language", result.english ? "en" : "zh_CN");
        result.pauseAfterPlay = flag(root.get(), "pauseAfterPlay");
        result.miniOnStart = flag(root.get(), "miniOnStart");
        result.rememberPosition = flag(root.get(), "rememberPosition", true);
        result.desktopDualLine = flag(root.get(), "desktopDualLine");
        result.karaoke = flag(root.get(), "karaoke", true);
        result.autoLyrics = flag(root.get(), "autoLyrics");
        result.playlistPrefix = text(root.get(), "playlistPrefix", "TTPlayer");
        result.titleFormat = text(root.get(), "titleFormat", "%artist% - %title%");
        result.topmost = flag(root.get(), "topmost");
        result.desktopLocked = flag(root.get(), "desktopLocked");
        result.skin = fromUtf8(text(root.get(), "skin"));
        const auto *equalizer = field(root.get(), "equalizer");
        result.equalizer.enabled = flag(equalizer, "enabled");
        result.equalizer.dolby = flag(equalizer, "dolby");
        result.equalizer.preamp = static_cast<float>(std::clamp(number(equalizer, "preamp", 0), -12.0, 12.0));
        result.equalizer.balance = static_cast<float>(std::clamp(number(equalizer, "balance", 0), -1.0, 1.0));
        result.equalizer.surround = static_cast<float>(std::clamp(number(equalizer, "surround", 0), 0.0, 1.0));
        const auto *bands = field(equalizer, "bands");
        for (int index = 0; index < 10; ++index) {
            const auto *value = cJSON_GetArrayItem(bands, index);
            if (cJSON_IsNumber(value) && std::isfinite(value->valuedouble))
                result.equalizer.bands[static_cast<std::size_t>(index)] = static_cast<float>(std::clamp(
                    value->valuedouble, -12.0, 12.0));
        }
        if (const auto *lists = field(root.get(), "lists"); cJSON_IsArray(lists)) {
            result.lists.clear();
            const cJSON *list = nullptr;
            cJSON_ArrayForEach(list, lists)
            {
                if (result.lists.size() >= 1000) break;
                Playlist playlist{text(list, "name", "Playlist"), {}};
                const cJSON *entry = nullptr;
                cJSON_ArrayForEach(entry, field(list, "songs"))
                {
                    if (playlist.songs.size() >= 100000) break;
                    const auto file = fromUtf8(text(entry, "path"));
                    if (file.empty()) continue;
                    playlist.songs.push_back({
                        file, text(entry, "title", utf8(file.stem())), text(entry, "artist"), text(entry, "album"),
                        static_cast<std::int64_t>(std::clamp(number(entry, "duration", 0), 0.0, 1.0e12))
                    });
                }
                result.lists.push_back(std::move(playlist));
            }
        }
        if (result.lists.empty()) result.lists.push_back({"默认列表", {}});
        result.playlist = std::min(result.playlist, static_cast<int>(result.lists.size()) - 1);
        result.song = std::min(result.song,
                               static_cast<int>(result.lists[static_cast<std::size_t>(result.playlist)].songs.size()) -
                               1);
        const cJSON *geometry = nullptr;
        const cJSON *offset = nullptr;
        cJSON_ArrayForEach(offset, field(root.get(), "lyricOffsets"))
        {
            if (offset->string && cJSON_IsNumber(offset) && std::isfinite(offset->valuedouble))
                result.lyricOffsets[offset->string] = static_cast<std::int64_t>(std::clamp(
                    offset->valuedouble, -3600000.0, 3600000.0));
        }
        cJSON_ArrayForEach(geometry, field(root.get(), "windows"))
        {
            if (!geometry->string) continue;
            result.windows[geometry->string] = {
                static_cast<int>(std::clamp(number(geometry, "x", -1), -100000.0, 100000.0)),
                static_cast<int>(std::clamp(number(geometry, "y", -1), -100000.0, 100000.0)),
                static_cast<int>(std::clamp(number(geometry, "width", 0), 0.0, 7680.0)),
                static_cast<int>(std::clamp(number(geometry, "height", 0), 0.0, 4320.0)), flag(geometry, "visible")
            };
        }
        for (const auto *name: {"player", "playlist", "lyric", "eq"}) {
            if (const auto *visible = field(field(root.get(), "miniVisibility"), name); cJSON_IsBool(visible))
                result.miniVisibility[name] = cJSON_IsTrue(visible);
        }
        return result;
    }

    void saveSettings(const std::filesystem::path &path, const Settings &settings) {
        Json root(cJSON_CreateObject(), cJSON_Delete);
        if (!root) throw std::bad_alloc();
        cJSON_AddNumberToObject(root.get(), "version", 1);
        cJSON_AddNumberToObject(root.get(), "appearanceVersion", settings.appearanceVersion);
        cJSON_AddNumberToObject(root.get(), "playlistSplit", settings.playlistSplit);
        cJSON_AddNumberToObject(root.get(), "playlist", settings.playlist);
        cJSON_AddNumberToObject(root.get(), "song", settings.song);
        cJSON_AddNumberToObject(root.get(), "mode", settings.mode);
        cJSON_AddNumberToObject(root.get(), "visual", settings.visual);
        cJSON_AddNumberToObject(root.get(), "volume", settings.volume);
        cJSON_AddBoolToObject(root.get(), "english", settings.english);
        cJSON_AddStringToObject(root.get(), "language", settings.language.c_str());
        cJSON_AddBoolToObject(root.get(), "pauseAfterPlay", settings.pauseAfterPlay);
        cJSON_AddBoolToObject(root.get(), "miniOnStart", settings.miniOnStart);
        cJSON_AddBoolToObject(root.get(), "rememberPosition", settings.rememberPosition);
        cJSON_AddBoolToObject(root.get(), "desktopDualLine", settings.desktopDualLine);
        cJSON_AddBoolToObject(root.get(), "karaoke", settings.karaoke);
        cJSON_AddBoolToObject(root.get(), "autoLyrics", settings.autoLyrics);
        auto *miniVisibility = cJSON_AddObjectToObject(root.get(), "miniVisibility");
        for (const auto &[name, visible]: settings.miniVisibility) cJSON_AddBoolToObject(
            miniVisibility, name.c_str(), visible);
        cJSON_AddStringToObject(root.get(), "playlistPrefix", settings.playlistPrefix.c_str());
        cJSON_AddStringToObject(root.get(), "titleFormat", settings.titleFormat.c_str());
        cJSON_AddBoolToObject(root.get(), "topmost", settings.topmost);
        cJSON_AddBoolToObject(root.get(), "desktopLocked", settings.desktopLocked);
        cJSON_AddStringToObject(root.get(), "skin", utf8(settings.skin).c_str());
        auto *equalizer = cJSON_AddObjectToObject(root.get(), "equalizer");
        cJSON_AddBoolToObject(equalizer, "enabled", settings.equalizer.enabled);
        cJSON_AddBoolToObject(equalizer, "dolby", settings.equalizer.dolby);
        cJSON_AddNumberToObject(equalizer, "preamp", settings.equalizer.preamp);
        cJSON_AddNumberToObject(equalizer, "balance", settings.equalizer.balance);
        cJSON_AddNumberToObject(equalizer, "surround", settings.equalizer.surround);
        auto *bands = cJSON_AddArrayToObject(equalizer, "bands");
        for (const auto gain: settings.equalizer.bands) cJSON_AddItemToArray(bands, cJSON_CreateNumber(gain));
        auto *lists = cJSON_AddArrayToObject(root.get(), "lists");
        for (const auto &list: settings.lists) {
            auto *object = cJSON_CreateObject();
            cJSON_AddItemToArray(lists, object);
            cJSON_AddStringToObject(object, "name", list.name.c_str());
            auto *songs = cJSON_AddArrayToObject(object, "songs");
            for (const auto &song: list.songs) {
                auto *entry = cJSON_CreateObject();
                cJSON_AddItemToArray(songs, entry);
                cJSON_AddStringToObject(entry, "path", utf8(song.path).c_str());
                cJSON_AddStringToObject(entry, "title", song.title.c_str());
                cJSON_AddStringToObject(entry, "artist", song.artist.c_str());
                cJSON_AddStringToObject(entry, "album", song.album.c_str());
                cJSON_AddNumberToObject(entry, "duration", static_cast<double>(song.duration));
            }
        }
        auto *windows = cJSON_AddObjectToObject(root.get(), "windows");
        auto *offsets = cJSON_AddObjectToObject(root.get(), "lyricOffsets");
        for (const auto &[file, offset]: settings.lyricOffsets) cJSON_AddNumberToObject(
            offsets, file.c_str(), static_cast<double>(offset));
        for (const auto &[name, geometry]: settings.windows) {
            auto *object = cJSON_AddObjectToObject(windows, name.c_str());
            cJSON_AddNumberToObject(object, "x", geometry.x);
            cJSON_AddNumberToObject(object, "y", geometry.y);
            cJSON_AddNumberToObject(object, "width", geometry.width);
            cJSON_AddNumberToObject(object, "height", geometry.height);
            cJSON_AddBoolToObject(object, "visible", geometry.visible);
        }
        std::unique_ptr<char, decltype(&cJSON_free)> output(cJSON_PrintUnformatted(root.get()), cJSON_free);
        if (!output) throw std::bad_alloc();
        writeFileAtomic(path, output.get());
    }

    void importJavaAppearance(const std::filesystem::path &path, const std::filesystem::path &skins,
                              Settings &settings) {
        const auto properties = parseProperties(decodeText(readFile(path, 1024 * 1024)));
        auto value = [&](const std::string &key, const std::string &fallback) {
            const auto found = properties.find(key);
            return found == properties.end() ? fallback : found->second;
        };
        auto integer = [&](const std::string &key, const int fallback) {
            try { return std::clamp(std::stoi(value(key, std::to_string(fallback))), -100000, 100000); } catch (const
                std::exception &) { return fallback; }
        };
        std::string skin = value("skin", "");
        if (skin.starts_with("classpath:")) skin.erase(0, 10);
        if (skin.starts_with("fs:")) skin.erase(0, 3);
        const auto file = fromUtf8(skin);
        if (!file.empty() && std::filesystem::is_regular_file(file)) settings.skin = file;
        else if (!file.empty() && std::filesystem::is_regular_file(skins / file.filename()))
            settings.skin = skins / file.filename();
        for (const auto *name: {"main", "playlist", "lyric", "eq", "mini"}) {
            const std::string prefix = std::string(name) + ".";
            if (!properties.contains(prefix + "x")) continue;
            settings.windows[std::string_view(name) == "main" ? "player" : name] = {
                integer(prefix + "x", -1), integer(prefix + "y", -1),
                std::clamp(integer(prefix + "width", 0), 0, 7680), std::clamp(integer(prefix + "height", 0), 0, 4320),
                value(prefix + "visible", "true") == "true"
            };
        }
        if (value("inMiniMode", "false") == "true") {
            settings.windows["player"].visible = false;
            settings.windows["mini"].visible = true;
        }
        settings.english = value("language", settings.english ? "en" : "zh_CN").starts_with("en");
        settings.language = value("language", settings.english ? "en" : "zh_CN");
    }
}
