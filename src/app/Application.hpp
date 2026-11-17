#pragma once

#include "app/Store.hpp"
#include "app/FileDialogService.hpp"
#include "app/Localization.hpp"
#include "app/LyricRequestService.hpp"
#include "app/LyricsRepository.hpp"
#include "app/MediaImportService.hpp"
#include "app/Ui.hpp"
#include "app/PopupMenu.hpp"
#include "app/SystemTray.hpp"
#include "app/WindowDragSession.hpp"
#include "media/AudioEngine.hpp"
#ifdef __APPLE__
#include "app/PlatformMac.hpp"
#endif

#include <random>
#include <set>

namespace ttplayer::app {
    int runApplication(int argc, char **argv);

    class Application final {
    public:
        Application(int argc, char **argv);

        ~Application();

        int run();

    private:
        using MenuItem = MenuEntry;

        std::string tr(const char *chinese, const char *english) const;

        std::string message(std::string_view key) const;

        void setLanguage(std::string language);

        std::string songTitle(const Song &song, int index) const;

        UiWindow &view(std::string_view name);

        Playlist &playlist();

        void initialize();

        void createWindows();

        void presentInitialWindows();

        void createTray();

        bool hasTray() const;

        void applySkin(const std::filesystem::path &path);

        void applyDesktopLock();

        void command(std::string name);

        void toggle(std::string_view name);

        void playSong(int list, int index);

        void advance(int direction, bool automatic = false);

        void playPause();

        void loadLyrics(const std::filesystem::path &path = {});

        void importFiles(std::vector<std::filesystem::path> files, bool play);

        void processDroppedFiles(std::vector<std::filesystem::path> files);

        void pollJobs();

        void completeImport(MediaImportService::Result result);

        void completeLyricRequest(LyricRequestService::Result result);

        void completeFileDialog(FileDialogService::Result result);

        void startSearch();

        void startAutomaticLyrics();

        void fetchLyric(int index);

        void showDialog(FileDialogKind kind);

        void removeSong();

        void save();

        void event(const SDL_Event &event);

        void keyboard(const SDL_KeyboardEvent &key, UiWindow *window);

        void trackWindowMove(UiWindow &window);

        bool beginPointerWindowDrag(UiWindow &window, const SDL_MouseButtonEvent &button);

        void updatePointerWindowDrag(SDL_FPoint pointer, bool bypass);

        void finishPointerWindowDrag(bool cancel);
#ifdef __APPLE__
        void applyMacDesktopLock();

        void macWindowEvent(SDL_Window *window, MacWindowEvent event);
#endif
        void initializeWindowManagement();

        void beginWindowDrag(UiWindow &window);

        SDL_Rect moveWindowDrag(UiWindow &window, SDL_Rect proposed, bool bypass);

        void endWindowDrag();

        void recoverWindows();

        void refreshWindowWorkAreas();

        void resetWindowLayout();

        void updateDialogOwner(bool visible);

        void centerWindowOn(UiWindow &window, UiWindow &owner);

        UiWindow &dialogOwner();

        void restoreSuspendedWindows();

        void raiseVisibleWindows(UiWindow &anchor);

        SDL_Rect normalWindowBounds(UiWindow &window);

        static bool SDLCALL windowEventWatch(void *userdata, SDL_Event *event);

        std::vector<SDL_Rect> windowPeers(const UiWindow &window) const;

        void render(UiWindow &window);

        void renderSkin(UiWindow &window, const SkinWindow &definition);

        void renderPlayer(UiWindow &window);

        void renderPlaylist(UiWindow &window, SDL_FRect bounds, bool skinned = false);

        void renderClassicPlaylist(UiWindow &window, SDL_FRect bounds);

        void renderClassicLyrics(UiWindow &window, SDL_FRect bounds);

        void renderSkinScrollbar(UiWindow &window, SDL_FRect bounds, int count, int rows, int &scroll);

        void renderEqualizer(UiWindow &window);

        void renderLyrics(UiWindow &window, SDL_FRect bounds, bool desktop = false);

        void renderSettings(UiWindow &window);

        void renderContext(UiWindow &window);

        void openPanel(std::string kind, std::string title = {}, std::string value = {},
                       std::function<void(const std::string &)> submit = {});

        void renderPanel(UiWindow &window);

        void renderImportProgress(UiWindow &window);

        void panelButton(UiWindow &window, SDL_FRect bounds, std::string_view label, std::function<void()> action,
                         bool primary = false);

