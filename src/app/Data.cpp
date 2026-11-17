#include "app/Data.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace ttplayer::app {
    namespace {
        void appendCodepoint(std::string &output, const unsigned int codepoint) {
            if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
                throw std::runtime_error("Invalid Unicode codepoint");
            }
            if (codepoint < 0x80) output += static_cast<char>(codepoint);
            else if (codepoint < 0x800) {
                output += static_cast<char>(0xc0 | (codepoint >> 6));
                output += static_cast<char>(0x80 | (codepoint & 63));
            } else if (codepoint < 0x10000) {
                output += static_cast<char>(0xe0 | (codepoint >> 12));
                output += static_cast<char>(0x80 | ((codepoint >> 6) & 63));
                output += static_cast<char>(0x80 | (codepoint & 63));
            } else {
                output += static_cast<char>(0xf0 | (codepoint >> 18));
                output += static_cast<char>(0x80 | ((codepoint >> 12) & 63));
                output += static_cast<char>(0x80 | ((codepoint >> 6) & 63));
                output += static_cast<char>(0x80 | (codepoint & 63));
            }
        }

        std::string unescape(const std::string_view text) {
            std::string result;
            for (std::size_t index = 0; index < text.size(); ++index) {
                if (text[index] != '&') {
                    result += text[index];
                    continue;
                }
                const auto end = text.find(';', index + 1);
                if (end == std::string_view::npos || end - index > 12) {
                    result += '&';
                    continue;
                }
                const auto entity = text.substr(index + 1, end - index - 1);
                if (entity == "amp") result += '&';
                else if (entity == "lt") result += '<';
                else if (entity == "gt") result += '>';
                else if (entity == "quot") result += '"';
                else if (entity == "apos") result += '\'';
                else if (entity.starts_with('#')) {
                    const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                    const auto digits = entity.substr(hex ? 2 : 1);
                    unsigned int codepoint = 0;
                    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), codepoint,
                                                        hex ? 16 : 10);
                    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) throw
                            std::runtime_error("Invalid XML entity");
                    appendCodepoint(result, codepoint);
                } else { result.append(text.substr(index, end - index + 1)); }
                index = end;
            }
            return result;
        }

        bool whitespace(const char value) { return value == ' ' || value == '\t' || value == '\r' || value == '\n'; }

        std::int64_t timestamp(std::string_view text) {
            const auto colon = text.find(':');
            if (colon == std::string_view::npos) return -1;
            int minutes = 0;
            const auto minute_result = std::from_chars(text.data(), text.data() + colon, minutes);
            if (minute_result.ec != std::errc{} || minute_result.ptr != text.data() + colon || minutes < 0 || minutes >
                100000) return -1;
            text.remove_prefix(colon + 1);
            const auto decimal = text.find_first_of(".:");
            const auto seconds_text = text.substr(0, decimal);
            int seconds = 0;
            const auto second_result = std::from_chars(seconds_text.data(), seconds_text.data() + seconds_text.size(),
                                                       seconds);
            if (second_result.ec != std::errc{} || second_result.ptr != seconds_text.data() + seconds_text.size() ||
                seconds < 0 || seconds > 59) return -1;
            int fraction = 0;
            if (decimal != std::string_view::npos) {
                const auto digits = text.substr(decimal + 1);
                int scale = 100;
                for (const char character: digits) {
                    if (character < '0' || character > '9') return -1;
                    if (scale > 0) {
                        fraction += (character - '0') * scale;
                        scale /= 10;
                    }
                }
            }
            return (static_cast<std::int64_t>(minutes) * 60 + seconds) * 1000 + fraction;
        }
    }

    std::filesystem::path fromUtf8(const std::string_view value) {
        return std::filesystem::path(std::u8string(value.begin(), value.end()));
    }

    std::string utf8(const std::filesystem::path &path) {
        const auto value = path.u8string();
        return {value.begin(), value.end()};
    }

    std::string lower(std::string value) {
        for (char &character: value) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        return value;
    }

    std::map<std::string, std::string> parseProperties(const std::string_view text) {
        auto unescapeProperty = [](const std::string_view input) {
            std::string output;
            for (std::size_t index = 0; index < input.size(); ++index) {
                if (input[index] != '\\' || index + 1 == input.size()) {
                    output += input[index];
                    continue;
                }
                const char escaped = input[++index];
                if (escaped == 'u' && index + 4 < input.size()) {
                    auto readHex = [&](const std::size_t start) {
                        unsigned int codepoint = 0;
                        const auto parsed = std::from_chars(input.data() + start, input.data() + start + 4, codepoint,
                                                            16);
                        if (parsed.ec != std::errc{} || parsed.ptr != input.data() + start + 4) throw
                                std::runtime_error("Invalid properties Unicode escape");
                        return codepoint;
                    };
                    auto codepoint = readHex(index + 1);
                    index += 4;
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff && index + 6 < input.size() && input[index + 1] ==
                        '\\' && input[index + 2] == 'u') {
                        const auto low = readHex(index + 3);
                        if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("Invalid properties surrogate pair");
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + low - 0xdc00;
                        index += 6;
                    }
                    appendCodepoint(output, codepoint);
                } else if (escaped == 't') output += '\t';
                else if (escaped == 'r') output += '\r';
                else if (escaped == 'n') output += '\n';
                else if (escaped == 'f') output += '\f';
                else output += escaped;
            }
            return output;
        };
        std::map<std::string, std::string> result;
        std::istringstream input{std::string(text)};
        std::string line, logical;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto start = line.find_first_not_of(" \t\f");
            if (start == std::string::npos) continue;
            if (logical.empty() && (line[start] == '#' || line[start] == '!')) continue;
            logical += line.substr(start);
            std::size_t backslashes = 0;
            for (std::size_t index = logical.size(); index > 0 && logical[index - 1] == '\\'; --index) ++backslashes;
            if (backslashes % 2 != 0) {
                logical.pop_back();
                continue;
            }
            std::size_t split = 0;
            for (; split < logical.size(); ++split) {
                if (logical[split] == '\\') { if (split + 1 < logical.size()) ++split; } else if (
                    logical[split] == '=' || logical[split] == ':' || whitespace(logical[split])) break;
            }
            auto value = split;
            while (value < logical.size() && whitespace(logical[value])) ++value;
            if (value < logical.size() && (logical[value] == '=' || logical[value] == ':')) ++value;
            while (value < logical.size() && whitespace(logical[value])) ++value;
            result[unescapeProperty(std::string_view(logical).substr(0, split))] = unescapeProperty(
                std::string_view(logical).substr(value));
            logical.clear();
        }
        return result;
    }

    std::string readFile(const std::filesystem::path &path, const std::size_t limit) {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("Cannot open " + utf8(path));
        const auto size = stream.tellg();
        if (size < 0 || static_cast<std::uint64_t>(size) > limit) throw std::runtime_error("File exceeds size limit");
        std::string result(static_cast<std::size_t>(size), '\0');
        stream.seekg(0);
        if (!stream.read(result.data(), static_cast<std::streamsize>(result.size()))) throw std::runtime_error(
            "Cannot read file");
        return result;
    }

    std::string decodeText(std::string_view bytes) {
        if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
#ifdef _WIN32
        auto convert = [](const std::wstring &wide) {
            const int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                                  nullptr, nullptr);
            std::string result(static_cast<std::size_t>(count), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), count, nullptr,
                                nullptr);
            return result;
        };
        if (bytes.size() >= 2 && ((static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1])
                                   == 0xfe)
                                  || (static_cast<unsigned char>(bytes[0]) == 0xfe && static_cast<unsigned char>(bytes[
                                          1]) == 0xff))) {
            const bool little_endian = static_cast<unsigned char>(bytes[0]) == 0xff;
            std::wstring wide;
            for (std::size_t index = 2; index + 1 < bytes.size(); index += 2) {
                const auto first = static_cast<unsigned char>(bytes[index]);
                const auto second = static_cast<unsigned char>(bytes[index + 1]);
                wide += static_cast<wchar_t>(little_endian ? first | (second << 8) : second | (first << 8));
            }
            return convert(wide);
        }
        if (bytes.empty()) return {};
        const auto declaration = lower(std::string(bytes.substr(0, std::min<std::size_t>(bytes.size(), 160))));
        const bool declared_gbk = declaration.find("encoding=") != std::string::npos &&
                                  (declaration.find("gbk") != std::string::npos || declaration.find("gb2312") !=
                                   std::string::npos);
        const int valid_utf8 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                                   static_cast<int>(bytes.size()), nullptr, 0);
        if (!declared_gbk && valid_utf8 > 0) return std::string(bytes);
        const int count = MultiByteToWideChar(936, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        std::wstring wide(static_cast<std::size_t>(count), L'\0');
        MultiByteToWideChar(936, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), count);
        return convert(wide);
