#pragma once

#include <string>

#include "model/Game.h"
#include "ui/UiState.h"

namespace playr {

struct PlayParts {
    std::wstring playtime;  // "36 Std."
    std::wstring when;      // "vor 3 Tagen"
    std::wstring size;      // "54,3 GB"
    bool played = false;
};

PlayParts PlayStats(const Game& game);

// "36 Std. · Zuletzt gespielt vor 3 Tagen · 54 GB", for the one-line list rows
std::wstring PlayInfo(const Game& game);

const wchar_t* SortName(SortKey key);
const wchar_t* SortArrow(bool descending);
SortKey SortKeyOf(Menu::Action action);

}  // namespace playr
