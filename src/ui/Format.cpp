#include "ui/Format.h"

#include <algorithm>
#include <ctime>

namespace playr {

namespace {

std::wstring Plural(long long n, const wchar_t* one, const wchar_t* many) {
    return std::to_wstring(n) + L" " + (n == 1 ? one : many);
}

// "54,3 GB" below ten gigabytes with one decimal, whole gigabytes above, megabytes for small games.
std::wstring FormatSize(long long bytes) {
    if (bytes <= 0) return {};
    double gb = bytes / (1024.0 * 1024.0 * 1024.0);
    wchar_t text[32];
    if (gb < 1) swprintf_s(text, L"%.0f MB", bytes / (1024.0 * 1024.0));
    else if (gb < 10) swprintf_s(text, L"%.1f GB", gb);
    else swprintf_s(text, L"%.0f GB", gb);
    std::wstring result = text;
    std::replace(result.begin(), result.end(), L'.', L',');
    return result;
}

}  // namespace

PlayParts PlayStats(const Game& game) {
    PlayParts parts;
    parts.size = FormatSize(game.sizeBytes);
    parts.played = game.playtimeMinutes > 0 || game.lastPlayed > 0;
    if (!parts.played) {
        parts.playtime = L"Noch nicht gespielt";
        return parts;
    }
    if (game.playtimeMinutes > 0) {
        if (game.playtimeMinutes < 60) {
            parts.playtime = std::to_wstring(game.playtimeMinutes) + L" Min.";
        } else if (game.playtimeMinutes < 600) {
            wchar_t hours[32];
            swprintf_s(hours, L"%.1f", game.playtimeMinutes / 60.0);
            std::wstring text = hours;
            std::replace(text.begin(), text.end(), L'.', L',');
            parts.playtime = text + L" Std.";
        } else {
            parts.playtime = std::to_wstring(game.playtimeMinutes / 60) + L" Std.";
        }
    }
    if (game.lastPlayed > 0) {
        long long days = (std::time(nullptr) - game.lastPlayed) / 86400;
        std::wstring when;
        if (days <= 0) when = L"heute";
        else if (days == 1) when = L"gestern";
        else if (days < 14) when = L"vor " + std::to_wstring(days) + L" Tagen";
        else if (days < 60) when = L"vor " + std::to_wstring(days / 7) + L" Wochen";
        else if (days < 365) when = L"vor " + std::to_wstring(days / 30) + L" Monaten";
        else when = L"vor " + Plural(days / 365, L"Jahr", L"Jahren");
        parts.when = when;
    }
    return parts;
}

// "36 Std. · Zuletzt gespielt vor 3 Tagen · 54 GB", for the one-line list rows
std::wstring PlayInfo(const Game& game) {
    PlayParts parts = PlayStats(game);
    std::wstring text = game.installed ? parts.playtime : L"Nicht installiert";
    auto append = [&](const std::wstring& part) {
        if (part.empty()) return;
        text += text.empty() ? L"" : L"  ·  ";
        text += part;
    };
    if (!game.installed && parts.played) append(parts.playtime);
    if (!parts.when.empty()) append(L"Zuletzt gespielt " + parts.when);
    append(parts.size);
    return text;
}

const wchar_t* SortName(SortKey key) {
    switch (key) {
    case SortKey::Playtime: return L"Spielzeit";
    case SortKey::LastPlayed: return L"Zuletzt gespielt";
    case SortKey::Size: return L"Größe";
    default: return L"Name";
    }
}

const wchar_t* SortArrow(bool descending) { return descending ? L"↓" : L"↑"; }

SortKey SortKeyOf(Menu::Action action) {
    switch (action) {
    case Menu::SortByPlaytime: return SortKey::Playtime;
    case Menu::SortByLastPlayed: return SortKey::LastPlayed;
    case Menu::SortBySize: return SortKey::Size;
    default: return SortKey::Name;
    }
}

}  // namespace playr
