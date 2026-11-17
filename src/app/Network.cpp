#include "app/Store.hpp"

#include <cJSON.h>

#include <memory>
#include <stdexcept>

#include <curl/curl.h>

namespace ttplayer::app {
    namespace {
        std::string encode(const std::string_view text) {
            constexpr char digits[] = "0123456789ABCDEF";
            std::string result;
            for (const unsigned char character: text) {
                if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                    (character >= '0' && character <= '9') || character == '-' || character == '_' || character == '.'
                    || character == '~')
                    result += static_cast<char>(character);
                else {
                    result += '%';
                    result += digits[character >> 4];
                    result += digits[character & 15];
                }
            }
            return result;
        }

        std::string get(const std::string &endpoint) {
            static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
            if (initialized != CURLE_OK) throw std::runtime_error("Cannot initialize HTTPS");
            using Handle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
            Handle request(curl_easy_init(), curl_easy_cleanup);
            if (!request) throw std::runtime_error("Cannot create lyric request");
            struct Response {
                std::string text;
                bool tooLarge = false;
            } response;
            const auto address = "https://music.gd.cn" + endpoint;
            curl_easy_setopt(request.get(), CURLOPT_URL, address.c_str());
            curl_easy_setopt(request.get(), CURLOPT_USERAGENT, "TTPlayerCpp/1.0");
            curl_easy_setopt(request.get(), CURLOPT_REFERER, "https://music.gd.cn/");
            curl_easy_setopt(request.get(), CURLOPT_CONNECTTIMEOUT_MS, 5000L);
            curl_easy_setopt(request.get(), CURLOPT_TIMEOUT_MS, 15000L);
            curl_easy_setopt(request.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(request.get(), CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(request.get(), CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(request.get(), CURLOPT_SSL_VERIFYHOST, 2L);
#if LIBCURL_VERSION_NUM >= 0x075500
            curl_easy_setopt(request.get(), CURLOPT_PROTOCOLS_STR, "https");
#else
            curl_easy_setopt(request.get(), CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
            curl_easy_setopt(request.get(), CURLOPT_WRITEDATA, &response);
            curl_easy_setopt(request.get(), CURLOPT_WRITEFUNCTION,
                             +[](char *bytes, const std::size_t size, const std::size_t count,
                                 void *userdata) -> std::size_t {
                                 auto &output = *static_cast<Response *>(userdata);
                                 constexpr std::size_t limit = 4 * 1024 * 1024;
                                 if (size != 0 && count > (limit - output.text.size()) / size) {
                                     output.tooLarge = true;
                                     return 0;
                                 }
                                 try { output.text.append(bytes, size * count); } catch (...) { return 0; }
                                 return size * count;
                             });
            const auto result = curl_easy_perform(request.get());
            if (response.tooLarge) throw std::runtime_error("Lyric response too large");
            if (result != CURLE_OK) throw std::runtime_error(
                "Lyric request failed: " + std::string(curl_easy_strerror(result)));
            long status = 0;
            curl_easy_getinfo(request.get(), CURLINFO_RESPONSE_CODE, &status);
            if (status != 200) throw std::runtime_error("Lyric server HTTP " + std::to_string(status));
            return response.text;
        }

        using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

        Json request(const std::string &endpoint) {
            const auto response = get(endpoint);
            Json json(cJSON_ParseWithLength(response.data(), response.size()), cJSON_Delete);
            if (!json || !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json.get(), "success"))) throw
                    std::runtime_error("Lyric server returned an invalid or unsuccessful response");
            return json;
        }

        std::string field(const cJSON *object, const char *key) {
            const auto *value = cJSON_GetObjectItemCaseSensitive(object, key);
            return cJSON_IsString(value) ? value->valuestring : "";
        }
    }

    std::vector<LyricResult> searchLyrics(const std::string_view query) {
        const auto root = request("/api/lyrics/search?keyword=" + encode(query) + "&pageSize=30");
        const auto *data = cJSON_GetObjectItemCaseSensitive(root.get(), "data");
        if (!cJSON_IsArray(data)) throw std::runtime_error("Invalid lyric search data");
        std::vector<LyricResult> result;
        const cJSON *entry = nullptr;
        cJSON_ArrayForEach(entry, data)
        {
            const auto id = field(entry, "mid");
            if (!id.empty()) result.push_back({
                id, field(entry, "name"), field(entry, "singer"), field(entry, "album")
            });
        }
        return result;
    }

    std::string downloadLyrics(const std::string_view id) {
        const auto root = request("/api/lyrics/lyric?mid=" + encode(id));
        return field(root.get(), "data");
    }
}
