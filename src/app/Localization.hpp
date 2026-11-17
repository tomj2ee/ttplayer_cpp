#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace ttplayer::app {
    class Localization final {
    public:
        void load(const std::filesystem::path &assets);

        [[nodiscard]] static bool supports(std::string_view language);

        [[nodiscard]] std::string message(std::string_view key, std::string_view language) const;

        [[nodiscard]] std::string translate(const char *chinese, const char *english, std::string_view language,
                                            bool legacyEnglish = false) const;

    private:
        using Dictionary = std::map<std::string, std::string>;
        std::map<std::string, Dictionary> catalogs_;
        Dictionary keysByText_;
    };
}
