#pragma once

#include <map>
#include <string>

#include "model/Game.h"
#include "net/SteamStore.h"
#include "platform/Win32.h"

namespace playr {

void LaunchGame(HWND hwnd, const Game& game);
void SelectRandomGame(HWND hwnd);
void ImportFromSteam(HWND hwnd);
void Rescan(HWND hwnd);

// Results that the background threads post to the window.
void OnScanDone(HWND hwnd, std::map<std::wstring, Game>* found);
void OnOwned(HWND hwnd, OwnedResult* raw);
void OnClassified(HWND hwnd, const std::wstring& appId, bool isGame);

}  // namespace playr