        void panelInput(UiWindow &window, SDL_FRect bounds, int field, std::string_view placeholder = {});

        void renderInputText(UiWindow &window, SDL_FRect bounds, const std::string &value, int field,
                             std::string_view placeholder, bool light);

        void insertInput(std::string_view text);

        std::string *inputValue();

        void submitPanel();

        bool confirmAction(const std::string &prompt);

        void selectSong(int index, SDL_Keymod modifiers = SDL_KMOD_NONE);

        void findSong(bool next);

        void reorderSongs(const std::string &order);

        void moveSongs(int target);

        void saveAllLists(const std::filesystem::path &directory);

        void removeList();

        std::vector<MenuItem> contextItems(int kind);

        void openContext(UiWindow &window, int kind, float left, float top);

        void contextAction(const std::string &action);

        void drawInput(UiWindow &window, SDL_FRect bounds, std::string_view placeholder, int field);

        float sliderValue(std::string_view name) const;

        void changeSlider(std::string_view name, float ratio);

        void smokeStep();

        std::vector<std::string> arguments_;
        std::filesystem::path base_, assets_, legacy_, config_, smokeOutput_;
        Fonts fonts_;
        Settings settings_;
        std::unique_ptr<Skin> skin_;
        std::vector<std::filesystem::path> skins_;
        std::map<std::string, std::unique_ptr<UiWindow> > windows_;
        PopupMenu popupMenu_;
        media::AudioEngine engine_;
        Lyrics lyrics_;
        Song nowSong_;
        std::vector<float> spectrum_;
        std::vector<float> waveform_;
        std::vector<LyricResult> searchResults_;
        MediaImportService importer_;
        LyricRequestService lyricRequests_;
        LyricsRepository lyricsRepository_;
        FileDialogService fileDialogs_;
        Localization localization_;
        std::unique_ptr<SystemTray> tray_;
#ifdef __APPLE__
        std::unique_ptr<MacApplication> macApplication_;
        Uint32 macReopenEvent_ = 0;
#endif
        std::optional<Hit> dragging_;
        std::string contextWindow_;
        SDL_FRect contextBounds_{};
        SDL_WindowID dragWindow_ = 0;
        bool running_ = true, autoAdvance_ = false, dirty_ = false, initialized_ = false, smoke_ = false, dropping_ =
                        false;
        std::vector<std::filesystem::path> dropFiles_;
        std::vector<std::filesystem::path> pendingDropImports_;
        int nowList_ = 0, nowIndex_ = -1, selected_ = -1, page_ = 0, inputField_ = 0, smokeStage_ = 0, clickCount_ = 0,
                contextKind_ = 0;
        float unmutedVolume_ = 0.7F;
        float playlistDividerX_ = 0;
        int playlistScroll_ = 0, selectionAnchor_ = -1;
        bool remainingTime_ = false;
        std::set<int> selectedSongs_;
        std::map<std::string, bool> miniVisibility_, suspendedVisibility_;
        bool karaoke_ = true;
        std::string panelKind_, panelValue_, panelArtist_, panelTitle_, panelSavePath_;
        std::function<void(const std::string &)> panelSubmit_;
        std::unique_ptr<Skin> previewSkin_;
        int previewIndex_ = -1, resultIndex_ = -1;
        bool associateLyrics_ = true;
        int songDragIndex_ = -1, songDropIndex_ = -1;
        float songDragX_ = 0, songDragY_ = 0;
        bool songDragging_ = false, lyricDragging_ = false;
        float lyricDragY_ = 0, lyricPreviewLine_ = 0;
        float lyricRowHeight_ = 26;
        std::string findText_;
        bool inputSelectedAll_ = false;
        std::size_t inputCaret_ = std::string::npos;
        std::string inputPreedit_;
        WindowDragSession windowDrag_;
        bool nativeWindowManagement_ = false;
        std::vector<SDL_Rect> windowWorkAreas_;
        std::map<std::string, SDL_Rect> normalWindowBounds_;
        bool windowWatchInstalled_ = false, liveWindowRendering_ = false;
        Uint64 lastLiveWindowRender_ = 0;
        std::string query_, filter_, status_;
        Uint64 lastSave_ = 0, lastTitle_ = 0, lastSmoke_ = 0, quitAt_ = 0;
        std::mt19937 random_{std::random_device{}()};
    };
}
