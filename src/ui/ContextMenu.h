#pragma once

#include <initializer_list>

#include "ui/UiState.h"

namespace playr {

void CloseMenu(HWND hwnd);

// Opens the context menu at `at`: for a category row (`chip`) or for a game (`game`, an index into g_games).
void OpenMenu(HWND hwnd, POINT at, int chip, int game, std::initializer_list<Menu::Action> actions);

}  // namespace playr
