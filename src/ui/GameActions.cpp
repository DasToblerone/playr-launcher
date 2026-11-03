#include "ui/GameActions.h"

#include <algorithm>
#include <memory>
#include <random>
#include <thread>

#include "app/Messages.h"
#include "model/Library.h"
#include "steam/Steam.h"
#include "storage/Storage.h"
#include "ui/Feedback.h"
#include "ui/Layout.h"
#include "ui/UiState.h"

namespace playr {

void LaunchGame(HWND hwnd, const Game& game) {
    if (!game.installed) return;
    std::wstring uri = L"steam://rungameid/" + game.appId;
    HINSTANCE result = ShellExecuteW(hwnd, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32)
        ShowToast(hwnd, L"Steam konnte das Spiel nicht starten");
    else
        DestroyWindow(hwnd);
}

// Selects a random game of the current category, different from the current selection. Starting is up to the user.
void SelectRandomGame(HWND hwnd) {
    if (g_visible.empty()) {
        ShowToast(hwnd, L"In dieser Kategorie sind keine Spiele");
        return;
    }
    // Only installed games can be played, so only they are picked.
    std::vector<int> choices, installed;
    for (int i = 0; i < (int)g_visible.size(); ++i) {
        if (!g_games[g_visible[i]].installed) continue;
        installed.push_back(i);
        if (g_visible[i] != g_sel) choices.push_back(i);
    }
    if (installed.empty()) {
        ShowToast(hwnd, L"In dieser Kategorie ist kein Spiel installiert");
        return;
    }
    if (choices.empty()) choices = installed;

    std::random_device rd;
    std::mt19937 rng(rd());
    int index = choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(rng)];
    g_sel = g_visible[index];
    EnsureVisible(index);
    Redraw(hwnd);
}

void ImportFromSteam(HWND hwnd) {
    if (g_badge[BTN_IMPORT].mode == Badge::Spinner) return;
    ImportResult result = ImportSteamCategories();
    if (result.games > 0) {
        SaveSettings();
        RefreshGames();
        Relayout(hwnd);
        g_importResult = { Badge::Check,
            std::to_wstring(result.categories) + (result.categories == 1 ? L" Kategorie" : L" Kategorien"), 0, 2200 };
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
    Relayout(hwnd);
}

}  // namespace playr
