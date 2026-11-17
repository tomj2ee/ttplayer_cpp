#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
struct Entry { fs::path source; std::string name; };

std::string utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}

void pack(const fs::path& resources, const fs::path& thirdparty, const fs::path& output) {
    std::vector<Entry> entries;
    for (const auto* directory : {"skin", "ico"}) {
        for (const auto& file : fs::recursive_directory_iterator(resources / directory)) {
            if (file.is_symlink()) throw std::runtime_error("Resource source contains a symbolic link");
            if (file.is_regular_file()) entries.push_back({file.path(), utf8(file.path().lexically_relative(resources))});
        }
    }
    for (const auto& file : fs::directory_iterator(resources)) {
        const auto name = utf8(file.path().filename());
        if (file.is_regular_file() && name.starts_with("messages") && name.ends_with(".properties")) entries.push_back({file.path(), name});
    }
    const std::pair<const char*, const char*> licenses[]{
        {"sdl3-src/LICENSE.txt", "SDL3.txt"}, {"sdl_ttf-src/LICENSE.txt", "SDL3_ttf.txt"}, {"curl-src/COPYING", "curl.txt"},
        {"ffmpeg-src/COPYING.LGPLv2.1", "FFmpeg-LGPLv2.1.txt"}, {"ffmpeg-src/LICENSE.md", "FFmpeg-LICENSE.md"},
        {"sdl_ttf-src/external/freetype/docs/FTL.TXT", "FreeType-FTL.txt"},
        {"sdl_ttf-src/external/freetype/docs/GPLv2.TXT", "FreeType-GPLv2.txt"},
        {"cJSON.h", "cJSON-header.txt"}, {"miniz.c", "miniz-source.txt"}
    };
    for (const auto& [source, name] : licenses) entries.push_back({thirdparty / source, std::string("licenses/") + name});
    std::sort(entries.begin(), entries.end(), [](const Entry& first, const Entry& second) { return first.name < second.name; });
    mz_zip_archive archive{};
    if (!mz_zip_writer_init_heap(&archive, 0, 0)) throw std::runtime_error("Cannot initialize embedded resource archive");
    try {
        MZ_TIME_T timestamp = 315532800;
        for (const auto& entry : entries) {
            std::ifstream input(entry.source, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read resource: " + entry.name);
            const std::vector<char> content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (input.bad()) throw std::runtime_error("Cannot finish reading resource: " + entry.name);
            if (!mz_zip_writer_add_mem_ex_v2(&archive, entry.name.c_str(), content.data(), content.size(), nullptr, 0,
                MZ_BEST_COMPRESSION, 0, 0, &timestamp, nullptr, 0, nullptr, 0)) throw std::runtime_error("Cannot archive resource: " + entry.name);
        }
        void* buffer = nullptr; std::size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size)) throw std::runtime_error("Cannot finalize embedded resource archive");
        try {
            std::ofstream stream;
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            stream.open(output, std::ios::binary | std::ios::trunc);
            stream.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
            stream.close();
        } catch (...) { mz_free(buffer); throw; }
        mz_free(buffer); mz_zip_writer_end(&archive);
    } catch (...) { mz_zip_writer_end(&archive); throw; }
}
}

template<class Character>
int run(const int argc, Character** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Expected resource root, thirdparty root and output archive");
        pack(fs::path(argv[1]), fs::path(argv[2]), fs::path(argv[3]));
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

#ifdef _WIN32
int wmain(const int argc, wchar_t** argv) { return run(argc, argv); }
#else
int main(const int argc, char** argv) { return run(argc, argv); }
#endif
