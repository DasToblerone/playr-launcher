#pragma once

#include <string>
#include <vector>

#include "model/Game.h"
#include "platform/Win32.h"

namespace playr {

// What the draw may pick, by play time.
enum class PlayFilter { All, Never, Barely, Neglected };
constexpr int kPlayFilterCount = 4;
const wchar_t* PlayFilterName(PlayFilter filter);

// The spin: a strip of covers (the reel) runs past the center and slows down until the winner stops there.
struct Spin {
    bool running = false;
    ULONGLONG start = 0;
    DWORD duration = 0;
    int from = 0, to = 0;  // positions on the reel
};

extern PlayFilter g_playFilter;
extern int g_poolCategory;               // index into g_categories
extern bool g_rollUninstalled;           // games that are not installed take part in the draw, too
extern std::vector<int> g_pool;          // the games (indices into g_games) the filters let through
extern std::vector<std::wstring> g_reel; // app ids on the strip; they stay valid when g_games changes
extern int g_reelPos;                    // the reel position that rests in the center
extern std::wstring g_resultId;          // the game that was drawn, empty before the first draw and during a spin
extern Spin g_spin;
extern ULONGLONG g_landedAt;             // when the last spin stopped, for the landing glow

const Game* FindGame(const std::wstring& appId);
const Game* ResultGame();

// Recomputes the pool after a filter or the library changed. `resetReel` also rebuilds the strip and stops a spin.
void RebuildPool(bool resetReel);

// Draws a game and starts the reel; false when the pool is empty.
bool StartSpin(HWND hwnd);
void TickSpin(HWND hwnd);

inline bool Spinning() { return g_spin.running; }

// The fractional reel position right now: moves while a spin runs, rests on a whole number otherwise.
double ReelPosition();

// How fast the reel runs right now, in cards per second (0 when it rests).
double ReelSpeed();

// Which of the three buttons (ACT_*) can be clicked at the moment.
bool ActionActive(int action);

}  // namespace playr
