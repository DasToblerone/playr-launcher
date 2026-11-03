#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace playr {

struct Game {
    std::wstring appId;
    std::wstring name;
    std::vector<std::wstring> categories;  // a game can be in any number of categories
    int playtimeMinutes = 0;
    long long lastPlayed = 0;  // unix time, 0 when never played
    long long sizeBytes = 0;   // install size from the Steam manifest, 0 when unknown
    bool installed = true;     // false for games the account owns but that are not on this PC
    int achTotal = 0, achDone = 0;  // achievements, 0 total when Steam has no data for the game

    bool InCategory(const std::wstring& category) const {
        return std::find(categories.begin(), categories.end(), category) != categories.end();
    }
    // Returns false when the game already was in the category.
    bool AddCategory(const std::wstring& category) {
        if (InCategory(category)) return false;
        categories.push_back(category);
        return true;
    }
    void RemoveCategory(const std::wstring& category) {
        categories.erase(std::remove(categories.begin(), categories.end(), category), categories.end());
    }
};

inline const std::wstring kAllGames = L"Alle Spiele";

enum class SortKey { Name, Playtime, LastPlayed, Size };

}  // namespace playr
