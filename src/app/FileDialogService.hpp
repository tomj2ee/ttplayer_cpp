#pragma once

#include "app/Data.hpp"

#include <SDL3/SDL_dialog.h>

#include <memory>
#include <mutex>

namespace ttplayer::app {
    enum class FileDialogKind {
        OpenAudio,
        AddFolder,
        ExportPlaylist,
        ImportPlaylists,
        OpenLyrics,
        OpenSkin,
        ExportLyrics,
        ExportAllPlaylists,
        ChooseLyricDestination
    };

    class FileDialogService final {
    public:
        struct Result {
            FileDialogKind kind;
            std::vector<std::filesystem::path> files;
            std::string error;
        };

        FileDialogService() = default;

        ~FileDialogService();

        FileDialogService(const FileDialogService &) = delete;

        FileDialogService &operator=(const FileDialogService &) = delete;

        void show(FileDialogKind kind, SDL_Window *parent, std::string lyricSavePath = {});

        [[nodiscard]] std::vector<Result> takeResults();

        void close();

    private:
        struct State {
            std::mutex mutex;
            bool accepting = true;
            std::vector<Result> results;
        };

        struct RequestContext {
            std::weak_ptr<State> state;
            FileDialogKind kind;
            std::string defaultPath;
        };

        static void SDLCALL selectedFiles(void *userdata, const char *const*files, int filter);

        std::shared_ptr<State> state_ = std::make_shared<State>();
    };
}
