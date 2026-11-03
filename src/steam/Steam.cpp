#include "steam/Steam.h"

#include <algorithm>

#include "model/Library.h"
#include "util/Text.h"
#include "util/Vdf.h"

namespace playr {

std::wstring g_steamName;  // persona name of the Steam account that signed in last
std::wstring g_steamId;    // its SteamID64, which names the avatar file in Steam's cache

std::wstring SteamPath() {
    std::wstring steam = RegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty())
        steam = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (steam.empty()) {
        wchar_t programFiles[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"PROGRAMFILES(X86)", programFiles, MAX_PATH))
            steam = std::wstring(programFiles) + L"\\Steam";
    }
    return steam;
}

namespace {

std::vector<fs::path> FindSteamLibraries() {
    std::wstring steam = SteamPath();
    if (steam.empty()) return {};

    std::set<std::wstring> unique{ fs::path(steam).wstring() };
    std::wstring vdf = ReadTextFile(fs::path(steam) / L"steamapps" / L"libraryfolders.vdf");
    for (size_t pos = 0;;) {
        std::wstring path = VdfValue(vdf, L"path", pos, &pos);
        if (path.empty()) break;
        unique.insert(path);
    }

    std::vector<fs::path> libraries;
    for (const auto& item : unique) {
        std::error_code ec;
        if (fs::exists(fs::path(item) / L"steamapps", ec)) libraries.emplace_back(item);
    }
    return libraries;
}

// Steam's own helper packages show up as installed apps but are not games.
bool IsSteamTool(const std::wstring& name) {
    for (const wchar_t* prefix : { L"Steamworks Common", L"Steam Linux Runtime", L"Proton", L"Steam Controller" })
        if (name.rfind(prefix, 0) == 0) return true;
    return false;
}

}  // namespace

std::map<std::wstring, Game> ScanSteamGames() {
    std::map<std::wstring, Game> found;
    for (const auto& library : FindSteamLibraries()) {
        std::error_code ec;
        for (fs::directory_iterator it(library / L"steamapps", ec), end; !ec && it != end; it.increment(ec)) {
            std::wstring filename = it->path().filename().wstring();
            if (filename.rfind(L"appmanifest_", 0) != 0 || it->path().extension() != L".acf") continue;

            std::wstring vdf = ReadTextFile(it->path());
            Game game;
            game.appId = VdfValue(vdf, L"appid");
            game.name = VdfValue(vdf, L"name");
            if (game.appId.empty() || game.name.empty() || IsSteamTool(game.name)) continue;
            game.sizeBytes = _wtoi64(VdfValue(vdf, L"SizeOnDisk").c_str());
            found[game.appId] = game;
        }
    }
    return found;
}

// Reads the account that signed in last from loginusers.vdf: its SteamID64 (the key of its block) and persona name.
void LoadSteamUser() {
    std::wstring text = ReadTextFile(fs::path(SteamPath()) / L"config" / L"loginusers.vdf");
    size_t nameKey = std::wstring::npos;
    // Several accounts can be listed; prefer the one marked as the most recent.
    for (size_t pos = 0; (pos = text.find(L"\"MostRecent\"", pos)) != std::wstring::npos; pos += 12) {
        if (VdfValue(text, L"MostRecent", pos) != L"1") continue;
        nameKey = text.rfind(L"\"PersonaName\"", pos);
        break;
    }
    if (nameKey == std::wstring::npos) nameKey = text.find(L"\"PersonaName\"");
    if (nameKey == std::wstring::npos) return;

    g_steamName = VdfValue(text, L"PersonaName", nameKey);
    size_t idKey = text.rfind(L"\"7656", nameKey);  // SteamID64s all start with 7656
    if (idKey != std::wstring::npos) g_steamId = ReadJsonString(text, idKey + 1);
}

namespace {

// Reads the number of achievements from the per-app cache of the Steam client (nTotal and nAchieved).
void ReadAchievements(const fs::path& cacheDir, Game& game) {
    std::wstring text = ReadTextFile(cacheDir / (game.appId + L".json"));
    size_t total = text.find(L"\"nTotal\":"), done = text.find(L"\"nAchieved\":");
    if (total == std::wstring::npos || done == std::wstring::npos) return;
    int t = _wtoi(text.c_str() + total + 9), d = _wtoi(text.c_str() + done + 12);
    if (t > game.achTotal) {
        game.achTotal = t;
        game.achDone = d;
    }
}

}  // namespace

// Reads play time (minutes) and last played (unix time) per app from Steam's localconfig.vdf, and the
// achievement counts from the client's cache. With `only`, just that app is updated.
void ApplyPlayStats(const std::wstring& only) {
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(SteamPath()) / L"userdata", ec), end; !ec && it != end; it.increment(ec)) {
        std::wstring text = ReadTextFile(it->path() / L"config" / L"localconfig.vdf");
        if (text.empty()) continue;
        for (auto& game : g_games) {
            if (!only.empty() && game.appId != only) continue;
            ReadAchievements(it->path() / L"config" / L"librarycache", game);
            const std::wstring marker = L"\"" + game.appId + L"\"";
            for (size_t pos = 0; (pos = text.find(marker, pos)) != std::wstring::npos; pos += marker.size()) {
                size_t open = text.find_first_not_of(L" \t\r\n", pos + marker.size());
                if (open == std::wstring::npos || text[open] != L'{') continue;
                std::wstring block = text.substr(open, BlockEnd(text, open) - open);
                std::wstring played = VdfValue(block, L"LastPlayed"), minutes = VdfValue(block, L"Playtime");
                if (played.empty() && minutes.empty()) continue;
                game.lastPlayed = std::max(game.lastPlayed, _wtoi64(played.c_str()));
                game.playtimeMinutes = std::max(game.playtimeMinutes, _wtoi(minutes.c_str()));
                break;
            }
        }
    }
}

// Games the account owns beyond the installed ones. Steam has no local list of them, so this collects the apps that
// have play time in localconfig.vdf and the games in the user's collections. The store then says which are real games.
std::set<std::wstring> FindOwnedCandidates() {
    std::set<std::wstring> ids;
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(SteamPath()) / L"userdata", ec), end; !ec && it != end; it.increment(ec)) {
        std::wstring text = ReadTextFile(it->path() / L"config" / L"localconfig.vdf");
        for (size_t pos = 0; (pos = text.find(L"\"LastPlayed\"", pos)) != std::wstring::npos; pos += 12) {
            // The key of the block that holds LastPlayed is the app id.
            size_t open = text.rfind(L'{', pos);
            if (open == std::wstring::npos || open == 0) continue;
            size_t keyEnd = text.rfind(L'"', open), keyStart = keyEnd == std::wstring::npos || keyEnd == 0
                ? std::wstring::npos : text.rfind(L'"', keyEnd - 1);
            if (keyStart == std::wstring::npos) continue;
            std::wstring key = text.substr(keyStart + 1, keyEnd - keyStart - 1);
            if (!key.empty() && std::all_of(key.begin(), key.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) ids.insert(key);
        }
    }
    for (const auto& collection : ReadSteamCollections())
        ids.insert(collection.appIds.begin(), collection.appIds.end());
    return ids;
}

}  // namespace playr
