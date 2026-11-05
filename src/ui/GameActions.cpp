#include "ui/GameActions.h"

#include <algorithm>
#include <memory>
#include <thread>

#include "app/Messages.h"
#include "model/Library.h"
#include "steam/Steam.h"
#include "storage/Storage.h"
#include "ui/Feedback.h"
#include "ui/Layout.h"
#include "ui/Randomizer.h"
#include "ui/UiState.h"

namespace playr {

namespace {

bool OpenSteamUri(HWND hwnd, const std::wstring& uri) {
    return (INT_PTR)ShellExecuteW(hwnd, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

}  // namespace

// Starts an installed game and closes the window (the job is done); a game that is not installed opens Steam's
// install dialog instead, and the window stays.
void ActivateGame(HWND hwnd, const Game& game) {
    if (game.installed) {
        if (OpenSteamUri(hwnd, L"steam://rungameid/" + game.appId)) DestroyWindow(hwnd);
        else ShowToast(hwnd, L"Steam konnte das Spiel nicht starten");
    } else if (OpenSteamUri(hwnd, L"steam://install/" + game.appId)) {
        ShowToast(hwnd, L"Steam öffnet die Installation");
    } else {
        ShowToast(hwnd, L"Steam konnte nicht geöffnet werden");
    }
}

// Library: starting is for installed games only.
void LaunchGame(HWND hwnd, const Game& game) {
    if (game.installed) ActivateGame(hwnd, game);
}

void ImportFromSteam(HWND hwnd) {
    if (g_badge[BTN_IMPORT].mode == Badge::Spinner) return;
    ImportResult result = ImportSteamCategories();
    if (result.games > 0) {
        RefreshGames();
        RebuildPool(false);
        RebuildPool(false);
        Relayout(hwnd);  // new collections add chips
        g_importResult = { Badge::Check,
            std::to_wstring(result.categories) + (result.categories == 1 ? L" Sammlung" : L" Sammlungen"), 0, 2200 };
    } else {
        g_importResult = { Badge::Text, L"Nichts Neues", 0, 2200 };
    }
    g_importReady = true;
    ShowBadge(hwnd, BTN_IMPORT, Badge::Spinner, L"Lädt…");
}

void Rescan(HWND hwnd) {
    if (g_badge[BTN_SCAN].mode == Badge::Spinner) return;
    g_scanReady = false;
    ShowBadge(hwnd, BTN_SCAN, Badge::Spinner, L"Scannt…");
    std::thread([hwnd] {
        auto* found = new std::map<std::wstring, Game>(ScanSteamGames());
        PostMessageW(hwnd, WM_SCANDONE, 0, (LPARAM)found);
    }).detach();
}

void OnScanDone(HWND hwnd, std::map<std::wstring, Game>* found) {
    MergeGames(*found);
    delete found;
    if (g_active >= (int)g_categories.size()) g_active = 0;
    g_sel = -1;
    RefreshGames();
    RebuildPool(true);
    Relayout(hwnd);
    QueueBackgroundWork();
    g_scanReady = true;
}

// An owned game that is not installed has been looked up: add it to the library.
void OnOwned(HWND hwnd, OwnedResult* raw) {
    std::unique_ptr<OwnedResult> result(raw);
    g_isGame[result->appId] = result->isGame;
    SaveTypes();
    if (!result->isGame) return;
    g_ownedNames[result->appId] = result->name;
    SaveOwned();

    std::wstring selected = g_sel >= 0 && g_sel < (int)g_games.size() ? g_games[g_sel].appId : L"";
    Game game;
    game.appId = result->appId;
    game.name = result->name;
    game.installed = false;
    g_games.push_back(game);
    SortGamesByName();
    ApplyPlayStats(result->appId);
    g_sel = -1;  // indices moved; the selection is looked up again by app id
    for (int i = 0; i < (int)g_games.size(); ++i)
        if (!selected.empty() && g_games[i].appId == selected) g_sel = i;
    g_coverQueue.Push({ result->appId });
    RefreshGames();
    RebuildPool(false);  // the reel and the result go by app id, so a running spin is not disturbed
    Relayout(hwnd);
}

void OnClassified(HWND hwnd, const std::wstring& appId, bool isGame) {
    g_isGame[appId] = isGame;
    SaveTypes();
    if (isGame) return;
    g_games.erase(std::remove_if(g_games.begin(), g_games.end(),
        [&](const Game& game) { return game.appId == appId; }), g_games.end());
    g_sel = -1;
    SaveSettings();
    RefreshGames();
    RebuildPool(false);
    Relayout(hwnd);
}

}  // namespace playr
