#include "app/Skin.hpp"
#ifdef __APPLE__
#include "app/PlatformMac.hpp"
#endif

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ttplayer::app {
    namespace {
        SDL_Surface *loadIcon(const std::string &content, const int iconSize = 16) {
#ifdef _WIN32
            auto readWord = [&](const std::size_t offset) {
                return static_cast<unsigned char>(content[offset]) | (
                           static_cast<unsigned char>(content[offset + 1]) << 8);
            };
            auto readDword = [&](const std::size_t offset) {
                std::uint32_t result = 0;
                for (std::size_t byte = 0; byte < 4; ++byte)
                    result |= static_cast<std::uint32_t>(static_cast<unsigned char>(content[offset + byte])) << (
                        byte * 8);
                return result;
            };
            if (content.size() < 22 || readWord(0) != 0 || readWord(2) != 1) return nullptr;
            const auto count = readWord(4);
            if (count <= 0 || count > 256 || static_cast<std::size_t>(6 + count * 16) > content.size()) return nullptr;
            std::size_t selected = 6;
            int bestDistance = 1024, bestArea = 0, bestDepth = 0;
            for (int index = 0; index < count; ++index) {
                const auto offset = static_cast<std::size_t>(6 + index * 16);
                const int width = content[offset] ? static_cast<unsigned char>(content[offset]) : 256;
                const int height = content[offset + 1] ? static_cast<unsigned char>(content[offset + 1]) : 256;
                const int distance = std::abs(width - iconSize) + std::abs(height - iconSize);
                const int area = width * height, depth = readWord(offset + 6);
                if (distance < bestDistance || (distance == bestDistance &&
                    (area > bestArea || (area == bestArea && depth > bestDepth)))) {
                    selected = offset;
                    bestDistance = distance;
                    bestArea = area;
                    bestDepth = depth;
                }
            }
            const auto size = readDword(selected + 8), offset = readDword(selected + 12);
            if (size == 0 || offset > content.size() || size > content.size() - offset) return nullptr;
            auto *icon = CreateIconFromResourceEx(reinterpret_cast<PBYTE>(const_cast<char *>(content.data() + offset)),
                                                  size, TRUE, 0x00030000, iconSize, iconSize, LR_DEFAULTCOLOR);
            if (!icon) return nullptr;
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = iconSize; info.bmiHeader.biHeight = -iconSize;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            auto *device = CreateCompatibleDC(nullptr);
            void *pixels = nullptr;
            auto *bitmap = CreateDIBSection(device, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            SDL_Surface *surface = nullptr;
            if (device && bitmap && pixels) {
                const auto previous = SelectObject(device, bitmap);
                std::vector<unsigned char> black(iconSize * iconSize * 4);
                std::memset(pixels, 0, black.size());
                DrawIconEx(device, 0, 0, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                GdiFlush();
                std::memcpy(black.data(), pixels, black.size());
                std::memset(pixels, 255, black.size());
                DrawIconEx(device, 0, 0, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
                GdiFlush();
                surface = SDL_CreateSurface(iconSize, iconSize, SDL_PIXELFORMAT_RGBA32);
                if (surface)
                    for (int index = 0; index < iconSize * iconSize; ++index) {
                        const auto *white = static_cast<unsigned char *>(pixels) + index * 4;
                        const int alpha = 255 - std::max({
                                              0, white[0] - black[index * 4], white[1] - black[index * 4 + 1],
                                              white[2] - black[index * 4 + 2]
                                          });
                        auto *target = static_cast<unsigned char *>(surface->pixels) + (index / iconSize) * surface->pitch + (
                                           index % iconSize) * 4;
                        for (int channel = 0; channel < 3; ++channel) target[channel] = alpha
                                                                          ? static_cast<unsigned char>(std::min(
                                                                              255,
                                                                              black[index * 4 + 2 - channel] * 255 /
                                                                              alpha))
                                                                          : 0;
                        target[3] = static_cast<unsigned char>(alpha);
                    }
                SelectObject(device, previous);
            }
            if (bitmap) DeleteObject(bitmap);
            if (device) DeleteDC(device);
            DestroyIcon(icon);
            return surface;
#elif defined(__APPLE__)
            return loadMacIcon(content, iconSize);
#else
            static_cast<void>(content);
            static_cast<void>(iconSize);
            return nullptr;
#endif
        }
    }

    SDL_Color parseColor(std::string_view text, const SDL_Color fallback) {
        if (text.starts_with('#')) text.remove_prefix(1);
        unsigned int value = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || text.size() != 6) return fallback;
        return {static_cast<Uint8>(value >> 16), static_cast<Uint8>(value >> 8), static_cast<Uint8>(value), 255};
    }

    SDL_FRect parseRect(std::string text) {
        std::replace(text.begin(), text.end(), ',', ' ');
        float left = 0, top = 0, right = 0, bottom = 0;
        std::istringstream stream(text);
        stream >> left >> top >> right >> bottom;
        return {left, top, std::max(0.0F, right - left), std::max(0.0F, bottom - top)};
    }

    std::string SkinControl::value(const std::string_view key, std::string fallback) const {
        const auto found = attributes.find(std::string(key));
        return found == attributes.end() ? std::move(fallback) : found->second;
    }

    const SkinControl *SkinWindow::control(const std::string_view name) const {
        const auto found = std::find_if(controls.begin(), controls.end(),
                                        [&](const auto &item) { return item.name == name; });
        return found == controls.end() ? nullptr : &*found;
    }

    const SkinWindow *Skin::window(const std::string_view name) const {
        const auto found = windows_.find(std::string(name));
        return found == windows_.end() ? nullptr : &found->second;
    }

    const XmlNode &Skin::style(const std::string_view name) const {
        static const XmlNode empty;
        const auto found = styles_.find(lower(std::string(name)));
        return found == styles_.end() ? empty : found->second;
    }

    SDL_FRect controlBounds(const SkinWindow &window, const SkinControl &control, const int width, const int height,
                            const float imageWidth, const float imageHeight) {
        auto bounds = control.bounds;
        if (imageWidth > 0 && imageHeight > 0) {
            bounds.w = imageWidth;
            bounds.h = imageHeight;
        }
        const auto align = lower(control.value("align"));
        auto has = [&](const std::initializer_list<std::string_view> tokens) {
            return std::any_of(tokens.begin(), tokens.end(), [&](const auto token) {
                return align.find(token) != std::string::npos;
            });
        };
        if (has({"fill", "stretch", "hfill", "xfill", "width", "expand"})) bounds.w = std::max(
                                                                               1.0F,
                                                                               control.bounds.w + width - window.width);
        else if (has({"right"})) bounds.x = width - (window.width - control.bounds.x - control.bounds.w) - bounds.w;
        else if (has({"center"}) && !has({"centery", "centervert", "vcenter"})) bounds.x = (width - bounds.w) / 2;
        if (has({"fill", "stretch", "vfill", "yfill", "height", "expand"})) bounds.h = std::max(
                                                                                1.0F,
                                                                                control.bounds.h + height - window.
                                                                                height);
        else if (has({"bottom"})) bounds.y = height - (window.height - control.bounds.y - control.bounds.h) - bounds.h;
        else if (has({"middle", "centery", "centervert", "vertcenter", "vcenter"})) bounds.y = (height - bounds.h) / 2;
        return bounds;
    }

    SDL_Surface *Skin::image(const std::string_view name) const {
        auto key = lower(std::string(name));
        std::replace(key.begin(), key.end(), '\\', '/');
        const auto found = images_.find(key);
        return found == images_.end() ? nullptr : found->second.get();
    }

    void Skin::load(const std::filesystem::path &path, const std::filesystem::path &fallbackIcon) {
        const auto bytes = readFile(path, 64 * 1024 * 1024);
        mz_zip_archive archive{};
        if (!mz_zip_reader_init_mem(&archive, bytes.data(), bytes.size(), 0)) throw std::runtime_error(
            "Invalid SKN ZIP archive");
        struct Cleanup {
            mz_zip_archive *archive;
            ~Cleanup() { mz_zip_reader_end(archive); }
        } cleanup{&archive};
        const auto count = mz_zip_reader_get_num_files(&archive);
        if (count > 4096) throw std::runtime_error("Too many skin entries");
        std::map<std::string, mz_uint> entries;
        std::uint64_t total = 0;
        for (mz_uint index = 0; index < count; ++index) {
            mz_zip_archive_file_stat info{};
            if (!mz_zip_reader_file_stat(&archive, index, &info)) throw std::runtime_error("Invalid skin entry");
            total += info.m_uncomp_size;
            if (info.m_uncomp_size > 32 * 1024 * 1024 || total > 128 * 1024 * 1024) throw std::runtime_error(
                "Skin exceeds decompression limits");
            auto key = lower(info.m_filename);
            std::replace(key.begin(), key.end(), '\\', '/');
            if (key.starts_with('/') || key.find("..") != std::string::npos || key.find(':') != std::string::npos) throw
                    std::runtime_error("Unsafe skin entry name");
            entries.emplace(std::move(key), index);
        }
        const auto xml_entry = std::find_if(entries.begin(), entries.end(), [](const auto &entry) {
            return entry.first == "skin.xml" || entry.first.ends_with("/skin.xml");
        });
        if (xml_entry == entries.end()) throw std::runtime_error("Skin.xml not found");
        auto extract = [&](const mz_uint index) {
            std::size_t size = 0;
            void *data = mz_zip_reader_extract_to_heap(&archive, index, &size, 0);
            if (!data) throw std::runtime_error("Cannot extract skin entry");
            std::string result(static_cast<const char *>(data), size);
            mz_free(data);
            return result;
        };
        const auto root = parseXml(decodeText(extract(xml_entry->second)));
        if (root.name != "skin") throw std::runtime_error("Invalid skin root");
        const auto slash = xml_entry->first.rfind('/');
        const auto prefix = slash == std::string::npos ? std::string{} : xml_entry->first.substr(0, slash + 1);
        const SDL_Color transparency = parseColor(root.value("transparent_color", "#ff00ff"));
        std::map<std::string, std::shared_ptr<SDL_Surface> > images;
        std::shared_ptr<SDL_Surface> windowIcon;
        std::map<std::string, SkinWindow> windows;
        std::map<std::string, XmlNode> styles;
        for (const auto *section: {"playlist", "lyric", "visual"}) {
            auto entry = entries.find(prefix + section + ".xml");
            if (entry == entries.end()) entry = entries.find(std::string(section) + ".xml");
            if (entry == entries.end()) continue;
            try {
                const auto document = parseXml(decodeText(extract(entry->second)));
                if (document.name == section) styles[section] = document;
                else for (const auto &child: document.children) if (child.name == section) styles[section] = child;
            } catch (const std::exception &) {
            }
        }
        auto loadImage = [&](std::string name) -> SDL_Surface * {
            name = lower(name);
            std::replace(name.begin(), name.end(), '\\', '/');
            if (const auto existing = images.find(name); existing != images.end()) return existing->second.get();
            auto entry = entries.find(prefix + name);
            if (entry == entries.end()) entry = entries.find(name);
            if (entry == entries.end()) {
                if (name != "ttplayer.ico") return nullptr;
                const auto iconPath = fallbackIcon.empty()
                                          ? path.parent_path().parent_path() / "ico/TTPlayer.ico"
                                          : fallbackIcon;
                if (!std::filesystem::is_regular_file(iconPath)) return nullptr;
                const auto content = readFile(iconPath, 4 * 1024 * 1024);
                auto *icon = loadIcon(content);
                windowIcon.reset(loadIcon(content, 48), SDL_DestroySurface);
                if (icon) images[name] = std::shared_ptr<SDL_Surface>(icon, SDL_DestroySurface);
                return icon;
            }
            const auto content = extract(entry->second);
            if (name.ends_with(".ico")) {
                auto *icon = loadIcon(content);
                if (name == "ttplayer.ico") windowIcon.reset(loadIcon(content, 48), SDL_DestroySurface);
                if (icon) images[name] = std::shared_ptr<SDL_Surface>(icon, SDL_DestroySurface);
                return icon;
            }
            if (!name.ends_with(".bmp")) return nullptr;
            if (content.size() < 26 || content[0] != 'B' || content[1] != 'M') return nullptr;
            const auto dimension = [&](const std::size_t offset) {
                std::uint32_t value = 0;
                for (std::size_t byte = 0; byte < 4; ++byte)
                    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(content[offset + byte])) << (
                        byte * 8);
                return static_cast<std::int32_t>(value);
            };
            const auto headerSize = dimension(14);
            const auto imageWidth = headerSize == 12
                                        ? static_cast<std::int32_t>(
                                            static_cast<unsigned char>(content[18]) | (
                                                static_cast<unsigned char>(content[19]) << 8))
                                        : dimension(18);
            const auto imageHeight = headerSize == 12
                                         ? static_cast<std::int32_t>(
                                             static_cast<unsigned char>(content[20]) | (
                                                 static_cast<unsigned char>(content[21]) << 8))
                                         : dimension(22);
            if (imageWidth <= 0 || imageWidth > 8192 || imageHeight == 0 || imageHeight < -8192 || imageHeight > 8192 ||
                static_cast<std::int64_t>(imageWidth) * std::abs(static_cast<std::int64_t>(imageHeight)) > 16 * 1024 *
                1024) throw std::runtime_error("Skin image dimensions exceed safety limits");
            SDL_IOStream *stream = SDL_IOFromConstMem(content.data(), content.size());
            SDL_Surface *bitmap = stream ? SDL_LoadBMP_IO(stream, true) : nullptr;
            if (!bitmap) return nullptr;
            SDL_Surface *converted = SDL_ConvertSurface(bitmap, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(bitmap);
            if (!converted) return nullptr;
            if (converted->w > 8192 || converted->h > 8192) {
                SDL_DestroySurface(converted);
                throw std::runtime_error("Skin image dimensions too large");
            }
            SDL_SetSurfaceColorKey(converted, true,
                                   SDL_MapSurfaceRGB(converted, transparency.r, transparency.g, transparency.b));
            images[name] = std::shared_ptr<SDL_Surface>(converted, SDL_DestroySurface);
            return converted;
        };
        for (const auto &node: root.children) {
            if (!node.name.ends_with("_window") && node.name != "desklrc_bar") continue;
            SkinWindow window;
            window.image = node.value("image");
            const auto *background = loadImage(window.image);
            if (!background) continue;
            window.width = background->w;
            window.height = background->h;
            window.resize = parseRect(node.value("resize_rect"));
            window.position = parseRect(node.value("position"));
            window.tiled = node.value("resize_tile") == "1" || node.value("resize_tile") == "true";
            const auto interval = node.value("eq_interval", "2");
            std::from_chars(interval.data(), interval.data() + interval.size(), window.interval);
            for (const auto &child: node.children) {
                SkinControl control{child.name, parseRect(child.value("position")), child.attributes};
                if (control.name == "icon") loadImage("TTPlayer.ico");
                if (control.name == "eqfactor") {
                    for (int band = 0; band < 10; ++band) {
                        auto copy = control;
                        copy.name = "band" + std::to_string(band);
                        copy.bounds.x += band * (control.bounds.w + window.interval);
                        window.controls.push_back(std::move(copy));
                    }
                } else window.controls.push_back(control);
                for (const auto &[key, value]: child.attributes) {
                    if ((key.find("image") != std::string::npos || key == "icon") && !value.empty()) loadImage(value);
                }
            }
            windows.emplace(node.name, std::move(window));
        }
        if (!windows.contains("player_window")) throw std::runtime_error("Skin has no usable player window");
        path_ = path;
        name_ = root.value("name", utf8(path.stem()));
        author_ = root.value("author");
        windows_ = std::move(windows);
        images_ = std::move(images);
        windowIcon_ = std::move(windowIcon);
        styles_ = std::move(styles);
    }
}
