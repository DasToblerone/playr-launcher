#include "ui/Randomizer.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <random>

#include "app/Messages.h"
#include "model/Library.h"
#include "ui/Layout.h"
#include "ui/Sound.h"
#include "ui/Theme.h"
#include "ui/UiState.h"

namespace playr {

PlayFilter g_playFilter = PlayFilter::All;
int g_poolCategory = 0;
bool g_rollUninstalled = false;
std::vector<int> g_pool;
std::vector<std::wstring> g_reel;
int g_reelPos = 0;
std::wstring g_resultId;
Spin g_spin;
ULONGLONG g_landedAt = 0;

namespace {

constexpr int kPreviewCards = 13;          // cards on the resting strip
constexpr int kSpinMinCards = 30;          // how many cards run past during a spin, at least
constexpr int kSpinExtraCards = 10;        // ...plus up to this many more
constexpr DWORD kSpinMillis = 4200;
constexpr int kTailCards = 6;              // cards after the winner, so the strip does not end at the center
constexpr size_t kRecentKept = 20;

std::vector<std::wstring> g_recent;  // the last winners, oldest first

std::mt19937& Rng() {
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

int RandomBelow(int n) { return std::uniform_int_distribution<int>(0, n - 1)(Rng()); }

bool Passes(const Game& game) {
    if (!g_rollUninstalled && !game.installed) return false;
    if (g_poolCategory > 0 && !game.InCategory(g_categories[g_poolCategory])) return false;
    switch (g_playFilter) {
    case PlayFilter::All: return true;
    case PlayFilter::Never: return game.playtimeMinutes == 0 && game.lastPlayed == 0;
    case PlayFilter::Barely: return game.playtimeMinutes < 120;
    case PlayFilter::Neglected:  // played once, but not for three months
        return game.lastPlayed > 0 && (long long)std::time(nullptr) - game.lastPlayed > 90ll * 86400;
    }
    return true;
}

// A random game of the pool that differs from the last two cards of the strip, so neighbors rarely repeat.
const std::wstring& Decoy() {
    size_t n = g_reel.size();
    for (int tries = 0; tries < 8; ++tries) {
        const std::wstring& id = g_games[g_pool[RandomBelow((int)g_pool.size())]].appId;
        bool same = (n >= 1 && g_reel[n - 1] == id) || (n >= 2 && g_reel[n - 2] == id);
        if (!same || g_pool.size() < 3) return id;
    }
    return g_games[g_pool[RandomBelow((int)g_pool.size())]].appId;
}

void BuildReel() {
    g_reel.clear();
    g_reelPos = kPreviewCards / 2;
    if (g_pool.empty()) return;
    bool resultInPool = !g_resultId.empty();
    for (int i = 0; i < kPreviewCards; ++i) {
        if (i == kPreviewCards / 2 && resultInPool) g_reel.push_back(g_resultId);
        else g_reel.push_back(Decoy());
    }
}

// Not one of the last few winners, as long as the pool is big enough to allow that.
std::wstring PickWinner() {
    size_t keep = std::min<size_t>(5, g_pool.size() - 1);
    std::vector<std::wstring> avoid;
    for (size_t i = g_recent.size(); i > 0 && avoid.size() < keep; --i) avoid.push_back(g_recent[i - 1]);
    std::vector<int> candidates;
    for (int index : g_pool)
        if (std::find(avoid.begin(), avoid.end(), g_games[index].appId) == avoid.end()) candidates.push_back(index);
    if (candidates.empty()) candidates = g_pool;
    return g_games[candidates[RandomBelow((int)candidates.size())]].appId;
}

void StopSpin() {
    if (g_spin.running) StopSpinSound();  // a spin that is cut short takes its sound with it
    g_spin.running = false;
    if (g_mainWnd) KillTimer(g_mainWnd, TIMER_SPIN);
}

}  // namespace

const wchar_t* PlayFilterName(PlayFilter filter) {
    switch (filter) {
    case PlayFilter::Never: return L"Nie gespielt";
    case PlayFilter::Barely: return L"Unter 2 Std.";
    case PlayFilter::Neglected: return L"Lange nicht dran";
    default: return L"Alle";
    }
}

const Game* FindGame(const std::wstring& appId) {
    if (appId.empty()) return nullptr;
    for (const Game& game : g_games)
        if (game.appId == appId) return &game;
    return nullptr;
}

const Game* ResultGame() { return FindGame(g_resultId); }

void RebuildPool(bool resetReel) {
    if (g_poolCategory >= (int)g_categories.size()) g_poolCategory = 0;
    g_pool.clear();
    for (int i = 0; i < (int)g_games.size(); ++i)
        if (Passes(g_games[i])) g_pool.push_back(i);
    if (!resetReel) return;

    StopSpin();
    // A result that no longer fits the filters is dropped.
    if (!g_resultId.empty()) {
        bool stillThere = std::any_of(g_pool.begin(), g_pool.end(), [](int i) { return g_games[i].appId == g_resultId; });
        if (!stillThere) g_resultId.clear();
    }
    BuildReel();
}

bool StartSpin(HWND hwnd) {
    if (g_spin.running || g_pool.empty()) return false;
    if (g_reel.empty()) BuildReel();

    // Keep the strip short: drop what scrolled out of sight long ago.
    if (g_reelPos > 150) {
        int drop = g_reelPos - 8;
        g_reel.erase(g_reel.begin(), g_reel.begin() + drop);
        g_reelPos -= drop;
    }
    // The cards before the center stay as they are, so the spin starts without a jump.
    g_reel.resize(g_reelPos + 1);
    int steps = kSpinMinCards + RandomBelow(kSpinExtraCards + 1);
    std::wstring winner = PickWinner();
    for (int i = 1; i <= steps; ++i) g_reel.push_back(i == steps ? winner : Decoy());
    for (int i = 0; i < kTailCards; ++i) g_reel.push_back(Decoy());

    g_resultId.clear();
    g_spin = { true, GetTickCount64(), kSpinMillis, g_reelPos, g_reelPos + steps };
    PlaySpinSound(steps, kSpinMillis);
    SetTimer(hwnd, TIMER_SPIN, 16, nullptr);
    return true;
}

void TickSpin(HWND hwnd) {
    ULONGLONG now = GetTickCount64();
    if (g_spin.running && now - g_spin.start >= g_spin.duration) {
        g_spin.running = false;
        g_reelPos = g_spin.to;
        g_resultId = g_reel[g_reelPos];
        g_recent.push_back(g_resultId);
        if (g_recent.size() > kRecentKept) g_recent.erase(g_recent.begin());
        g_landedAt = now;
    }
    if (g_spin.running) {
        InvalidateRect(hwnd, &g_L.stage, FALSE);
    } else {  // after landing only the glow ring and the title change; the cards stand still
        RECT ring = CenterCard();
        InflateRect(&ring, S(44), S(44));
        InvalidateRect(hwnd, &ring, FALSE);
        RECT info{ g_L.name.left, g_L.name.top, g_L.name.right, g_L.stats.bottom + S(4) };
        InvalidateRect(hwnd, &info, FALSE);
    }
    if (!g_spin.running && now - g_landedAt > 1000) KillTimer(hwnd, TIMER_SPIN);
}

double ReelPosition() {
    if (!g_spin.running) return g_reelPos;
    double t = std::clamp((double)(GetTickCount64() - g_spin.start) / g_spin.duration, 0.0, 1.0);
    double eased = 1.0 - std::pow(1.0 - t, 4.0);  // fast at first, then a long, soft stop
    return g_spin.from + (g_spin.to - g_spin.from) * eased;
}

double ReelSpeed() {
    if (!g_spin.running) return 0;
    double t = std::clamp((double)(GetTickCount64() - g_spin.start) / g_spin.duration, 0.0, 1.0);
    return (g_spin.to - g_spin.from) * 4.0 * std::pow(1.0 - t, 3.0) / (g_spin.duration / 1000.0);  // derivative of the easing
}

bool ActionActive(int action) {
    if (g_pool.empty() || g_spin.running) return false;
    bool hasResult = ResultGame() != nullptr;
    return action == ACT_ROLL ? !hasResult : hasResult;
}

}  // namespace playr
