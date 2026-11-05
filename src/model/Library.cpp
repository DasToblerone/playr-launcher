#include "model/Library.h"

#include <algorithm>

#include "steam/Steam.h"
#include "storage/Storage.h"

namespace playr {

SortKey g_sortKey = SortKey::Name;
bool g_sortDescending = false;
bool g_showUninstalled = true;

std::vector<Game> g_games;
std::vector<std::wstring> g_categories{ kAllGames };  // sidebar rows and collections to draw from; "Alle Spiele" is always the first
std::map<std::wstring, bool> g_isGame;   // appId -> verdict from the Steam store, cached on disk
std::map<std::wstring, std::wstring> g_ownedNames;  // appId -> name of owned games that are not installed, cached on disk

bool IsReservedCategory(const std::wstring& name) { return name == kAllGames; }

void SortGamesByName() {
    std::sort(g_games.begin(), g_games.end(), [](const Game& a, const Game& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
}

// Replaces the game list with `detected` plus the owned games that are not installed, keeping saved category
// assignments and dropping known non-games.
void MergeGames(const std::map<std::wstring, Game>& detected) {
    std::map<std::wstring, std::vector<std::wstring>> assignments;
    for (const auto& game : g_games)
        for (const auto& category : game.categories)
            if (!game.appId.empty()) assignments[game.appId].push_back(category);

    g_games.clear();
    for (const auto& entry : detected) {
        Game game = entry.second;
        auto verdict = g_isGame.find(game.appId);
        if (verdict != g_isGame.end() && !verdict->second) continue;
        for (const auto& category : assignments[game.appId])
            if (std::find(g_categories.begin(), g_categories.end(), category) != g_categories.end())
                game.AddCategory(category);  // categories that no longer exist are dropped
        g_games.push_back(game);
    }
    for (const auto& owned : g_ownedNames) {
        if (detected.count(owned.first)) continue;
        auto verdict = g_isGame.find(owned.first);
        if (verdict != g_isGame.end() && !verdict->second) continue;
        Game game;
        game.appId = owned.first;
        game.name = owned.second;
        game.installed = false;
        for (const auto& category : assignments[game.appId])
            if (std::find(g_categories.begin(), g_categories.end(), category) != g_categories.end())
                game.AddCategory(category);
        g_games.push_back(game);
    }
    SortGamesByName();
    ApplyPlayStats();
    SaveSettings();
}

}  // namespace playr
