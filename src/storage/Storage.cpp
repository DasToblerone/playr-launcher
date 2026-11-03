#include "storage/Storage.h"

#include <algorithm>
#include <fstream>

#include "model/Library.h"
#include "util/Text.h"

namespace playr {

std::wstring g_dataDir;
std::wstring g_coverDir;

fs::path SettingsPath() { return fs::path(g_dataDir) / L"settings.txt"; }
fs::path TypesPath() { return fs::path(g_dataDir) / L"types.txt"; }
fs::path OwnedPath() { return fs::path(g_dataDir) / L"owned.txt"; }
fs::path CoverPath(const std::wstring& appId) { return fs::path(g_coverDir) / (appId + L".jpg"); }
fs::path PortraitPath(const std::wstring& appId) { return fs::path(g_coverDir) / (appId + L"_p.jpg"); }

void SaveSettings() {
    std::ofstream file(SettingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) return;
    for (const auto& category : g_categories) {
        if (IsReservedCategory(category)) continue;
        file << "C\t";
        WriteUtf8(file, category);
        file << "\r\n";
    }
    file << "S\t" << (int)g_sortKey << '\t' << (g_sortDescending ? 1 : 0) << "\r\n";
    file << "U\t" << (g_showUninstalled ? 1 : 0) << "\r\n";
    for (const auto& game : g_games) {
        for (const auto& category : game.categories) {  // one line per membership
            file << "G\t";
            WriteUtf8(file, game.appId);
            file << '\t';
            WriteUtf8(file, category);
            file << "\r\n";
        }
    }
}

// Loads categories and assignments. Assignments are stored as nameless games and applied by MergeGames.
void LoadSettings() {
    ForEachLine(ReadTextFile(SettingsPath()), [](const std::wstring& line) {
        if (line.rfind(L"C\t", 0) == 0) {
            std::wstring category = line.substr(2);
            if (!category.empty() && std::find(g_categories.begin(), g_categories.end(), category) == g_categories.end())
                g_categories.push_back(category);
        } else if (line.rfind(L"S\t", 0) == 0) {
            size_t split = line.find(L'\t', 2);
            int key = _wtoi(line.substr(2, split == std::wstring::npos ? split : split - 2).c_str());
            if (key >= 0 && key <= (int)SortKey::Size) g_sortKey = (SortKey)key;
            g_sortDescending = split != std::wstring::npos && line.compare(split + 1, 1, L"1") == 0;
        } else if (line.rfind(L"U\t", 0) == 0) {
            g_showUninstalled = line.compare(2, 1, L"0") != 0;
        } else if (line.rfind(L"G\t", 0) == 0) {
            size_t split = line.find(L'\t', 2);
            if (split == std::wstring::npos) return;
            std::wstring id = line.substr(2, split - 2), category = line.substr(split + 1);
            if (id.empty() || category.empty()) return;
            Game saved;
            saved.appId = id;
            saved.categories = { category };
            g_games.push_back(saved);
        }
    });
}

void SaveTypes() {
    std::ofstream file(TypesPath(), std::ios::binary | std::ios::trunc);
    for (const auto& entry : g_isGame) {
        WriteUtf8(file, entry.first);
        file << '\t' << (entry.second ? '1' : '0') << "\r\n";
    }
}

void LoadTypes() {
    ForEachLine(ReadTextFile(TypesPath()), [](const std::wstring& line) {
        size_t split = line.find(L'\t');
        if (split != std::wstring::npos && split + 1 < line.size())
            g_isGame[line.substr(0, split)] = line[split + 1] == L'1';
    });
}

void SaveOwned() {
    std::ofstream file(OwnedPath(), std::ios::binary | std::ios::trunc);
    for (const auto& entry : g_ownedNames) {
        WriteUtf8(file, entry.first);
        file << '\t';
        WriteUtf8(file, entry.second);
        file << "\r\n";
    }
}

void LoadOwned() {
    ForEachLine(ReadTextFile(OwnedPath()), [](const std::wstring& line) {
        size_t split = line.find(L'\t');
        if (split != std::wstring::npos && split + 1 < line.size()) g_ownedNames[line.substr(0, split)] = line.substr(split + 1);
    });
}

void InitStorage() {
    g_dataDir = KnownFolder(FOLDERID_RoamingAppData) + L"\\PLAYR-Launcher";
    g_coverDir = g_dataDir + L"\\covers";
    {   // the app used to be called GameRandomizer: take over its settings and covers
        std::error_code ec;
        fs::path legacy = fs::path(KnownFolder(FOLDERID_RoamingAppData)) / L"GameRandomizer";
        if (fs::exists(legacy, ec) && !fs::exists(g_dataDir, ec)) fs::rename(legacy, g_dataDir, ec);
    }
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
    CreateDirectoryW(g_coverDir.c_str(), nullptr);
}

}  // namespace playr
