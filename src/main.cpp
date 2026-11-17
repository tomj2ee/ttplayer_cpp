#include "app/Application.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <iostream>
#include <string_view>

namespace {
    int checkPlayback(const int argc, char **argv) {
        std::filesystem::path file;
        for (int index = 1; index < argc; ++index) {
            if (std::string_view(argv[index]) == "--exit-on-complete") continue;
            if (std::string_view(argv[index]).starts_with("--") || !file.empty()) {
                std::cerr << "Usage: ttplayer_cpp --exit-on-complete <audio-file>\n";
                return 2;
            }
            file = ttplayer::app::fromUtf8(argv[index]);
        }
        if (file.empty()) {
            std::cerr << "--exit-on-complete requires an audio file\n";
            return 2;
        }
        if (!SDL_Init(SDL_INIT_AUDIO)) {
            SDL_Log("SDL audio: %s", SDL_GetError());
            return 1;
        }
        int result = 0;
        {
            ttplayer::media::AudioEngine engine;
            engine.play(file);
            while (engine.state() == ttplayer::media::PlaybackState::loading || engine.state() ==
                   ttplayer::media::PlaybackState::playing) SDL_Delay(10);
            if (engine.state() == ttplayer::media::PlaybackState::error) {
                SDL_Log("Playback: %s", engine.errorMessage().c_str());
                result = 2;
            }
        }
        SDL_Quit();
        return result;
    }

    int runPlayer(const int argc, char **argv) {
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--help" || argument == "-h") {
                std::cout << "TTPlayer C++ 1.0\n"
                        "Usage: ttplayer_cpp [audio files / directories / playlists]\n"
                        "  --portable                Save settings alongside the executable\n"
                        "  --restore-java-ui         Restore the Java skin, window sizes and positions\n"
                        "  --config-dir <directory>  Use a separate configuration directory\n"
                        "  --exit-on-complete <file> Decode/play a single file, then exit (0 success / 2 error)\n"
                        "  --smoke-ui <directory>    Capture window rendering diagnostics, then exit\n"
                        "  --quit-after <ms>         Exit after the specified number of milliseconds\n"
                        "  --version                 Show version\n";
                return 0;
            }
            if (argument == "--version") {
                std::cout << "TTPlayer C++ 1.0.0\n";
                return 0;
            }
            if (argument == "--exit-on-complete") return checkPlayback(argc, argv);
        }
        return ttplayer::app::runApplication(argc, argv);
    }
}

int main(const int argc, char **argv) { return runPlayer(argc, argv); }
