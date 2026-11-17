#include "app/Localization.hpp"

#include "app/Data.hpp"

namespace ttplayer::app {
    void Localization::load(const std::filesystem::path &assets) {
        catalogs_.clear();
        keysByText_.clear();
        for (const auto *language: {"zh_CN", "en", "ja", "ko", "de"}) {
            const auto file = std::string("messages") + (std::string_view(language) == "en"
                                                             ? ""
                                                             : "_" + std::string(language)) + ".properties";
            auto path = assets / "messages" / file;
            if (!std::filesystem::exists(path)) path = assets / file;
            if (!std::filesystem::is_regular_file(path)) continue;
            catalogs_[language] = parseProperties(decodeText(readFile(path)));
        }
        for (const auto *language: {"zh_CN", "en"}) {
            const auto found = catalogs_.find(language);
            if (found == catalogs_.end()) continue;
            for (const auto &[key, value]: found->second) {
                if (!value.empty()) keysByText_.try_emplace(value, key);
            }
        }
    }

    bool Localization::supports(const std::string_view language) {
        return language == "zh_CN" || language == "en" || language == "ja" || language == "ko" || language == "de";
    }

    std::string Localization::message(const std::string_view key, const std::string_view language) const {
        for (const auto candidate: {language, std::string_view("zh_CN"), std::string_view("en")}) {
            const auto catalog = catalogs_.find(std::string(candidate));
            if (catalog == catalogs_.end()) continue;
            const auto found = catalog->second.find(std::string(key));
            if (found != catalog->second.end()) return found->second;
        }
        return std::string(key);
    }

    std::string Localization::translate(const char *chinese, const char *english, const std::string_view language,
                                        const bool legacyEnglish) const {
        if (language != "zh_CN" && language != "en") {
            const auto found = keysByText_.find(chinese);
            if (found != keysByText_.end()) return message(found->second, language);
            const auto alternative = keysByText_.find(english);
            if (alternative != keysByText_.end()) return message(alternative->second, language);
        }
        return legacyEnglish || language == "en" ? english : chinese;
    }
}
