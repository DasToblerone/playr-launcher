#pragma once

#include <map>
#include <string>
#include <vector>

#include "model/Game.h"

namespace playr {

// The library as the user sees it: the games, the categories and how they are sorted and filtered.
extern SortKey g_sortKey;
extern bool g_sortDescending;
extern bool g_showUninstalled;  // the library lists the games that are not installed, too

extern std::vector<Game> g_games;
extern std::vector<std::wstring> g_categories;  // sidebar rows and collections to draw from; "Alle Spiele" is always the first
extern std::map<std::wstring, bool> g_isGame;   // appId -> verdict from the Steam store, cached on disk
extern std::map<std::wstring, std::wstring> g_ownedNames;  // appId -> name of owned games that are not installed, cached on disk

bool IsReservedCategory(const std::wstring& name);
void SortGamesByName();

// Replaces the game list with `detected` plus the owned games that are not installed, keeping saved category
// assignments and dropping known non-games.
void MergeGames(const std::map<std::wstring, Game>& detected);

}  // namespace playr
