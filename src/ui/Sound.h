#pragma once

#include "platform/Win32.h"

namespace playr {

extern bool g_soundOn;

// Plays the sound of one spin: a click for every card that passes the center, timed like the reel slows down,
// and a short chime when it stops. It is built in memory, so the app needs no audio files.
void PlaySpinSound(int cards, DWORD durationMs);
void StopSpinSound();

}  // namespace playr
