#include "app/Application.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>

namespace ttplayer::app {
    void Application::selectSong(const int index, const SDL_Keymod modifiers) {
        const auto selectionModifiers = static_cast<SDL_Keymod>(modifiers
#ifdef __APPLE__
            | ((modifiers & SDL_KMOD_GUI) ? SDL_KMOD_CTRL : 0)
#endif
        );
        if (index < 0 || index >= static_cast<int>(playlist().songs.size())) return;
        if ((selectionModifiers & SDL_KMOD_SHIFT) && selectionAnchor_ >= 0) {
            if (!(selectionModifiers & SDL_KMOD_CTRL)) selectedSongs_.clear();
            for (int song = std::min(selectionAnchor_, index); song <= std::max(selectionAnchor_, index); ++song)
                selectedSongs_.insert(song);
        } else if (selectionModifiers & SDL_KMOD_CTRL) {
            if (!selectedSongs_.erase(index)) selectedSongs_.insert(index);
            selectionAnchor_ = index;
        } else {
            selectedSongs_ = {index};
            selectionAnchor_ = index;
        }
        selected_ = selectedSongs_.contains(index) ? index : selectedSongs_.empty() ? -1 : *selectedSongs_.begin();
    }

    void Application::findSong(const bool next) {
        if (!next) {
            openPanel("input", message("dialog.findPrompt"), findText_, [this](const auto &value) {
                findText_ = value;
                selected_ = -1;
                findSong(true);
            });
            return;
        }
        if (findText_.empty()) {
            findSong(false);
            return;
        }
        const auto &songs = playlist().songs;
        const auto needle = lower(findText_);
        for (int offset = 1; offset <= static_cast<int>(songs.size()); ++offset) {
            const int index = (std::max(-1, selected_) + offset) % static_cast<int>(songs.size());
            const auto &song = songs[static_cast<std::size_t>(index)];
            if (lower(song.title + " " + song.artist + " " + utf8(song.path)).find(needle) == std::string::npos)
                continue;
            filter_.clear();
            selectSong(index);
            view("playlist").scroll = index;
            view("playlist").show(true);
            return;
        }
        openPanel("info", message("dialog.findResultTitle"), message("dialog.noMatch"));
    }

    void Application::reorderSongs(const std::string &order) {
        auto &songs = playlist().songs;
        std::vector<int> indices(songs.size());
        std::iota(indices.begin(), indices.end(), 0);
        if (order == "reverse") std::reverse(indices.begin(), indices.end());
        else if (order == "random") std::shuffle(indices.begin(), indices.end(), random_);
        else
            std::stable_sort(indices.begin(), indices.end(), [&](const int first, const int second) {
                auto field = [&](const Song &song) {
                    return lower(order == "artist"
                                     ? song.artist
                                     : order == "filename"
                                           ? utf8(song.path.filename())
                                           : order == "path"
                                                 ? utf8(song.path)
                                                 : song.title);
                };
                return field(songs[first]) < field(songs[second]);
            });
        auto source = std::move(songs);
        const int playingIndex = nowIndex_;
        songs.clear();
        songs.reserve(source.size());
        std::set<int> selection;
        int focused = -1;
        for (std::size_t index = 0; index < indices.size(); ++index) {
            if (selectedSongs_.contains(indices[index])) selection.insert(static_cast<int>(index));
            if (indices[index] == selected_) focused = static_cast<int>(index);
            if (nowList_ == settings_.playlist && indices[index] == playingIndex) nowIndex_ = static_cast<int>(index);
            songs.push_back(std::move(source[indices[index]]));
        }
        selectedSongs_ = std::move(selection);
        selected_ = selectionAnchor_ = focused;
        view("playlist").scroll = 0;
        dirty_ = true;
    }

    void Application::moveSongs(const int target) {
        auto &songs = playlist().songs;
        auto selection = selectedSongs_;
        if (selection.empty() && songDragIndex_ >= 0) selection.insert(songDragIndex_);
        if (selection.empty() || target < 0 || target > static_cast<int>(songs.size())) return;
        std::vector<Song> selected, remaining;
        const int playingIndex = nowIndex_;
        int before = 0;
        for (int index = 0; index < static_cast<int>(songs.size()); ++index) {
            if (selection.contains(index)) {
                selected.push_back(std::move(songs[index]));
                if (index < target) ++before;
            } else remaining.push_back(std::move(songs[index]));
        }
        const int insertion = std::clamp(target - before, 0, static_cast<int>(remaining.size()));
        remaining.insert(remaining.begin() + insertion, std::make_move_iterator(selected.begin()),
                         std::make_move_iterator(selected.end()));
        songs = std::move(remaining);
        selectedSongs_.clear();
        for (int offset = 0; offset < static_cast<int>(selected.size()); ++offset) selectedSongs_.insert(
            insertion + offset);
        selected_ = selectionAnchor_ = insertion;
        if (nowList_ == settings_.playlist) {
            if (playingIndex >= 0 && playingIndex < static_cast<int>(songs.size())) {
                const int selectedBefore = static_cast<int>(std::distance(
                    selection.begin(), selection.lower_bound(playingIndex)));
                const int remainingIndex = playingIndex - selectedBefore;
                nowIndex_ = selection.contains(playingIndex)
                                ? insertion + selectedBefore
                                : remainingIndex + (remainingIndex >= insertion
                                                        ? static_cast<int>(selected.size())
                                                        : 0);
                settings_.song = nowIndex_;
            }
        }
        dirty_ = true;
    }

    void Application::saveAllLists(const std::filesystem::path &directory) {
        std::filesystem::create_directories(directory);
        std::set<std::string> names;
        int count = 0;
        for (const auto &list: settings_.lists) {
            std::string name = list.name;
            for (auto &character: name) if (std::string_view("\\/:*?\"<>|").find(character) != std::string_view::npos ||
                                            static_cast<unsigned char>(character) < 32) character = '_';
            while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
            if (name.empty()) name = "Playlist";
            auto unique = name;
            for (int suffix = 2; !names.insert(lower(unique)).second; ++suffix)
                unique = name + "-" + std::to_string(suffix);
            auto file = directory / fromUtf8(unique + ".m3u8");
            if (std::filesystem::exists(file)) {
                for (int suffix = 2; std::filesystem::exists(file); ++suffix) file = directory / fromUtf8(
                                                                                  unique + "-" + std::to_string(
                                                                                      suffix) + ".m3u8");
            }
            exportM3u(file, list.songs);
            ++count;
        }
        status_ = std::to_string(count) + " " + message("dialog.saveAll");
    }

    void Application::removeList() {
        if (importer_.busy()) {
            status_ = tr("请等待导入结束", "Wait for import completion");
            return;
        }
        const int removed = settings_.playlist;
        settings_.lists.erase(settings_.lists.begin() + removed);
        if (settings_.lists.empty()) settings_.lists.push_back({message("playlist.defaultName"), {}});
        if (nowList_ > removed) --nowList_;
        else if (nowList_ == removed) {
            autoAdvance_ = false;
            nowList_ = -1;
        }
        settings_.playlist = std::min(removed, static_cast<int>(settings_.lists.size()) - 1);
        selected_ = selectionAnchor_ = settings_.song = -1;
        selectedSongs_.clear();
        view("playlist").scroll = 0;
        dirty_ = true;
    }
}
