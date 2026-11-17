#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ttplayer::app {
    namespace embedded {
        std::span<const unsigned char> archive();

        std::string_view identifier();
    }

    class EmbeddedAssets final {
    public:
        EmbeddedAssets();

        EmbeddedAssets(std::span<const unsigned char> archive, std::string identifier);

        bool available() const;

        std::optional<std::filesystem::path> prepare(const std::filesystem::path &cacheDirectory) const;

    private:
        std::span<const unsigned char> archive_;
        std::string identifier_;
    };
}