#else
        if (bytes.empty()) return {};
        auto convert = [&](const char *encoding, const std::size_t skip = 0) -> std::optional<std::string> {
            const auto converter = iconv_open("UTF-8", encoding);
            if (converter == reinterpret_cast<iconv_t>(-1)) return {};
            struct Close {
                iconv_t converter;
                ~Close() { iconv_close(converter); }
            } close{converter};
            auto *input = const_cast<char *>(bytes.data() + skip);
            auto inputSize = bytes.size() - skip;
            std::string result(inputSize * 4 + 4, '\0');
            auto *output = result.data();
            auto outputSize = result.size();
            if (iconv(converter, &input, &inputSize, &output, &outputSize) == static_cast<std::size_t>(-1)) return {};
            result.resize(result.size() - outputSize);
            return result;
        };
        if (bytes.starts_with("\xff\xfe")) {
            if (auto result = convert("UTF-16LE", 2)) return *result;
            throw std::runtime_error("Invalid UTF-16LE text");
        }
        if (bytes.starts_with("\xfe\xff")) {
            if (auto result = convert("UTF-16BE", 2)) return *result;
            throw std::runtime_error("Invalid UTF-16BE text");
        }
        const auto declaration = lower(std::string(bytes.substr(0, std::min<std::size_t>(bytes.size(), 160))));
        const bool declaredGbk = declaration.find("encoding=") != std::string::npos &&
                                 (declaration.find("gbk") != std::string::npos || declaration.find("gb2312") !=
                                  std::string::npos);
        if (!declaredGbk) if (auto result = convert("UTF-8")) return *result;
        if (auto result = convert("GB18030")) return *result;
        throw std::runtime_error("Unsupported or invalid text encoding");
