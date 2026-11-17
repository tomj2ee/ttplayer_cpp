#include "app/Application.hpp"

namespace ttplayer::app {
    std::string Application::message(const std::string_view key) const {
        return localization_.message(key, settings_.language);
    }

    std::string Application::tr(const char *chinese, const char *english) const {
        return localization_.translate(chinese, english, settings_.language, settings_.english);
    }

    void Application::setLanguage(std::string language) {
        if (!Localization::supports(language)) language = "zh_CN";
        settings_.language = std::move(language);
        settings_.english = settings_.language == "en";
        for (auto &[name, window]: windows_) {
            window->clearCache();
            if (name == "playlist") SDL_SetWindowTitle(window->window, message("playlist.title").c_str());
            else if (name == "lyric") SDL_SetWindowTitle(window->window, message("menu.lyricShow").c_str());
            else if (name == "eq") SDL_SetWindowTitle(window->window, message("menu.equalizer").c_str());
        }
        dirty_ = true;
    }

    std::string Application::songTitle(const Song &song, const int index) const {
        auto title = settings_.titleFormat;
        const std::pair<std::string_view, std::string> fields[]{
            {"%artist%", song.artist}, {"%title%", song.title.empty() ? utf8(song.path.stem()) : song.title},
            {"%album%", song.album}, {"%filename%", utf8(song.path.filename())}, {"%track%", std::to_string(index + 1)}
        };
        for (const auto &[key, value]: fields) {
            std::size_t offset = 0;
            while ((offset = title.find(key, offset)) != std::string::npos) {
                title.replace(offset, key.size(), value);
                offset += value.size();
            }
        }
        if (song.artist.empty() && title.starts_with(" - ")) title.erase(0, 3);
        return title;
    }
}
