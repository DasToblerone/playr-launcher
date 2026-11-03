#include "net/SteamStore.h"

#include <algorithm>
#include <set>

#include "app/Messages.h"
#include "model/Library.h"
#include "net/Http.h"
#include "steam/Steam.h"
#include "storage/Storage.h"
#include "util/Text.h"
#include "util/Vdf.h"

namespace playr {

namespace {

std::string StoreDetails(const std::wstring& appId, const wchar_t* filters) {
    std::string json;
    HttpGet(L"https://store.steampowered.com/api/appdetails?l=english&appids=" + appId + L"&filters=" + filters,
        [&](const char* data, DWORD size) { json.append(data, size); });
    return json;
}

// Downloads `urls` in order into `finalPath` through a temp file; the first one that works wins.
bool FetchArt(const std::vector<std::wstring>& urls, const fs::path& finalPath) {
    std::error_code ec;
    fs::path tmp = finalPath;
    tmp += L".tmp";
    bool got = false;
    for (const auto& url : urls)
        if ((got = HttpGetToFile(url, tmp))) break;
    if (!got) { fs::remove(tmp, ec); return false; }

    // Virus scanners can briefly lock a freshly written file, so retry the rename.
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (MoveFileExW(tmp.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return true;
        Sleep(150);
    }
    fs::remove(tmp, ec);
    return false;
}

// Newer apps keep their header under a hashed path, so the store API is the fallback.
std::wstring StoreHeaderUrl(const std::wstring& appId) {
    std::string json = StoreDetails(appId, L"basic");
    const std::string key = "\"header_image\":\"";
    size_t start = json.find(key);
    if (start == std::string::npos) return {};
    std::string url;
    for (size_t i = start + key.size(); i < json.size() && json[i] != '"'; ++i) {
        if (json[i] != '\\' || i + 1 >= json.size()) { url += json[i]; continue; }
        if (json.compare(i + 1, 5, "u0026") == 0) { url += '&'; i += 5; }
        else url += json[++i];
    }
    return std::wstring(url.begin(), url.end());
}

// Downloads the wide header (list) and the portrait art (grid).
void DownloadCover(const std::wstring& appId) {
    std::error_code ec;
    unsigned long long id = wcstoull(appId.c_str(), nullptr, 10);
    const std::wstring base = L"https://cdn.cloudflare.steamstatic.com/steam/apps/" + appId + L"/";

    if (!fs::exists(CoverPath(appId), ec)) {
        bool got = FetchArt({ base + L"header.jpg" }, CoverPath(appId));
        if (!got) {
            std::wstring url = StoreHeaderUrl(appId);
            got = !url.empty() && FetchArt({ url }, CoverPath(appId));
        }
        if (got) PostMessageW(g_mainWnd, WM_COVER, (WPARAM)id, 0);
    }
    if (!fs::exists(PortraitPath(appId), ec) && FetchArt({ base + L"library_600x900.jpg" }, PortraitPath(appId)))
        PostMessageW(g_mainWnd, WM_COVER, (WPARAM)(id | kPortraitFlag), 0);
}

// Steam's store genres that mark software rather than games (Animation & Modeling, Utilities, ...).
bool IsSoftwareGenre(int id) {
    static const std::set<int> kSoftware{ 51, 52, 53, 55, 56, 57, 58, 59, 60 };
    return kSoftware.count(id) != 0;
}

// Whether the store's answer describes a game: the type must be "game" and no software genre may be set.
bool IsGameJson(const std::string& json) {
    if (json.find("\"success\":true") == std::string::npos) return true;  // unknown apps stay in the list
    bool isGame = true;
    size_t type = json.find("\"type\":\"");
    if (type != std::string::npos) isGame = json.compare(type + 8, 4, "game") == 0;
    size_t genres = json.find("\"genres\":[");
    if (genres != std::string::npos) {
        size_t end = json.find(']', genres);
        for (size_t pos = genres; (pos = json.find("\"id\":\"", pos)) != std::string::npos && pos < end; pos += 6)
            if (IsSoftwareGenre(atoi(json.c_str() + pos + 6))) isGame = false;
    }
    return isGame;
}

// Asks the store whether an app is a game. Failed requests are not cached, so they are retried next start.
void ClassifyApp(const std::wstring& appId) {
    Sleep(250);  // with three threads this is a few requests per second, below the store API's rate limit
    std::string json = StoreDetails(appId, L"basic,genres");
    if (json.empty()) return;
    PostMessageW(g_mainWnd, WM_CLASSIFIED, (WPARAM)wcstoull(appId.c_str(), nullptr, 10), IsGameJson(json) ? 1 : 0);
}

// Looks up an app the account owns but has not installed: its name, and whether it is a game at all.
void ResolveOwnedApp(const std::wstring& appId) {
    Sleep(250);
    std::string json = StoreDetails(appId, L"basic,genres");
    if (json.empty()) return;

    auto* result = new OwnedResult;
    result->appId = appId;
    result->isGame = json.find("\"success\":true") != std::string::npos && IsGameJson(json);
    size_t type = json.find("\"type\":\"");
    size_t name = type == std::string::npos ? std::string::npos : json.find("\"name\":\"", type);
    if (result->isGame && name != std::string::npos) {
        size_t start = name + 8, end = start;
        while (end < json.size() && json[end] != '"') end += json[end] == '\\' ? 2 : 1;
        result->name = ReadJsonString(Utf8ToWide(json.substr(start, end - start)), 0);
    }
    if (result->name.empty()) result->isGame = false;
    PostMessageW(g_mainWnd, WM_OWNED, 0, (LPARAM)result);
}

}  // namespace

WorkQueue g_coverQueue(DownloadCover, 6);  // the CDN has no rate limit, so several images load at once

namespace {

// The store API limits the request rate, so these run on a few threads with a short pause each.
WorkQueue g_classifyQueue(ClassifyApp, 3);
WorkQueue g_ownedQueue(ResolveOwnedApp, 3);

}  // namespace

void QueueBackgroundWork() {
    std::vector<std::wstring> unknown, owned;
    std::vector<const Game*> needArt;
    std::set<std::wstring> known;
    for (const auto& game : g_games) {
        known.insert(game.appId);
        std::error_code ec;
        if (!fs::exists(CoverPath(game.appId), ec) || !fs::exists(PortraitPath(game.appId), ec)) needArt.push_back(&game);
        if (!g_isGame.count(game.appId)) unknown.push_back(game.appId);
    }
    // On the first start the games you see first should get their pictures first: installed games, most recent on top.
    std::stable_sort(needArt.begin(), needArt.end(), [](const Game* a, const Game* b) {
        if (a->installed != b->installed) return a->installed;
        return a->lastPlayed > b->lastPlayed;
    });
    std::vector<std::wstring> covers;
    for (const Game* game : needArt) covers.push_back(game->appId);

    for (const auto& id : FindOwnedCandidates()) {
        auto verdict = g_isGame.find(id);
        if (!known.count(id) && !g_ownedNames.count(id) && (verdict == g_isGame.end() || verdict->second)) owned.push_back(id);
    }
    if (!covers.empty()) g_coverQueue.Push(covers);
    if (!unknown.empty()) g_classifyQueue.Push(unknown);
    if (!owned.empty()) g_ownedQueue.Push(owned);
}

}  // namespace playr