#endif
    }

    void writeFileAtomic(const std::filesystem::path &path, const std::string_view text) {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream || !stream.write(text.data(), static_cast<std::streamsize>(text.size())) || !stream.flush())
                throw std::runtime_error("Cannot save " + utf8(path));
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw
                std::runtime_error("Cannot replace " + utf8(path));
#else
        std::filesystem::rename(temporary, path);
#endif
    }

    std::string timeText(const std::int64_t milliseconds) {
        const auto seconds = std::max<std::int64_t>(0, milliseconds / 1000);
        std::ostringstream output;
        output << seconds / 60 << ':' << std::setw(2) << std::setfill('0') << seconds % 60;
        return output.str();
    }

    bool isAudio(const std::filesystem::path &path) {
        static constexpr std::string_view extensions = ";mp3;flac;wav;ogg;opus;wma;aac;m4a;mp4;ape;wv;aiff;aif;";
        const auto extension = lower(utf8(path.extension()));
        return extension.size() > 1 && extensions.find(";" + extension.substr(1) + ";") != std::string_view::npos;
    }

    std::string XmlNode::value(const std::string_view key, std::string fallback) const {
        const auto found = attributes.find(std::string(key));
        return found == attributes.end() ? std::move(fallback) : found->second;
    }

    XmlNode parseXml(const std::string_view text) {
        XmlNode document{"document", {}, {}};
        std::vector<XmlNode *> stack{&document};
        std::size_t cursor = 0, nodes = 0;
        auto skip = [&] { while (cursor < text.size() && whitespace(text[cursor])) ++cursor; };
        auto name = [&] {
            const auto start = cursor;
            while (cursor < text.size() && !whitespace(text[cursor]) && text[cursor] != '=' && text[cursor] != '>' &&
                   text[cursor] != '/') ++cursor;
            if (cursor == start) throw std::runtime_error("Invalid XML name");
            return lower(std::string(text.substr(start, cursor - start)));
        };
        while ((cursor = text.find('<', cursor)) != std::string_view::npos) {
            if (text.substr(cursor).starts_with("<!--")) {
                const auto end = text.find("-->", cursor + 4);
                if (end == std::string_view::npos) throw std::runtime_error("Unterminated XML comment");
                cursor = end + 3;
                continue;
            }
            if (text.substr(cursor).starts_with("<?")) {
                const auto end = text.find("?>", cursor + 2);
                if (end == std::string_view::npos) throw std::runtime_error("Unterminated XML instruction");
                cursor = end + 2;
                continue;
            }
            if (text.substr(cursor).starts_with("<!")) throw std::runtime_error(
                "XML declarations and external entities are not supported");
            ++cursor;
            if (cursor < text.size() && text[cursor] == '/') {
                ++cursor;
                const auto closing = name();
                skip();
                if (stack.size() < 2 || closing != stack.back()->name || cursor >= text.size() || text[cursor++] != '>')
                    throw std::runtime_error("Mismatched XML close tag");
                stack.pop_back();
                continue;
            }
            if (++nodes > 150000 || stack.size() > 64) throw std::runtime_error("XML nesting or node limit exceeded");
            XmlNode node{name(), {}, {}};
            skip();
            while (cursor < text.size() && text[cursor] != '/' && text[cursor] != '>') {
                const auto key = name();
                skip();
                if (cursor >= text.size() || text[cursor++] != '=') throw std::runtime_error(
                    "Missing XML attribute value");
                skip();
                if (cursor >= text.size()) throw std::runtime_error("Missing XML attribute value");
                const char quote = text[cursor];
                std::string_view value;
                if (quote == '\'' || quote == '"') {
                    const auto start = ++cursor;
                    const auto end = text.find(quote, start);
                    if (end == std::string_view::npos) throw std::runtime_error("Unterminated XML attribute");
                    value = text.substr(start, end - start);
                    cursor = end + 1;
                } else {
                    const auto start = cursor;
                    while (cursor < text.size() && !whitespace(text[cursor]) && text[cursor] != '>' && text[cursor] !=
                           '/') ++cursor;
                    value = text.substr(start, cursor - start);
                }
                node.attributes[key] = unescape(value);
                skip();
            }
            const bool self_closing = cursor < text.size() && text[cursor] == '/';
            if (self_closing) ++cursor;
            if (cursor >= text.size() || text[cursor++] != '>') throw std::runtime_error("Unterminated XML tag");
            auto &siblings = stack.back()->children;
            siblings.push_back(std::move(node));
            if (!self_closing) stack.push_back(&siblings.back());
        }
        if (stack.size() != 1 || document.children.size() != 1) throw std::runtime_error("Invalid XML document");
        return std::move(document.children.front());
    }

    std::vector<Playlist> importPlaylists(const std::filesystem::path &path) {
        const auto root = parseXml(decodeText(readFile(path)));
        if (root.name != "ttplayer_playlists") throw std::runtime_error("Not a TTPlayer playlist");
        std::vector<Playlist> lists;
        for (const auto &item: root.children) {
            if (item.name != "playlist") continue;
            Playlist list{item.value("name", "Playlist"), {}};
            for (const auto &entry: item.children) {
                if (entry.name != "song" || entry.value("path").empty()) continue;
                Song song;
                song.path = fromUtf8(entry.value("path"));
                if (song.path.is_relative()) song.path = path.parent_path() / song.path;
                song.title = entry.value("title", utf8(song.path.stem()));
                song.artist = entry.value("artist");
                song.album = entry.value("album");
                song.duration = std::max<std::int64_t>(0, timestamp(entry.value("duration")));
                list.songs.push_back(std::move(song));
            }
            lists.push_back(std::move(list));
        }
        return lists;
    }

    std::vector<Song> importM3u(const std::filesystem::path &path) {
        const auto content = decodeText(readFile(path));
        std::istringstream input(content);
        std::string line, title;
        std::vector<Song> result;
        if (lower(utf8(path.extension())) == ".pls") {
            std::map<std::string, std::string> entries;
            while (std::getline(input, line)) {
                const auto equal = line.find('=');
                if (equal == std::string::npos) continue;
                const auto trim = [](std::string value) {
                    const auto start = value.find_first_not_of(" \t\r");
                    if (start == std::string::npos) return std::string{};
                    return value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
                };
                entries[lower(trim(line.substr(0, equal)))] = trim(line.substr(equal + 1));
            }
            std::map<int, std::string> files;
            for (const auto &[key, value]: entries)
                if (key.starts_with("file")) {
                    const auto suffix = key.substr(4);
                    if (suffix.empty() || suffix.find_first_not_of("0123456789") != std::string::npos || value.empty())
                        continue;
                    try {
                        const int index = std::stoi(suffix);
                        if (index > 0) files.emplace(index, value);
                    } catch (const std::exception &) {
                    }
                }
            for (const auto &[index, value]: files) {
                auto file = fromUtf8(value);
                const bool network = value.starts_with("https://") || value.starts_with("http://");
                if (!network && file.is_relative()) file = path.parent_path() / file;
                std::string title;
                for (const auto &[key, text]: entries) if (lower(key) == "title" + std::to_string(index)) title = text;
                result.push_back({
                    network ? file : file.lexically_normal(), title.empty() ? utf8(file.stem()) : title, {}, {}, 0
                });
            }
            return result;
        }
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("#EXTINF:")) {
                const auto comma = line.find(',');
                title = comma == std::string::npos ? "" : line.substr(comma + 1);
            } else if (!line.empty() && line[0] != '#') {
                auto file = fromUtf8(line);
                const bool network = line.starts_with("https://") || line.starts_with("http://");
                if (!network && file.is_relative()) file = path.parent_path() / file;
                result.push_back({
                    network ? file : file.lexically_normal(), title.empty() ? utf8(file.stem()) : title, {}, {}, 0
                });
                title.clear();
            }
        }
        return result;
    }

    void exportM3u(const std::filesystem::path &path, const std::vector<Song> &songs) {
        std::string output = "#EXTM3U\n";
        for (const auto &song: songs) {
            auto title = song.title;
            title.erase(std::remove(title.begin(), title.end(), '\n'), title.end());
            title.erase(std::remove(title.begin(), title.end(), '\r'), title.end());
            const auto raw = utf8(song.path);
            const auto file = raw.starts_with("https://") || raw.starts_with("http://")
                                  ? raw
                                  : utf8(std::filesystem::absolute(song.path));
            if (file.find_first_of("\r\n") != std::string::npos) continue;
            output += "#EXTINF:" + std::to_string(song.duration / 1000) + "," + title + "\n" + file + "\n";
        }
        writeFileAtomic(path, output);
    }

    Song probeSong(const std::filesystem::path &path, const std::atomic<bool> *cancellation) {
        Song song{path, utf8(path.stem()), {}, {}, 0};
        struct Interrupt {
            const std::atomic<bool> *cancellation;
            std::chrono::steady_clock::time_point deadline;
        };
        Interrupt interrupt{cancellation, std::chrono::steady_clock::now() + std::chrono::seconds(8)};
        AVFormatContext *context = avformat_alloc_context();
        if (!context) return song;
        context->interrupt_callback = {
            [](void *opaque) -> int {
                const auto &state = *static_cast<Interrupt *>(opaque);
                return (state.cancellation && state.cancellation->load()) || std::chrono::steady_clock::now() > state.
                       deadline
                           ? 1
                           : 0;
            },
            &interrupt
        };
        context->probesize = 2 * 1024 * 1024;
        context->max_analyze_duration = 3 * AV_TIME_BASE;
        const auto name = utf8(path);
        if (avformat_open_input(&context, name.c_str(), nullptr, nullptr) < 0) {
            avformat_close_input(&context);
            return song;
        }
        std::unique_ptr<AVFormatContext, void(*)(AVFormatContext *)>
        cleanup(context, [](AVFormatContext *value) { avformat_close_input(&value); });
        if (avformat_find_stream_info(context, nullptr) < 0) return song;
        if (context->duration > 0) song.duration = context->duration / (AV_TIME_BASE / 1000);
        auto field = [&](const char *key) {
            const auto *entry = av_dict_get(context->metadata, key, nullptr, 0);
            return entry ? decodeText(entry->value) : std::string{};
        };
        if (auto title = field("title"); !title.empty()) song.title = std::move(title);
        song.artist = field("artist");
        song.album = field("album");
        return song;
    }

    void Lyrics::parse(const std::string_view content) {
        lines.clear();
        metadata.clear();
        offset = 0;
        std::istringstream input(decodeText(content));
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::vector<std::int64_t> times;
            std::size_t cursor = 0;
            while (cursor < line.size() && line[cursor] == '[') {
                const auto end = line.find(']', cursor + 1);
                if (end == std::string::npos) break;
                const auto tag = line.substr(cursor + 1, end - cursor - 1);
                const auto parsed = timestamp(tag);
                if (parsed >= 0) times.push_back(parsed);
                else if (const auto colon = tag.find(':'); colon != std::string::npos) {
                    const auto key = lower(tag.substr(0, colon));
                    const auto value = tag.substr(colon + 1);
                    metadata[key] = value;
                    if (key == "offset") {
                        std::int64_t parsed_offset = 0;
                        const auto *start = value.data() + (!value.empty() && value[0] == '+' ? 1 : 0);
                        const auto converted = std::from_chars(start, value.data() + value.size(), parsed_offset);
                        if (converted.ec == std::errc{}) offset = std::clamp<std::int64_t>(
                                                             parsed_offset, -3600000, 3600000);
                    }
                }
                cursor = end + 1;
            }
            if (times.empty()) continue;
            LyricLine entry;
            std::int64_t word_time = times.front();
            std::string fragment;
            auto flush = [&] {
                if (!fragment.empty()) {
                    entry.words.push_back({word_time, fragment});
                    entry.text += fragment;
                    fragment.clear();
                }
            };
            while (cursor < line.size()) {
                if (line[cursor] == '<') {
                    const auto end = line.find('>', cursor + 1);
                    const auto parsed = end == std::string::npos
                                            ? -1
                                            : timestamp(std::string_view(line).substr(cursor + 1, end - cursor - 1));
                    if (parsed >= 0) {
                        flush();
                        word_time = parsed;
                        cursor = end + 1;
                        continue;
                    }
                }
                fragment += line[cursor++];
            }
            flush();
            for (const auto time: times) {
                auto copy = entry;
                copy.time = time;
                for (auto &word: copy.words) word.time += time - times.front();
                lines.push_back(std::move(copy));
            }
        }
        std::stable_sort(lines.begin(), lines.end(), [](const auto &first, const auto &second) {
            return first.time < second.time;
        });
    }

    int Lyrics::active(const std::int64_t position) const {
        const auto found = std::upper_bound(lines.begin(), lines.end(), position + offset,
                                            [](const auto time, const auto &line) { return time < line.time; });
        return static_cast<int>(found - lines.begin()) - 1;
    }

    std::string Lyrics::serialize() const {
        auto tag = [](const std::int64_t value) {
            const auto time = std::max<std::int64_t>(0, value);
            std::ostringstream output;
            output << std::setw(2) << std::setfill('0') << time / 60000 << ':' << std::setw(2) << (time / 1000) % 60 <<
                    '.' << std::setw(3) << time % 1000;
            return output.str();
        };
        std::string output;
        for (const auto &[key, value]: metadata) if (
            key != "offset" && key.find_first_of("[]\r\n") == std::string::npos && value.find_first_of("[]\r\n") ==
            std::string::npos) output += "[" + key + ":" + value + "]\n";
        output += "[offset:" + std::to_string(offset) + "]\n";
        for (const auto &line: lines) {
            output += "[" + tag(line.time) + "]";
            if (line.words.size() > 1) for (const auto &word: line.words)
                output += "<" + tag(word.time) + ">" + word.text;
            else output += line.text;
            output += '\n';
        }
        return output;
    }

    float Lyrics::progress(const int index, const std::int64_t position, const std::int64_t duration) const {
        if (index < 0 || index >= static_cast<int>(lines.size())) return 0;
        const auto &line = lines[static_cast<std::size_t>(index)];
        const auto end = index + 1 < static_cast<int>(lines.size())
                             ? lines[static_cast<std::size_t>(index + 1)].time
                             : std::max(duration, line.time + 5000);
        const auto now = position + offset;
        if (line.words.size() <= 1 || line.text.empty()) return std::clamp(
            static_cast<float>(now - line.time) / static_cast<float>(std::max<std::int64_t>(1, end - line.time)), 0.0F,
            1.0F);
        float completed = 0;
        for (std::size_t word_index = 0; word_index < line.words.size(); ++word_index) {
            const auto &word = line.words[word_index];
            const auto next = word_index + 1 < line.words.size() ? line.words[word_index + 1].time : end;
            const float ratio = std::clamp(
                static_cast<float>(now - word.time) / static_cast<float>(std::max<std::int64_t>(1, next - word.time)),
                0.0F, 1.0F);
            completed += static_cast<float>(word.text.size()) * ratio;
        }
        return std::clamp(completed / static_cast<float>(line.text.size()), 0.0F, 1.0F);
    }
}
