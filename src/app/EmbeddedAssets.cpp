#include "app/EmbeddedAssets.hpp"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ttplayer::app {
    namespace {
        class Archive final {
        public:
            explicit Archive(const std::span<const unsigned char> bytes) {
                if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) throw std::runtime_error(
                    "Invalid embedded resource archive");
            }

            ~Archive() { mz_zip_reader_end(&zip); }

            Archive(const Archive &) = delete;

            Archive &operator=(const Archive &) = delete;

            mz_zip_archive zip{};
        };

        class TemporaryFile final {
        public:
            explicit TemporaryFile(const std::filesystem::path &target) {
                static std::atomic<unsigned long long> sequence{0};
                path = target;
                path += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                        std::to_string(std::random_device{}()) + "-" + std::to_string(sequence++);
            }

            ~TemporaryFile() {
                std::error_code error;
                std::filesystem::remove(path, error);
            }

            std::filesystem::path path;
        };

        std::filesystem::path relativePath(const char *name) {
            const std::string_view value(name);
            if (value.empty() || value.find_first_of("\\:*?\"<>|\r\n") != std::string_view::npos) throw
                    std::runtime_error("Unsafe embedded resource name");
            const std::u8string unicode(reinterpret_cast<const char8_t *>(value.data()), value.size());
            const std::filesystem::path path(unicode);
            if (path.is_absolute() || path.has_root_path()) throw std::runtime_error("Absolute embedded resource name");
            for (const auto &part: path) {
                if (part == "." || part == "..") throw std::runtime_error("Embedded resource path traversal");
                const auto component = part.u8string();
                if (!component.empty() && (component.back() == u8'.' || component.back() == u8' ')) throw
                        std::runtime_error("Ambiguous embedded resource name");
            }
            return path;
        }

        void rejectSymlinks(const std::filesystem::path &root, const std::filesystem::path &relative) {
            auto current = root;
            std::error_code error;
            if (std::filesystem::is_symlink(current, error)) throw std::runtime_error(
                "Resource cache must not be a symbolic link");
            for (const auto &part: relative) {
                current /= part;
                if (std::filesystem::is_symlink(current, error)) throw std::runtime_error(
                    "Resource cache contains a symbolic link");
            }
        }

        bool matches(const std::filesystem::path &file, const mz_zip_archive_file_stat &entry) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error) || std::filesystem::file_size(file, error) != entry.
                m_uncomp_size || error) return false;
            std::ifstream input(file, std::ios::binary);
            if (!input) return false;
            std::array < unsigned char, 65536 > buffer{};
            mz_ulong checksum = MZ_CRC32_INIT;
            while (input) {
                input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
                checksum = mz_crc32(checksum, buffer.data(), static_cast<std::size_t>(input.gcount()));
            }
            return input.eof() && checksum == entry.m_crc32;
        }

        void materialize(Archive &archive, const mz_uint index, const mz_zip_archive_file_stat &entry,
                         const std::filesystem::path &target) {
            if (matches(target, entry)) return;
            std::vector<unsigned char> content(static_cast<std::size_t>(entry.m_uncomp_size));
            if (!mz_zip_reader_extract_to_mem(&archive.zip, index, content.data(), content.size(), 0)) throw
                    std::runtime_error("Cannot decode embedded resource");
            std::filesystem::create_directories(target.parent_path());
            TemporaryFile temporary(target);
            {
                std::ofstream output;
                output.exceptions(std::ios::failbit | std::ios::badbit);
                output.open(temporary.path, std::ios::binary | std::ios::trunc);
                output.write(reinterpret_cast<const char *>(content.data()),
                             static_cast<std::streamsize>(content.size()));
                output.close();
            }
            std::error_code error;
            for (int attempt = 0; attempt < 100; ++attempt) {
                if (matches(target, entry)) return;
                std::filesystem::rename(temporary.path, target, error);
                if (!error || matches(target, entry)) return;
                std::error_code inspectionError;
                if (std::filesystem::exists(target, inspectionError)) {
                    if (!std::filesystem::is_regular_file(target, inspectionError)) throw std::runtime_error(
                        "Resource cache file has an invalid type");
                    if (!matches(target, entry)) std::filesystem::remove(target, inspectionError);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            throw std::filesystem::filesystem_error("Cannot publish embedded resource", target, error);
        }
    }

    EmbeddedAssets::EmbeddedAssets() : EmbeddedAssets(embedded::archive(), std::string(embedded::identifier())) {
    }

    EmbeddedAssets::EmbeddedAssets(const std::span<const unsigned char> archive, std::string identifier)
        : archive_(archive), identifier_(std::move(identifier)) {
    }

    bool EmbeddedAssets::available() const { return !archive_.empty(); }

    std::optional<std::filesystem::path> EmbeddedAssets::prepare(const std::filesystem::path &cacheDirectory) const {
        if (!available()) return std::nullopt;
        if (identifier_.empty() || identifier_.size() > 80 || !std::all_of(
                identifier_.begin(), identifier_.end(), [](const unsigned char character) {
                    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (
                               character >= '0' && character <= '9') || character == '-' || character == '_';
                }))
            throw std::runtime_error("Invalid embedded resource identifier");
        Archive archive(archive_);
        const auto count = mz_zip_reader_get_num_files(&archive.zip);
        if (count == 0 || count > 1024) throw std::runtime_error("Invalid embedded resource count");
        struct Entry {
            mz_uint index;
            mz_zip_archive_file_stat stat;
            std::filesystem::path path;
        };
        std::vector<Entry> entries;
        mz_uint64 total = 0;
        for (mz_uint index = 0; index < count; ++index) {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&archive.zip, index, &stat) || !stat.m_is_supported || stat.m_is_encrypted ||
                ((stat.m_external_attr >> 16) & 0xf000) == 0xa000)
                throw std::runtime_error("Unsupported embedded resource entry");
            const auto path = relativePath(stat.m_filename);
            if (stat.m_is_directory) continue;
            total += stat.m_uncomp_size;
            if (stat.m_uncomp_size > 32 * 1024 * 1024 || total > 64 * 1024 * 1024) throw std::runtime_error(
                "Embedded resources exceed the extraction limit");
            entries.push_back({index, stat, path});
        }
        const auto root = cacheDirectory / identifier_;
        for (const auto &entry: entries) rejectSymlinks(root, entry.path);
        std::filesystem::create_directories(root);
        for (const auto &entry: entries) materialize(archive, entry.index, entry.stat, root / entry.path);
        return root;
    }
}
