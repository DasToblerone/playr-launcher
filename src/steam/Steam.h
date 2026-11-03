#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "model/Game.h"
#include "util/Fs.h"

namespace playr {

extern std::wstring g_steamName;  // persona name of the Steam account that signed in last
extern std::wstring g_steamId;    // its SteamID64, which names the avatar file in Steam's cache

std::wstring SteamPath();

// The installed games of all Steam libraries, by app id.
std::map<std::wstring, Game> ScanSteamGames();

void LoadSteamUser();

// Reads play time (minutes) and last played (unix time) per app from Steam's localconfig.vdf, and the
// achievement counts from the client's cache. With `only`, just that app is updated.
void ApplyPlayStats(const std::wstring& only = L"");

// Games the account owns beyond the installed ones. Steam has no local list of them, so this collects the apps that
// have play time in localconfig.vdf and the games in the user's collections. The store then says which are real games.
std::set<std::wstring> FindOwnedCandidates();

struct SteamCollection {
    std::wstring name;
    std::vector<std::wstring> appIds;
};

std::vector<SteamCollection> ReadSteamCollections();

struct ImportResult {
    int categories = 0;
    int games = 0;
};

ImportResult ImportSteamCategories();

}  // namespace playr
