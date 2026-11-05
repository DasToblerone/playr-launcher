#pragma once

#include <map>
#include <string>

#include "model/Game.h"
#include "net/SteamStore.h"
#include "platform/Win32.h"

namespace playr {

// Starts an installed game (and closes the window), or opens Steam's install dialog for one that is not installed.
void ActivateGame(HWND hwnd, const Game& game);
void LaunchGame(HWND hwnd, const Game& game);  // library: only installed games
void ImportFromSteam(HWND hwnd);
void Rescan(HWND hwnd);

// Results that the background threads post to the window.
void OnScanDone(HWND hwnd, std::map<std::wstring, Game>* found);
void OnOwned(HWND hwnd, OwnedResult* raw);
void OnClassified(HWND hwnd, const std::wstring& appId, bool isGame);

}  // namespace playr
