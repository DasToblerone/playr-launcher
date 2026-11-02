// Steam Game Randomizer
//
// Picks a random installed Steam game, or the one you click. Games are read from the local Steam
// libraries, cover art comes from Steam's CDN, and everything is drawn with GDI+ in a dark
// Spotify-like style with a purple accent.

#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <dwmapi.h>

#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>

#include <atomic>
#include <cmath>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

namespace {

// ============================================================================ data

struct Game {
    std::wstring appId;
    std::wstring name;
    std::wstring category = L"Unsortiert";
    int playtimeMinutes = 0;
    long long lastPlayed = 0;  // unix time, 0 when never played
};

const std::wstring kAllGames = L"Alle Spiele";
const std::wstring kUnsorted = L"Unsortiert";

std::vector<Game> g_games;
std::vector<std::wstring> g_categories{ kAllGames };  // chips; games without one carry kUnsorted internally
std::map<std::wstring, bool> g_isGame;   // appId -> verdict from the Steam store, cached on disk

std::wstring g_dataDir;
std::wstring g_coverDir;
std::wstring g_steamName;  // persona name of the Steam account that signed in last

fs::path SettingsPath() { return fs::path(g_dataDir) / L"settings.txt"; }
fs::path TypesPath() { return fs::path(g_dataDir) / L"types.txt"; }
fs::path CoverPath(const std::wstring& appId) { return fs::path(g_coverDir) / (appId + L".jpg"); }
fs::path PortraitPath(const std::wstring& appId) { return fs::path(g_coverDir) / (appId + L"_p.jpg"); }

bool IsReservedCategory(const std::wstring& name) { return name == kAllGames || name == kUnsorted; }

// ============================================================================ file helpers

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR folder = nullptr;
    std::wstring result = L".";
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &folder))) {
        result = folder;
        CoTaskMemFree(folder);
    }
    return result;
}

std::wstring RegistryString(HKEY root, const wchar_t* subkey, const wchar_t* valueName) {
    wchar_t buffer[32768]{};
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, nullptr, buffer, &bytes) == ERROR_SUCCESS)
        return buffer;
    return {};
}

std::wstring ReadTextFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(0, 3);
    if (bytes.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), result.data(), size);
    return result;
}

void WriteUtf8(std::ofstream& file, const std::wstring& value) {
    if (value.empty()) return;
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    std::string bytes(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), bytes.data(), size, nullptr, nullptr);
    file.write(bytes.data(), bytes.size());
}

// Calls `handle` with every line of a text file, without the line break.
void ForEachLine(const std::wstring& text, const std::function<void(const std::wstring&)>& handle) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        handle(line);
        start = end + 1;
    }
}

// ============================================================================ Steam discovery

// Reads the quoted value that follows a quoted key and collapses VDF's doubled backslashes.
std::wstring UnescapeVdf(const std::wstring& value) {
    std::wstring out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && i + 1 < value.size() && value[i + 1] == L'\\') ++i;
        out += value[i];
    }
    return out;
}

// Finds `"key" "value"` after `from` and returns the value. `end` receives the position after it.
std::wstring VdfValue(const std::wstring& text, const std::wstring& key, size_t from = 0, size_t* end = nullptr) {
    std::wstring marker = L"\"" + key + L"\"";
    size_t pos = text.find(marker, from);
    if (pos == std::wstring::npos) return {};
    size_t open = text.find(L'"', pos + marker.size());
    if (open == std::wstring::npos) return {};
    size_t close = text.find(L'"', open + 1);
    if (close == std::wstring::npos) return {};
    if (end) *end = close + 1;
    return UnescapeVdf(text.substr(open + 1, close - open - 1));
}

std::wstring SteamPath() {
    std::wstring steam = RegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty())
        steam = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (steam.empty()) {
        wchar_t programFiles[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"PROGRAMFILES(X86)", programFiles, MAX_PATH))
            steam = std::wstring(programFiles) + L"\\Steam";
    }
    return steam;
}

std::vector<fs::path> FindSteamLibraries() {
    std::wstring steam = SteamPath();
    if (steam.empty()) return {};

    std::set<std::wstring> unique{ fs::path(steam).wstring() };
    std::wstring vdf = ReadTextFile(fs::path(steam) / L"steamapps" / L"libraryfolders.vdf");
    for (size_t pos = 0;;) {
        std::wstring path = VdfValue(vdf, L"path", pos, &pos);
        if (path.empty()) break;
        unique.insert(path);
    }

    std::vector<fs::path> libraries;
    for (const auto& item : unique) {
        std::error_code ec;
        if (fs::exists(fs::path(item) / L"steamapps", ec)) libraries.emplace_back(item);
    }
    return libraries;
}

// Steam's own helper packages show up as installed apps but are not games.
bool IsSteamTool(const std::wstring& name) {
    for (const wchar_t* prefix : { L"Steamworks Common", L"Steam Linux Runtime", L"Proton", L"Steam Controller" })
        if (name.rfind(prefix, 0) == 0) return true;
    return false;
}

std::map<std::wstring, Game> ScanSteamGames() {
    std::map<std::wstring, Game> found;
    for (const auto& library : FindSteamLibraries()) {
        std::error_code ec;
        for (fs::directory_iterator it(library / L"steamapps", ec), end; !ec && it != end; it.increment(ec)) {
            std::wstring filename = it->path().filename().wstring();
            if (filename.rfind(L"appmanifest_", 0) != 0 || it->path().extension() != L".acf") continue;

            std::wstring vdf = ReadTextFile(it->path());
            Game game;
            game.appId = VdfValue(vdf, L"appid");
            game.name = VdfValue(vdf, L"name");
            if (game.appId.empty() || game.name.empty() || IsSteamTool(game.name)) continue;
            found[game.appId] = game;
        }
    }
    return found;
}

// ---------------------------------------------------------------- Steam collections

// Reads a JSON string body starting right after the opening quote and decodes its escapes.
std::wstring ReadJsonString(const std::wstring& text, size_t pos) {
    std::wstring out;
    for (; pos < text.size() && text[pos] != L'"'; ++pos) {
        if (text[pos] != L'\\' || pos + 1 >= text.size()) { out += text[pos]; continue; }
        wchar_t c = text[++pos];
        if (c == L'n') out += L'\n';
        else if (c == L't') out += L'\t';
        else if (c == L'u' && pos + 4 < text.size()) {
            out += (wchar_t)std::wcstoul(text.substr(pos + 1, 4).c_str(), nullptr, 16);
            pos += 4;
        } else out += c;
    }
    return out;
}

struct ImportResult {
    int categories = 0;
    int games = 0;
};

// Steam keeps collections in userdata/<account>/config/cloudstorage. Only collections that hold at
// least one installed game are imported, and only games without a category are moved into them.
// Smaller collections go first so a game lands in its most specific collection.
ImportResult ImportSteamCategories() {
    struct Collection {
        std::wstring name;
        std::vector<Game*> members;
    };
    std::vector<Collection> collections;

    std::error_code ec;
    std::wstring steam = SteamPath();
    if (!steam.empty()) {
        for (fs::directory_iterator it(fs::path(steam) / L"userdata", ec), end; !ec && it != end; it.increment(ec)) {
            std::wstring text = ReadTextFile(it->path() / L"config" / L"cloudstorage" / L"cloud-storage-namespace-1.json");
            const std::wstring keyMarker = L"\"key\":\"user-collections.";
            for (size_t pos = 0; (pos = text.find(keyMarker, pos)) != std::wstring::npos;) {
                size_t idPos = pos + keyMarker.size();
                size_t entryEnd = text.find(L"}],[", pos);
                size_t valuePos = text.find(L"\"value\":\"", pos);
                bool hidden = ReadJsonString(text, idPos) == L"hidden";
                pos = idPos;
                // Deleted collections have no value of their own; the next hit belongs to another entry.
                if (hidden || valuePos == std::wstring::npos || valuePos > entryEnd) continue;

                std::wstring inner = ReadJsonString(text, valuePos + 9);
                size_t namePos = inner.find(L"\"name\":\"");
                size_t addedPos = inner.find(L"\"added\":[");
                if (namePos == std::wstring::npos || addedPos == std::wstring::npos) continue;

                Collection collection;
                collection.name = ReadJsonString(inner, namePos + 8);
                size_t listEnd = inner.find(L']', addedPos);
                std::wstring list = inner.substr(addedPos + 9, listEnd - addedPos - 9);
                for (size_t start = 0; start < list.size();) {
                    size_t comma = list.find(L',', start);
                    if (comma == std::wstring::npos) comma = list.size();
                    std::wstring id = list.substr(start, comma - start);
                    for (auto& game : g_games)
                        if (game.appId == id) collection.members.push_back(&game);
                    start = comma + 1;
                }
                if (!collection.name.empty() && !collection.members.empty()) collections.push_back(collection);
            }
        }
    }

    std::stable_sort(collections.begin(), collections.end(), [](const Collection& a, const Collection& b) {
        return a.members.size() < b.members.size();
    });

    ImportResult result;
    for (const auto& collection : collections) {
        const std::wstring& name = collection.name;
        if (IsReservedCategory(name) || name.find_first_of(L"\t\r\n") != std::wstring::npos) continue;
        int assignable = (int)std::count_if(collection.members.begin(), collection.members.end(),
            [](const Game* game) { return game->category == kUnsorted; });
        if (assignable == 0) continue;
        if (std::find(g_categories.begin(), g_categories.end(), name) == g_categories.end()) {
            g_categories.push_back(name);
            ++result.categories;
        }
        for (Game* game : collection.members)
            if (game->category == kUnsorted) game->category = name;
        result.games += assignable;
    }
    return result;
}

// ============================================================================ persistence

void SaveSettings() {
    std::ofstream file(SettingsPath(), std::ios::binary | std::ios::trunc);
    if (!file) return;
    for (const auto& category : g_categories) {
        if (IsReservedCategory(category)) continue;
        file << "C\t";
        WriteUtf8(file, category);
        file << "\r\n";
    }
    for (const auto& game : g_games) {
        file << "G\t";
        WriteUtf8(file, game.appId);
        file << '\t';
        WriteUtf8(file, game.category);
        file << "\r\n";
    }
}

// Loads categories and assignments. Assignments are stored as nameless games and applied by MergeGames.
void LoadSettings() {
    ForEachLine(ReadTextFile(SettingsPath()), [](const std::wstring& line) {
        if (line.rfind(L"C\t", 0) == 0) {
            std::wstring category = line.substr(2);
            if (!category.empty() && std::find(g_categories.begin(), g_categories.end(), category) == g_categories.end())
                g_categories.push_back(category);
        } else if (line.rfind(L"G\t", 0) == 0) {
            size_t split = line.find(L'\t', 2);
            if (split == std::wstring::npos) return;
            std::wstring id = line.substr(2, split - 2), category = line.substr(split + 1);
            if (!id.empty() && !category.empty()) g_games.push_back({ id, L"", category });
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

std::wstring SteamPersonaName() {
    std::wstring text = ReadTextFile(fs::path(SteamPath()) / L"config" / L"loginusers.vdf");
    // Several accounts can be listed; prefer the one marked as the most recent.
    for (size_t pos = 0; (pos = text.find(L"\"MostRecent\"", pos)) != std::wstring::npos; pos += 12) {
        if (VdfValue(text, L"MostRecent", pos) != L"1") continue;
        size_t name = text.rfind(L"\"PersonaName\"", pos);
        if (name != std::wstring::npos) return VdfValue(text, L"PersonaName", name);
    }
    return VdfValue(text, L"PersonaName");
}

// Position of the brace that closes the block opened at `open`.
size_t BlockEnd(const std::wstring& text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == L'{') ++depth;
        else if (text[i] == L'}' && --depth == 0) return i;
    }
    return text.size();
}

// Reads play time (minutes) and last played (unix time) per app from Steam's localconfig.vdf.
void ApplyPlayStats() {
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(SteamPath()) / L"userdata", ec), end; !ec && it != end; it.increment(ec)) {
        std::wstring text = ReadTextFile(it->path() / L"config" / L"localconfig.vdf");
        if (text.empty()) continue;
        for (auto& game : g_games) {
            const std::wstring marker = L"\"" + game.appId + L"\"";
            for (size_t pos = 0; (pos = text.find(marker, pos)) != std::wstring::npos; pos += marker.size()) {
                size_t open = text.find_first_not_of(L" \t\r\n", pos + marker.size());
                if (open == std::wstring::npos || text[open] != L'{') continue;
                std::wstring block = text.substr(open, BlockEnd(text, open) - open);
                std::wstring played = VdfValue(block, L"LastPlayed"), minutes = VdfValue(block, L"Playtime");
                if (played.empty() && minutes.empty()) continue;
                game.lastPlayed = std::max(game.lastPlayed, _wtoi64(played.c_str()));
                game.playtimeMinutes = std::max(game.playtimeMinutes, _wtoi(minutes.c_str()));
                break;
            }
        }
    }
}

// Replaces the game list with `detected`, keeping saved category assignments and dropping known non-games.
void MergeGames(const std::map<std::wstring, Game>& detected) {
    std::map<std::wstring, std::wstring> assignments;
    for (const auto& game : g_games)
        if (!game.appId.empty() && !game.category.empty()) assignments[game.appId] = game.category;

    g_games.clear();
    for (const auto& entry : detected) {
        Game game = entry.second;
        auto verdict = g_isGame.find(game.appId);
        if (verdict != g_isGame.end() && !verdict->second) continue;
        auto saved = assignments.find(game.appId);
        if (saved != assignments.end() &&
            std::find(g_categories.begin(), g_categories.end(), saved->second) != g_categories.end())
            game.category = saved->second;
        g_games.push_back(game);
    }
    std::sort(g_games.begin(), g_games.end(), [](const Game& a, const Game& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    ApplyPlayStats();
    SaveSettings();
}

// ============================================================================ networking

// Fetches an https URL and streams a 200 response body into `sink`.
template <typename Sink>
bool HttpGet(const std::wstring& url, Sink sink) {
    wchar_t host[256]{}, path[2048]{};
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return false;
    std::wstring target = std::wstring(path) + std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(L"GameRandomizer/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
    HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;

    bool ok = false;
    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            std::vector<char> buffer(16384);
            DWORD read = 0;
            ok = true;
            while (WinHttpReadData(request, buffer.data(), (DWORD)buffer.size(), &read) && read)
                sink(buffer.data(), read);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

bool HttpGetToFile(const std::wstring& url, const fs::path& out) {
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    bool ok = HttpGet(url, [&](const char* data, DWORD size) { file.write(data, size); });
    file.close();
    return ok && !file.fail();
}

std::string StoreDetails(const std::wstring& appId, const wchar_t* filters) {
    std::string json;
    HttpGet(L"https://store.steampowered.com/api/appdetails?l=english&appids=" + appId + L"&filters=" + filters,
        [&](const char* data, DWORD size) { json.append(data, size); });
    return json;
}

// A queue of strings handled one by one on a background thread that starts on demand.
class WorkQueue {
public:
    explicit WorkQueue(std::function<void(const std::wstring&)> handler) : handler_(std::move(handler)) {}

    void Push(const std::vector<std::wstring>& items) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            items_.insert(items_.end(), items.begin(), items.end());
        }
        bool expected = false;
        if (running_.compare_exchange_strong(expected, true)) std::thread([this] { Run(); }).detach();
    }

private:
    void Run() {
        for (;;) {
            std::wstring item;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (items_.empty()) { running_ = false; return; }
                item = items_.front();
                items_.pop_front();
            }
            handler_(item);
        }
    }

    std::function<void(const std::wstring&)> handler_;
    std::mutex mutex_;
    std::deque<std::wstring> items_;
    std::atomic<bool> running_{ false };
};

constexpr UINT WM_COVER = WM_APP + 1;       // wParam: appId whose art finished downloading (+ kPortraitFlag)
constexpr unsigned long long kPortraitFlag = 1ull << 62;
constexpr UINT WM_SCANDONE = WM_APP + 2;    // lParam: heap-allocated std::map<std::wstring, Game>
constexpr UINT WM_CLASSIFIED = WM_APP + 3;  // wParam: appId, lParam: 1 when it is a game

HWND g_mainWnd = nullptr;

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

// Asks the store whether an app is a game. Failed requests are not cached, so they are retried next start.
void ClassifyApp(const std::wstring& appId) {
    Sleep(300);  // stay well below the store API's rate limit
    std::string json = StoreDetails(appId, L"basic,genres");
    if (json.empty()) return;

    bool isGame = true;
    if (json.find("\"success\":true") != std::string::npos) {
        size_t type = json.find("\"type\":\"");
        if (type != std::string::npos) isGame = json.compare(type + 8, 4, "game") == 0;
        size_t genres = json.find("\"genres\":[");
        if (genres != std::string::npos) {
            size_t end = json.find(']', genres);
            for (size_t pos = genres; (pos = json.find("\"id\":\"", pos)) != std::string::npos && pos < end; pos += 6)
                if (IsSoftwareGenre(atoi(json.c_str() + pos + 6))) isGame = false;
        }
    }
    PostMessageW(g_mainWnd, WM_CLASSIFIED, (WPARAM)wcstoull(appId.c_str(), nullptr, 10), isGame ? 1 : 0);
}

WorkQueue g_coverQueue(DownloadCover);
WorkQueue g_classifyQueue(ClassifyApp);

void QueueBackgroundWork() {
    std::vector<std::wstring> covers, unknown;
    for (const auto& game : g_games) {
        std::error_code ec;
        if (!fs::exists(CoverPath(game.appId), ec) || !fs::exists(PortraitPath(game.appId), ec))
            covers.push_back(game.appId);
        if (!g_isGame.count(game.appId)) unknown.push_back(game.appId);
    }
    if (!covers.empty()) g_coverQueue.Push(covers);
    if (!unknown.empty()) g_classifyQueue.Push(unknown);
}

// ============================================================================ UI

using namespace Gdiplus;

// Sizes are in 96-dpi pixels and go through S() before use.
constexpr float kMargin = 24, kRowHeight = 64, kRowInset = 14, kNumberColumn = 52;
constexpr float kCoverWidth = 96, kCoverHeight = 45;
constexpr float kChipTop = 108, kChipHeight = 32, kChipGap = 8, kRenameMinWidth = 130;
constexpr float kMenuWidth = 176, kMenuItemHeight = 38, kMenuPad = 6;
constexpr float kBarHeight = 76, kControlHeight = 40, kBarMargin = 20, kGap = 8;
constexpr float kPillWidth = 236, kRandomWidth = 190;
constexpr float kMinClientWidth = 830, kMinClientHeight = 520;
constexpr float kStartClientWidth = 850, kStartClientHeight = 640;

constexpr int ID_EDIT = 2001;
constexpr int ID_RENAME_EDIT = 2002;
constexpr int ID_ACTION = 3000;  // + button index
constexpr UINT_PTR TIMER_ANIMATION = 1;
constexpr DWORD WINDOW_STYLE = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

// Wide windows switch from a list to a grid of cards.
constexpr float kGridMinWidth = 1100, kCardPad = 12, kCardGap = 8, kCardTargetWidth = 200, kCardTextHeight = 58;

constexpr int ID_CREATE_CATEGORY = 3100;

enum { BTN_SCAN, BTN_IMPORT, BTN_RANDOM, BTN_COUNT };
enum class Icon { None, Refresh, Download };
const wchar_t* const kButtonLabels[BTN_COUNT] = { L"Neu scannen", L"Kategorien aus Steam", L"Zufällig auswählen" };
const Icon kButtonIcons[BTN_COUNT] = { Icon::Refresh, Icon::Download, Icon::None };
const float kButtonWidths[BTN_COUNT - 1] = { 140, 200 };

const Color kBg(255, 18, 18, 18);
const Color kBar(255, 0, 0, 0);
const Color kSurface(255, 42, 42, 42);
const Color kField(255, 31, 31, 31);
const Color kSurfaceHover(255, 31, 31, 31);
const Color kChipHover(255, 62, 62, 62);
const Color kCover(255, 40, 40, 40);
const Color kAccent(255, 168, 85, 247);
const Color kAccentHover(255, 192, 132, 252);
const Color kText(255, 255, 255, 255);
const Color kText2(255, 179, 179, 179);
const Color kLine(255, 31, 31, 31);
const Color kOutline(255, 114, 114, 114);
const Color kOnAccent(255, 0, 0, 0);
const Color kDanger(255, 229, 72, 77);
const Color kDangerHover(255, 240, 98, 102);

struct Layout {
    RECT list{};
    RECT bar{};
    RECT pill{};
    RECT btn[BTN_COUNT]{};
    std::vector<RECT> chips;
    int rowH = 0;  // vertical step between rows of items (list rows or card rows)
    int wheelStep = 0;
    bool grid = false;
    int cols = 1, cellW = 0, cellH = 0, cardGap = 0, cardPad = 0, artW = 0, artH = 0;
};

struct Hover {
    int row = -1, button = -1, chip = -1;
    bool cover = false;
    int menuItem = -1;
    bool Any() const { return row >= 0 || button >= 0 || chip >= 0 || menuItem >= 0; }
};

// Context menu of a category chip, opened with the right mouse button.
struct Menu {
    enum Item { Rename, Delete, ItemCount };
    bool open = false;
    int chip = -1;
    RECT rect{};
    RECT items[ItemCount]{};
};

// A short message above the bottom bar.
struct Toast {
    bool active = false;
    std::wstring text;
    ULONGLONG since = 0, duration = 0;
    RECT rect{};
};

struct DragState {
    int row = -1;            // visible index of the pressed row
    bool active = false;     // true once the pointer moved far enough to count as a drag
    POINT start{}, pos{};
    int dropChip = -1;
};

// Short feedback inside a button: a spinner, an animated check mark, or plain text.
struct Badge {
    enum Mode { Off, Spinner, Check, Text } mode = Off;
    std::wstring text;
    ULONGLONG since = 0;
    ULONGLONG hideAfter = 0;  // ms, for Check and Text
};

float g_scale = 1.0f;
Layout g_L;
Hover g_hover;
DragState g_drag;
Badge g_badge[BTN_COUNT];
Toast g_toast;
Badge g_importResult;
bool g_importReady = false;
Menu g_menu;
int g_renameChip = -1;   // category chip being edited in place
HWND g_renameEdit = nullptr;
bool g_scanReady = false;
bool g_tracking = false;
bool g_fullscreen = false;
HWND g_edit = nullptr;
HFONT g_editFont = nullptr;
HBRUSH g_editBrush = nullptr;
std::vector<int> g_visible;  // indices into g_games for the current category
int g_active = 0;            // selected category chip
int g_sel = -1;              // selected game (index into g_games)
int g_scroll = 0;

int S(float v) { return (int)std::lround(v * g_scale); }

RectF RF(const RECT& r) {
    return RectF((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
}

bool In(const RECT& r, POINT p) { return PtInRect(&r, p) != 0; }

const std::wstring& SelectedCategory() {
    return g_active >= 0 && g_active < (int)g_categories.size() ? g_categories[g_active] : kAllGames;
}

int ContentHeight() {
    int rows = ((int)g_visible.size() + g_L.cols - 1) / g_L.cols;
    return rows * g_L.rowH;
}

int MaxScroll() {
    int view = (int)(g_L.list.bottom - g_L.list.top);
    return std::max(0, ContentHeight() - view);
}

void ClampScroll() { g_scroll = std::clamp(g_scroll, 0, MaxScroll()); }

void Redraw(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void LayoutToast(HWND hwnd);

void RefreshGames() {
    const std::wstring& filter = SelectedCategory();
    g_visible.clear();
    bool selectionVisible = false;
    for (int i = 0; i < (int)g_games.size(); ++i) {
        if (filter != kAllGames && g_games[i].category != filter) continue;
        g_visible.push_back(i);
        selectionVisible |= i == g_sel;
    }
    if (!selectionVisible) g_sel = -1;
    ClampScroll();
}

void Relayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    g_L.rowH = S(kRowHeight);
    g_L.cols = 1;
    g_L.grid = W >= S(kGridMinWidth);
    if (g_L.grid) {
        // Portrait art in a grid; the cells stretch so the columns fill the window.
        g_L.cardPad = S(kCardPad);
        g_L.cardGap = S(kCardGap);
        int available = W - 2 * (S(kMargin) - g_L.cardPad);
        g_L.cols = std::max(1, (available + g_L.cardGap) / (S(kCardTargetWidth) + g_L.cardGap));
        g_L.cellW = (available - g_L.cardGap * (g_L.cols - 1)) / g_L.cols;
        g_L.artW = g_L.cellW - 2 * g_L.cardPad;
        g_L.artH = g_L.artW * 3 / 2;
        g_L.cellH = g_L.cardPad + g_L.artH + S(10) + S(kCardTextHeight) + g_L.cardPad;
        g_L.rowH = g_L.cellH + g_L.cardGap;
    }
    g_L.wheelStep = g_L.grid ? g_L.rowH / 3 : g_L.rowH;
    g_L.chips.clear();
    {
        Graphics g(hwnd);
        FontFamily family(L"Arial");
        Font font(&family, (REAL)S(13), FontStyleBold, UnitPixel);
        int x = S(kMargin), y = S(kChipTop), h = S(kChipHeight), gap = S(kChipGap);
        for (int i = 0; i < (int)g_categories.size(); ++i) {
            RectF box;
            g.MeasureString(g_categories[i].c_str(), -1, &font, PointF(0, 0), &box);
            int w = (int)box.Width + S(28);
            if (i == g_renameChip) w = std::max(w, S(kRenameMinWidth));
            if (x + w > W - S(kMargin) && x > S(kMargin)) { x = S(kMargin); y += h + gap; }
            g_L.chips.push_back({ x, y, x + w, y + h });
            x += w + gap;
        }
        int listTop = y + h + S(16);
        g_L.bar = { 0, H - S(kBarHeight), W, H };
        g_L.list = { 0, listTop, W, std::max(listTop, (int)g_L.bar.top) };
    }

    int ph = S(kControlHeight);
    int py = g_L.bar.top + (S(kBarHeight) - ph) / 2;
    g_L.pill = { S(kBarMargin), py, S(kBarMargin) + S(kPillWidth), py + ph };
    int x = g_L.pill.right + S(kGap);
    for (int i = 0; i < BTN_RANDOM; ++i) {
        g_L.btn[i] = { x, py, x + S(kButtonWidths[i]), py + ph };
        x += S(kButtonWidths[i]) + S(kGap);
    }
    g_L.btn[BTN_RANDOM] = { W - S(kBarMargin) - S(kRandomWidth), py, W - S(kBarMargin), py + ph };

    if (g_edit) {
        int eh = S(18), left = g_L.pill.left + S(20);
        SetWindowPos(g_edit, nullptr, left, py + (ph - eh) / 2, g_L.pill.right - S(20) - left, eh, SWP_NOZORDER);
    }
    if (g_renameEdit && g_renameChip >= 0 && g_renameChip < (int)g_L.chips.size()) {
        const RECT& c = g_L.chips[g_renameChip];
        int eh = S(18);
        SetWindowPos(g_renameEdit, HWND_TOP, c.left + S(14), c.top + (c.bottom - c.top - eh) / 2,
            c.right - c.left - S(28), eh, SWP_SHOWWINDOW);
    }
    ClampScroll();
    if (g_toast.active) LayoutToast(hwnd);
    Redraw(hwnd);
}

// ---------------------------------------------------------------- covers

// Art is loaded lazily and kept; a missing file is only looked up once until its download finishes.
struct ArtCache {
    std::map<unsigned long long, Bitmap*> bitmaps;
    std::set<unsigned long long> tried;

    Bitmap* Get(const std::wstring& appId, const fs::path& path) {
        unsigned long long id = wcstoull(appId.c_str(), nullptr, 10);
        auto it = bitmaps.find(id);
        if (it != bitmaps.end()) return it->second;
        if (!tried.insert(id).second) return nullptr;

        std::error_code ec;
        if (!fs::exists(path, ec)) return nullptr;
        Bitmap* bitmap = Bitmap::FromFile(path.c_str());
        if (!bitmap || bitmap->GetLastStatus() != Ok) { delete bitmap; return nullptr; }
        bitmaps[id] = bitmap;
        return bitmap;
    }

    void Forget(unsigned long long id) {
        auto it = bitmaps.find(id);
        if (it != bitmaps.end()) { delete it->second; bitmaps.erase(it); }
        tried.erase(id);
    }

    void Clear() {
        for (auto& entry : bitmaps) delete entry.second;
        bitmaps.clear();
    }
};

ArtCache g_headers, g_portraits;

Bitmap* GetCover(const std::wstring& appId) { return g_headers.Get(appId, CoverPath(appId)); }
Bitmap* GetPortrait(const std::wstring& appId) { return g_portraits.Get(appId, PortraitPath(appId)); }

void OnCoverDownloaded(HWND hwnd, unsigned long long id) {
    if (id & kPortraitFlag) g_portraits.Forget(id & ~kPortraitFlag);
    else g_headers.Forget(id);
    Redraw(hwnd);
}

// ---------------------------------------------------------------- drawing helpers

Color WithAlpha(const Color& c, REAL alpha) {
    return Color((BYTE)std::lround(c.GetA() * std::clamp(alpha, 0.0f, 1.0f)), c.GetR(), c.GetG(), c.GetB());
}

void RoundPath(GraphicsPath& path, RectF r, REAL radius) {
    REAL d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRound(Graphics& g, RectF r, REAL radius, const Color& color) {
    GraphicsPath path;
    RoundPath(path, r, radius);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

// Strokes a pill-shaped outline inside `r` so the line stays within the rectangle.
void StrokePill(Graphics& g, RectF r, const Color& color, REAL width, bool dashed = false) {
    GraphicsPath path;
    REAL inset = width / 2;
    RoundPath(path, RectF(r.X + inset, r.Y + inset, r.Width - 2 * inset, r.Height - 2 * inset), r.Height / 2 - inset);
    Pen pen(color, width);
    if (dashed) pen.SetDashStyle(DashStyleDash);
    g.DrawPath(&pen, &path);
}

// `wrap` lets the text use as many lines as the rectangle holds and ends the last one with an ellipsis.
void DrawLabel(Graphics& g, const std::wstring& text, REAL px, bool bold, const Color& color, RectF r,
    StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentNear, bool wrap = false) {
    FontFamily family(L"Arial");
    Font font(&family, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    StringFormat format;
    format.SetAlignment(h);
    format.SetLineAlignment(v);
    format.SetTrimming(wrap ? StringTrimmingEllipsisWord : StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(wrap ? StringFormatFlagsLineLimit : StringFormatFlagsNoWrap);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), -1, &font, r, &format, &brush);
}

void DrawCentered(Graphics& g, const std::wstring& text, REAL px, bool bold, const Color& color, RectF r) {
    DrawLabel(g, text, px, bold, color, r, StringAlignmentCenter, StringAlignmentCenter);
}

void DrawPlayTriangle(Graphics& g, REAL cx, REAL cy, REAL size, const Color& color) {
    PointF triangle[3] = { PointF(cx - size * 0.3f, cy - size / 2), PointF(cx - size * 0.3f, cy + size / 2),
        PointF(cx + size * 0.6f, cy) };
    SolidBrush brush(color);
    g.FillPolygon(&brush, triangle, 3);
}

// Spinner, animated check mark or text, centered in a button.
void DrawBadge(Graphics& g, RectF r, const Badge& badge, const Color& ink = kText, const Color& accent = kAccentHover) {
    FontFamily family(L"Arial");
    Font font(&family, (REAL)S(13), FontStyleBold, UnitPixel);
    RectF box;
    g.MeasureString(badge.text.c_str(), -1, &font, PointF(0, 0), &box);
    bool hasIcon = badge.mode == Badge::Spinner || badge.mode == Badge::Check;
    REAL icon = hasIcon ? (REAL)S(16) : 0.0f, gap = hasIcon ? (REAL)S(6) : 0.0f, textW = box.Width - S(8);
    REAL startX = r.X + (r.Width - (icon + gap + textW)) / 2;
    REAL cx = startX + icon / 2, cy = r.Y + r.Height / 2;

    Pen pen(accent, (REAL)S(2));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    if (badge.mode == Badge::Spinner) {
        REAL angle = (REAL)(GetTickCount64() % 800) / 800.0f * 360.0f;
        g.DrawArc(&pen, cx - icon / 2 + 1, cy - icon / 2 + 1, icon - 2, icon - 2, angle, 270);
    } else if (badge.mode == Badge::Check) {
        double t = std::min(1.0, (double)(GetTickCount64() - badge.since) / 260.0);
        REAL drawn = (REAL)(1.0 - std::pow(1.0 - t, 3.0));
        REAL k = (REAL)S(1);
        PointF a(cx - 5 * k, cy + 0.5f * k), b(cx - 1.5f * k, cy + 4 * k), c(cx + 6 * k, cy - 4 * k);
        REAL l1 = std::hypot(b.X - a.X, b.Y - a.Y), l2 = std::hypot(c.X - b.X, c.Y - b.Y);
        REAL length = drawn * (l1 + l2);
        REAL f1 = std::min(length, l1) / l1;
        g.DrawLine(&pen, a, PointF(a.X + (b.X - a.X) * f1, a.Y + (b.Y - a.Y) * f1));
        if (length > l1) {
            REAL f2 = (length - l1) / l2;
            g.DrawLine(&pen, b, PointF(b.X + (c.X - b.X) * f2, b.Y + (c.Y - b.Y) * f2));
        }
    }
    DrawLabel(g, badge.text, (REAL)S(13), true, ink,
        RectF(startX + icon + gap, r.Y, textW + S(12), r.Height), StringAlignmentNear, StringAlignmentCenter);
}

// ---------------------------------------------------------------- rows

// Geometry of item `i` (visible index): a list row, or a card in the grid.
int ItemTop(int i) { return g_L.list.top + (i / g_L.cols) * g_L.rowH - g_scroll; }

RECT ItemCell(int i) {
    int top = ItemTop(i);
    if (!g_L.grid) return { 0, top, (int)g_L.list.right, top + g_L.rowH };
    int left = S(kMargin) - g_L.cardPad + (i % g_L.cols) * (g_L.cellW + g_L.cardGap);
    return { left, top, left + g_L.cellW, top + g_L.cellH };
}

RECT ArtRect(int i) {
    RECT cell = ItemCell(i);
    if (g_L.grid) {
        int left = cell.left + g_L.cardPad, top = cell.top + g_L.cardPad;
        return { left, top, left + g_L.artW, top + g_L.artH };
    }
    int left = S(kRowInset) + S(kNumberColumn);
    int top = cell.top + S(2) + (g_L.rowH - S(4) - S(kCoverHeight)) / 2;
    return { left, top, left + S(kCoverWidth), top + S(kCoverHeight) };
}

// The area that starts the game when clicked: the whole cover in the list, the round button on a card.
RECT PlayRect(int i) {
    RECT art = ArtRect(i);
    if (!g_L.grid) return art;
    int d = S(44), inset = S(8);
    return { art.right - inset - d, art.bottom - inset - d, art.right - inset, art.bottom - inset };
}

bool InPlayZone(int i, POINT p) {
    if (i < 0) return false;
    RECT r = PlayRect(i);
    return In(r, p);
}

std::wstring Plural(long long n, const wchar_t* one, const wchar_t* many) {
    return std::to_wstring(n) + L" " + (n == 1 ? one : many);
}

// "36 Std. · Zuletzt gespielt vor 3 Tagen"
std::wstring PlayInfo(const Game& game) {
    if (game.playtimeMinutes == 0 && game.lastPlayed == 0) return L"Noch nicht gespielt";

    std::wstring parts;
    if (game.playtimeMinutes > 0) {
        if (game.playtimeMinutes < 60) {
            parts = std::to_wstring(game.playtimeMinutes) + L" Min.";
        } else if (game.playtimeMinutes < 600) {
            wchar_t hours[32];
            swprintf_s(hours, L"%.1f", game.playtimeMinutes / 60.0);
            std::wstring text = hours;
            std::replace(text.begin(), text.end(), L'.', L',');
            parts = text + L" Std.";
        } else {
            parts = std::to_wstring(game.playtimeMinutes / 60) + L" Std.";
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
        if (!parts.empty()) parts += L"  ·  ";
        parts += L"Zuletzt gespielt " + when;
    }
    return parts;
}

void DrawRow(Graphics& g, int visibleIndex, int W) {
    int rowTop = ItemTop(visibleIndex);
    const Game& game = g_games[g_visible[visibleIndex]];
    bool selected = g_visible[visibleIndex] == g_sel;
    bool hover = visibleIndex == g_hover.row;

    RectF slot((REAL)S(kRowInset), (REAL)rowTop + S(2), (REAL)(W - 2 * S(kRowInset)), (REAL)(g_L.rowH - S(4)));
    if (selected) FillRound(g, slot, (REAL)S(8), kSurface);
    else if (hover) FillRound(g, slot, (REAL)S(8), kSurfaceHover);

    DrawCentered(g, std::to_wstring(visibleIndex + 1), (REAL)S(15), false, selected ? kAccentHover : kText2,
        RectF(slot.X, slot.Y, (REAL)S(kNumberColumn), slot.Height));

    RectF cover = RF(ArtRect(visibleIndex));
    GraphicsPath clip;
    RoundPath(clip, cover, (REAL)S(5));
    if (Bitmap* bitmap = GetCover(game.appId)) {
        GraphicsState state = g.Save();
        g.SetClip(&clip, CombineModeIntersect);
        g.DrawImage(bitmap, cover);
        g.Restore(state);
    } else {
        SolidBrush brush(kCover);
        g.FillPath(&brush, &clip);
        DrawCentered(g, game.name.substr(0, 1), (REAL)S(18), true, kAccentHover, cover);
    }

    if (hover && !g_drag.active) {
        SolidBrush dim(Color(110, 0, 0, 0));
        g.FillPath(&dim, &clip);
        REAL diameter = (REAL)S(30), cx = cover.X + cover.Width / 2, cy = cover.Y + cover.Height / 2;
        bool hot = g_hover.cover;
        SolidBrush disc(hot ? kAccent : Color(230, 42, 42, 42));
        g.FillEllipse(&disc, cx - diameter / 2, cy - diameter / 2, diameter, diameter);
        DrawPlayTriangle(g, cx, cy, (REAL)S(12), hot ? kOnAccent : kText);
    }

    REAL tx = cover.GetRight() + S(16), tw = slot.GetRight() - tx - S(16);
    DrawLabel(g, game.name, (REAL)S(15), true, selected ? kAccentHover : kText, RectF(tx, slot.Y + S(10), tw, (REAL)S(22)));
    DrawLabel(g, PlayInfo(game), (REAL)S(13), false, kText2, RectF(tx, slot.Y + S(32), tw, (REAL)S(18)));
}

// A card in the grid, modeled on Spotify's: art with a round play button, then title and details.
void DrawCard(Graphics& g, int visibleIndex) {
    const Game& game = g_games[g_visible[visibleIndex]];
    bool selected = g_visible[visibleIndex] == g_sel;
    bool hover = visibleIndex == g_hover.row && !g_drag.active;

    RectF cell = RF(ItemCell(visibleIndex));
    if (selected) FillRound(g, cell, (REAL)S(8), kSurface);
    else if (hover) FillRound(g, cell, (REAL)S(8), kSurfaceHover);

    RectF art = RF(ArtRect(visibleIndex));
    GraphicsPath clip;
    RoundPath(clip, art, (REAL)S(6));
    GraphicsState state = g.Save();
    g.SetClip(&clip, CombineModeIntersect);
    if (Bitmap* portrait = GetPortrait(game.appId)) {
        g.DrawImage(portrait, art);
    } else if (Bitmap* header = GetCover(game.appId)) {
        // No portrait art: fill the card with the middle of the wide header.
        REAL sourceH = (REAL)header->GetHeight(), sourceW = sourceH * art.Width / art.Height;
        g.DrawImage(header, art, ((REAL)header->GetWidth() - sourceW) / 2, 0, sourceW, sourceH, UnitPixel);
    } else {
        SolidBrush brush(kCover);
        g.FillPath(&brush, &clip);
        DrawCentered(g, game.name.substr(0, 1), (REAL)S(40), true, kAccentHover, art);
    }
    g.Restore(state);

    if (hover || selected) {
        RectF play = RF(PlayRect(visibleIndex));
        bool hot = hover && g_hover.cover;
        REAL grow = hot ? (REAL)S(3) : 0.0f;
        RectF disc(play.X - grow, play.Y - grow, play.Width + 2 * grow, play.Height + 2 * grow);
        SolidBrush shadow(Color(80, 0, 0, 0));
        g.FillEllipse(&shadow, disc.X, disc.Y + 2, disc.Width, disc.Height);
        SolidBrush brush(hot ? kAccentHover : kAccent);
        g.FillEllipse(&brush, disc);
        DrawPlayTriangle(g, disc.X + disc.Width / 2, disc.Y + disc.Height / 2, (REAL)S(16), kOnAccent);
    }

    REAL textTop = art.GetBottom() + S(10);
    DrawLabel(g, game.name, (REAL)S(15), true, selected ? kAccentHover : kText, RectF(art.X, textTop, art.Width, (REAL)S(20)));
    DrawLabel(g, PlayInfo(game), (REAL)S(13), false, kText2,
        RectF(art.X, textTop + S(22), art.Width, (REAL)S(34)), StringAlignmentNear, StringAlignmentNear, true);
}

// ---------------------------------------------------------------- frame

void DrawChips(Graphics& g) {
    for (int i = 0; i < (int)g_L.chips.size(); ++i) {
        RectF r = RF(g_L.chips[i]);
        bool dragging = g_drag.active;
        bool target = dragging && i >= 1 && i == g_drag.dropChip;
        bool active = !dragging && i == g_active;
        bool hovered = !dragging && i == g_hover.chip;
        if (i == g_renameChip) {  // the edit control sits on top of this chip
            FillRound(g, r, r.Height / 2, kField);
            StrokePill(g, r, kText, 2.0f);
            continue;
        }
        Color fill = target || active ? kAccent : (hovered ? kChipHover : kSurface);
        FillRound(g, r, r.Height / 2, fill);
        if (dragging && i >= 1 && !target) StrokePill(g, r, kAccent, 1.5f, true);
        Color text = target || active ? kOnAccent : (dragging && i == 0 ? kOutline : kText);
        DrawCentered(g, g_categories[i], (REAL)S(13), true, text, r);
    }
}

void DrawList(Graphics& g, int W) {
    RectF listRect = RF(g_L.list);
    g.SetClip(listRect);
    if (g_visible.empty()) {
        DrawLabel(g, L"Hier ist noch nichts", (REAL)S(18), true, kText,
            RectF(listRect.X, listRect.Y + S(40), listRect.Width, (REAL)S(26)), StringAlignmentCenter);
        DrawLabel(g, L"Wähle eine andere Kategorie oder scanne Steam neu.", (REAL)S(13), false, kText2,
            RectF(listRect.X, listRect.Y + S(70), listRect.Width, (REAL)S(20)), StringAlignmentCenter);
    } else {
        for (int i = (g_scroll / g_L.rowH) * g_L.cols; i < (int)g_visible.size() && ItemTop(i) < g_L.list.bottom; ++i) {
            if (g_L.grid) DrawCard(g, i);
            else DrawRow(g, i, W);
        }

        int maxScroll = MaxScroll();
        if (maxScroll > 0) {
            REAL view = listRect.Height, content = (REAL)ContentHeight();
            REAL thumbH = std::max((REAL)S(32), view * view / content);
            REAL thumbY = listRect.Y + (view - thumbH) * g_scroll / maxScroll;
            FillRound(g, RectF((REAL)(W - S(8)), thumbY, (REAL)S(4), thumbH), (REAL)S(2), Color(90, 255, 255, 255));
        }
    }
    g.ResetClip();
}

void DrawIcon(Graphics& g, Icon icon, REAL cx, REAL cy, REAL size, const Color& color) {
    Pen pen(color, (REAL)S(1.8f));
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    REAL h = size / 2;
    switch (icon) {
    case Icon::Refresh: {
        // An open circle with an arrow head where it ends.
        REAL r = h * 0.78f, theta = 4.18879f;  // 240 degrees
        g.DrawArc(&pen, cx - r, cy - r, 2 * r, 2 * r, -60, 300);
        PointF end(cx + r * std::cos(theta), cy + r * std::sin(theta));
        PointF tangent(-std::sin(theta), std::cos(theta)), normal(-tangent.Y, tangent.X);
        REAL s = size * 0.3f;
        PointF head[3] = { PointF(end.X + tangent.X * s * 0.5f, end.Y + tangent.Y * s * 0.5f),
            PointF(end.X - tangent.X * s * 0.5f + normal.X * s * 0.8f, end.Y - tangent.Y * s * 0.5f + normal.Y * s * 0.8f),
            PointF(end.X - tangent.X * s * 0.5f - normal.X * s * 0.8f, end.Y - tangent.Y * s * 0.5f - normal.Y * s * 0.8f) };
        SolidBrush brush(color);
        g.FillPolygon(&brush, head, 3);
        break;
    }
    case Icon::Download: {
        g.DrawLine(&pen, cx, cy - h * 0.85f, cx, cy + h * 0.25f);
        PointF arrow[3] = { PointF(cx - h * 0.5f, cy - h * 0.25f), PointF(cx, cy + h * 0.3f), PointF(cx + h * 0.5f, cy - h * 0.25f) };
        g.DrawLines(&pen, arrow, 3);
        PointF tray[4] = { PointF(cx - h * 0.8f, cy + h * 0.35f), PointF(cx - h * 0.8f, cy + h * 0.85f),
            PointF(cx + h * 0.8f, cy + h * 0.85f), PointF(cx + h * 0.8f, cy + h * 0.35f) };
        g.DrawLines(&pen, tray, 4);
        break;
    }
    case Icon::None:
        break;
    }
}

// Icon and label as one group, centered in a button.
void DrawButtonContent(Graphics& g, RectF r, Icon icon, const std::wstring& text, const Color& color) {
    FontFamily family(L"Arial");
    Font font(&family, (REAL)S(13), FontStyleBold, UnitPixel);
    RectF box;
    g.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &box);
    REAL iconW = icon == Icon::None ? 0.0f : (REAL)S(16), gap = icon == Icon::None ? 0.0f : (REAL)S(8);
    REAL textW = box.Width - S(8);
    REAL startX = r.X + (r.Width - (iconW + gap + textW)) / 2;
    DrawIcon(g, icon, startX + iconW / 2, r.Y + r.Height / 2, iconW, color);
    DrawLabel(g, text, (REAL)S(13), true, color,
        RectF(startX + iconW + gap, r.Y, textW + S(12), r.Height), StringAlignmentNear, StringAlignmentCenter);
}

// Category field in the style of Spotify's search box: dark pill, icon, white ring on focus.
void DrawCategoryField(Graphics& g) {
    RectF pill = RF(g_L.pill);
    bool focus = GetFocus() == g_edit;
    FillRound(g, pill, pill.Height / 2, kField);
    if (focus) StrokePill(g, pill, kText, 2.0f);
}

void DrawBar(Graphics& g, int W, int H) {
    SolidBrush bar(kBar), line(kLine);
    g.FillRectangle(&bar, 0, (INT)g_L.bar.top, W, H - (INT)g_L.bar.top);
    g.FillRectangle(&line, 0, (INT)g_L.bar.top, W, 1);
    DrawCategoryField(g);

    for (int i = 0; i < BTN_COUNT; ++i) {
        RectF r = RF(g_L.btn[i]);
        bool hover = i == g_hover.button;
        if (i == BTN_RANDOM) {
            FillRound(g, r, r.Height / 2, hover ? kAccentHover : kAccent);
            DrawCentered(g, kButtonLabels[i], (REAL)S(14), true, kOnAccent, r);
            continue;
        }
        bool busy = g_badge[i].mode != Badge::Off;
        FillRound(g, r, r.Height / 2, hover && !busy ? kChipHover : kSurface);
        if (busy) DrawBadge(g, r, g_badge[i]);
        else DrawButtonContent(g, r, kButtonIcons[i], kButtonLabels[i], kText);
    }
}

void DrawDragGhost(Graphics& g) {
    if (!g_drag.active || g_drag.row < 0 || g_drag.row >= (int)g_visible.size()) return;
    RectF ghost((REAL)g_drag.pos.x + S(14), (REAL)g_drag.pos.y + S(10), (REAL)S(220), (REAL)S(40));
    FillRound(g, RectF(ghost.X + 1, ghost.Y + 3, ghost.Width, ghost.Height), (REAL)S(20), Color(70, 0, 0, 0));
    FillRound(g, ghost, (REAL)S(20), kAccent);
    DrawLabel(g, g_games[g_visible[g_drag.row]].name, (REAL)S(14), true, kOnAccent,
        RectF(ghost.X + S(18), ghost.Y, ghost.Width - S(36), ghost.Height), StringAlignmentNear, StringAlignmentCenter);
}

void DrawMenu(Graphics& g) {
    if (!g_menu.open) return;
    static const wchar_t* const labels[Menu::ItemCount] = { L"Umbenennen", L"Löschen" };
    RectF r = RF(g_menu.rect);
    FillRound(g, RectF(r.X, r.Y + 4, r.Width, r.Height), (REAL)S(10), Color(90, 0, 0, 0));
    FillRound(g, r, (REAL)S(10), kChipHover);
    FillRound(g, RectF(r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2), (REAL)S(9), Color(255, 36, 36, 36));
    for (int i = 0; i < Menu::ItemCount; ++i) {
        RectF item = RF(g_menu.items[i]);
        bool hover = i == g_hover.menuItem;
        if (hover) FillRound(g, item, (REAL)S(6), kChipHover);
        Color text = i == Menu::Delete ? (hover ? kDangerHover : kDanger) : kText;
        DrawLabel(g, labels[i], (REAL)S(14), false, text,
            RectF(item.X + S(12), item.Y, item.Width - S(24), item.Height), StringAlignmentNear, StringAlignmentCenter);
    }
}

// Fades and slides in, holds, then fades out.
void DrawToast(Graphics& g) {
    if (!g_toast.active) return;
    constexpr double kFade = 200.0;
    double elapsed = (double)(GetTickCount64() - g_toast.since);
    double in = std::min(1.0, elapsed / kFade);
    double out = std::min(1.0, ((double)g_toast.duration - elapsed) / kFade);
    REAL alpha = (REAL)std::clamp(std::min(in, out), 0.0, 1.0);
    REAL slide = (REAL)((1.0 - (1.0 - std::pow(1.0 - in, 3.0))) * S(12));

    RectF r = RF(g_toast.rect);
    r.Y += slide;
    FillRound(g, RectF(r.X, r.Y + 3, r.Width, r.Height), r.Height / 2, Color((BYTE)(70 * alpha), 0, 0, 0));
    FillRound(g, r, r.Height / 2, WithAlpha(kChipHover, alpha));

    DrawLabel(g, g_toast.text, (REAL)S(14), false, WithAlpha(kText, alpha),
        RectF(r.X + S(22), r.Y, r.Width - S(44), r.Height), StringAlignmentNear, StringAlignmentCenter);
}

void DrawUi(Graphics& g, int W, int H) {
    SolidBrush bg(kBg);
    g.FillRectangle(&bg, 0, 0, W, H);

    RectF header((REAL)S(kMargin), 0, (REAL)(W - 2 * S(kMargin)), 0);
    DrawLabel(g, g_steamName.empty() ? L"Deine Steam-Bibliothek" : L"Bibliothek von " + g_steamName, (REAL)S(13), true, kText2,
        RectF(header.X, (REAL)S(16), header.Width, (REAL)S(18)));
    DrawLabel(g, std::to_wstring(g_games.size()) + (g_games.size() == 1 ? L" Spiel" : L" Spiele"), (REAL)S(38), true, kText,
        RectF(header.X, (REAL)S(32), header.Width, (REAL)S(48)));
    std::wstring hint = L"Zieh ein Spiel auf eine Kategorie, um es zuzuordnen";
    if (g_drag.active) hint = L"Lass das Spiel auf einer Kategorie los, um es zuzuordnen";
    else if (g_categories.size() <= 1) hint = L"Lege unten eine Kategorie an und zieh Spiele hinein";
    DrawLabel(g, hint, (REAL)S(13), false, kText2,
        RectF((REAL)S(kMargin), (REAL)S(82), (REAL)(W - 2 * S(kMargin)), (REAL)S(20)));

    DrawChips(g);
    DrawList(g, W);
    DrawBar(g, W, H);
    DrawToast(g);
    DrawMenu(g);
    DrawDragGhost(g);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W > 0 && H > 0) {
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
        HGDIOBJ old = SelectObject(mem, bmp);
        {
            Graphics g(mem);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            DrawUi(g, W, H);
        }
        BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
    }
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------- hit testing

int HitRow(POINT p) {
    if (!In(g_L.list, p)) return -1;
    int row = (p.y - g_L.list.top + g_scroll) / g_L.rowH, col = 0;
    if (g_L.grid) {
        int x = p.x - (S(kMargin) - g_L.cardPad);
        if (x < 0) return -1;
        col = x / (g_L.cellW + g_L.cardGap);
        if (col >= g_L.cols) return -1;
    }
    int index = row * g_L.cols + col;
    if (row < 0 || index >= (int)g_visible.size()) return -1;
    return In(ItemCell(index), p) ? index : -1;  // the gaps between cards hit nothing
}

int HitButton(POINT p) {
    for (int i = 0; i < BTN_COUNT; ++i)
        if (In(g_L.btn[i], p)) return i;
    return -1;
}

int HitChip(POINT p) {
    for (int i = 0; i < (int)g_L.chips.size(); ++i)
        if (In(g_L.chips[i], p)) return i;
    return -1;
}

void UpdateHover(HWND hwnd, POINT p) {
    Hover next;
    next.row = HitRow(p);
    next.button = HitButton(p);
    next.chip = HitChip(p);
    next.cover = InPlayZone(next.row, p);
    if (g_menu.open) {
        // The menu sits on top, so it hides whatever is underneath.
        next = Hover{};
        for (int i = 0; i < Menu::ItemCount; ++i)
            if (In(g_menu.items[i], p)) next.menuItem = i;
    }
    if (next.row != g_hover.row || next.button != g_hover.button || next.chip != g_hover.chip ||
        next.cover != g_hover.cover || next.menuItem != g_hover.menuItem) {
        g_hover = next;
        Redraw(hwnd);
    }
}

void EnsureVisible(int visibleIndex) {
    int top = (visibleIndex / g_L.cols) * g_L.rowH, view = g_L.list.bottom - g_L.list.top;
    if (top < g_scroll) g_scroll = top;
    else if (top + g_L.rowH > g_scroll + view) g_scroll = top + g_L.rowH - view;
    ClampScroll();
}

// ============================================================================ actions

void LayoutToast(HWND hwnd) {
    Graphics g(hwnd);
    FontFamily family(L"Arial");
    Font regular(&family, (REAL)S(14), FontStyleRegular, UnitPixel);
    RectF textBox;
    g.MeasureString(g_toast.text.c_str(), -1, &regular, PointF(0, 0), &textBox);

    RECT client;
    GetClientRect(hwnd, &client);
    int pad = S(22), h = S(46);
    int w = std::min(pad * 2 + (int)std::ceil(textBox.Width) + S(2), (int)client.right - S(32));
    int x = (client.right - w) / 2, y = g_L.bar.top - S(14) - h;
    g_toast.rect = { x, y, x + w, y + h };
}

void ShowToast(HWND hwnd, const std::wstring& text) {
    g_toast.active = true;
    g_toast.text = text;
    g_toast.since = GetTickCount64();
    g_toast.duration = 3500;
    LayoutToast(hwnd);
    SetTimer(hwnd, TIMER_ANIMATION, 16, nullptr);
    Redraw(hwnd);
}

void CloseMenu(HWND hwnd) {
    if (!g_menu.open) return;
    g_menu = Menu{};
    g_hover.menuItem = -1;
    Redraw(hwnd);
}

void OpenMenu(HWND hwnd, int chip, POINT at) {
    RECT client;
    GetClientRect(hwnd, &client);
    int w = S(kMenuWidth), pad = S(kMenuPad), item = S(kMenuItemHeight);
    int h = Menu::ItemCount * item + 2 * pad;
    int x = std::min((int)at.x, (int)client.right - w - S(8)), y = std::min((int)at.y, (int)client.bottom - h - S(8));
    g_menu.open = true;
    g_menu.chip = chip;
    g_menu.rect = { x, y, x + w, y + h };
    for (int i = 0; i < Menu::ItemCount; ++i)
        g_menu.items[i] = { x + pad, y + pad + i * item, x + w - pad, y + pad + (i + 1) * item };
    UpdateHover(hwnd, at);
    Redraw(hwnd);
}

// Leaves rename mode. Focus only goes back to the window when the edit did not lose it to another control.
void EndRename(HWND hwnd, bool restoreFocus) {
    if (g_renameChip < 0) return;
    g_renameChip = -1;
    ShowWindow(g_renameEdit, SW_HIDE);
    if (restoreFocus) SetFocus(hwnd);
    Relayout(hwnd);
}

void StartRename(HWND hwnd, int chip) {
    g_renameChip = chip;
    SetWindowTextW(g_renameEdit, g_categories[chip].c_str());
    Relayout(hwnd);
    SetFocus(g_renameEdit);
    SendMessageW(g_renameEdit, EM_SETSEL, 0, -1);
}

void CommitRename(HWND hwnd) {
    if (g_renameChip < 0) return;
    int length = GetWindowTextLengthW(g_renameEdit);
    std::wstring name(length + 1, L'\0');
    GetWindowTextW(g_renameEdit, name.data(), length + 1);
    name.resize(length);
    const std::wstring old = g_categories[g_renameChip];

    if (name.empty() || name == old) { EndRename(hwnd, true); return; }
    if (name.find_first_of(L"\t\r\n") != std::wstring::npos) {
        ShowToast(hwnd, L"Der Name darf keine Tabs oder Zeilenumbrüche enthalten");
        return;
    }
    if (IsReservedCategory(name) || std::find(g_categories.begin(), g_categories.end(), name) != g_categories.end()) {
        ShowToast(hwnd, L"Diese Kategorie gibt es schon");
        return;
    }
    g_categories[g_renameChip] = name;
    for (auto& game : g_games)
        if (game.category == old) game.category = name;
    SaveSettings();
    EndRename(hwnd, true);
}

void AddCategory(HWND hwnd) {
    int length = GetWindowTextLengthW(g_edit);
    if (length <= 0) return;
    std::wstring name(length + 1, L'\0');
    GetWindowTextW(g_edit, name.data(), length + 1);
    name.resize(length);
    if (name.find_first_of(L"\t\r\n") != std::wstring::npos) {
        ShowToast(hwnd, L"Der Name darf keine Tabs oder Zeilenumbrüche enthalten");
        return;
    }
    if (IsReservedCategory(name) || std::find(g_categories.begin(), g_categories.end(), name) != g_categories.end()) {
        ShowToast(hwnd, L"Diese Kategorie gibt es schon");
        return;
    }
    g_categories.push_back(name);
    SetWindowTextW(g_edit, L"");
    g_active = (int)g_categories.size() - 1;
    g_scroll = 0;
    RefreshGames();
    Relayout(hwnd);
    SaveSettings();
}

void DeleteCategory(HWND hwnd, int index) {
    const std::wstring name = g_categories[index];
    for (auto& game : g_games)
        if (game.category == name) game.category = kUnsorted;
    g_categories.erase(g_categories.begin() + index);
    g_active = 0;
    g_scroll = 0;
    RefreshGames();
    Relayout(hwnd);
    SaveSettings();
}

void LaunchGame(HWND hwnd, const Game& game) {
    std::wstring uri = L"steam://rungameid/" + game.appId;
    HINSTANCE result = ShellExecuteW(hwnd, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32)
        ShowToast(hwnd, L"Steam konnte das Spiel nicht starten");
    else
        DestroyWindow(hwnd);
}

// Keeps the animation timer running while any button shows a badge.
void ShowBadge(HWND hwnd, int button, Badge::Mode mode, const std::wstring& text, ULONGLONG hideAfter = 0) {
    g_badge[button] = { mode, text, GetTickCount64(), hideAfter };
    SetTimer(hwnd, TIMER_ANIMATION, 16, nullptr);
    InvalidateRect(hwnd, &g_L.btn[button], FALSE);
}

// Selects a random game of the current category, different from the current selection. Starting is up to the user.
void SelectRandomGame(HWND hwnd) {
    if (g_visible.empty()) {
        ShowToast(hwnd, L"In dieser Kategorie sind keine Spiele");
        return;
    }
    std::vector<int> choices;
    for (int i = 0; i < (int)g_visible.size(); ++i)
        if (g_visible[i] != g_sel) choices.push_back(i);
    if (choices.empty()) choices.push_back(0);

    std::random_device rd;
    std::mt19937 rng(rd());
    int index = choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(rng)];
    g_sel = g_visible[index];
    EnsureVisible(index);
    Redraw(hwnd);
}

void TickBadges(HWND hwnd) {
    ULONGLONG now = GetTickCount64();
    bool anyActive = false;
    for (int i = 0; i < BTN_COUNT; ++i) {
        Badge& badge = g_badge[i];
        if (badge.mode == Badge::Off) continue;
        if (badge.mode == Badge::Spinner) {
            // Keep spinners visible long enough to notice, even when the work is already done.
            if (i == BTN_SCAN && g_scanReady && now - badge.since >= 700) {
                badge = { Badge::Check, L"Fertig", now, 1800 };
            } else if (i == BTN_IMPORT && g_importReady && now - badge.since >= 700) {
                badge = g_importResult;
                badge.since = now;
                g_importReady = false;
            }
        } else if (now - badge.since >= badge.hideAfter) {
            badge.mode = Badge::Off;
        }
        anyActive |= badge.mode != Badge::Off;
        InvalidateRect(hwnd, &g_L.btn[i], FALSE);
    }
    if (g_toast.active) {
        RECT area = g_toast.rect;
        InflateRect(&area, S(24), S(24));
        InvalidateRect(hwnd, &area, FALSE);
        if (now - g_toast.since >= g_toast.duration) g_toast.active = false;
        anyActive |= g_toast.active;
    }
    if (!anyActive) KillTimer(hwnd, TIMER_ANIMATION);
}

void ImportFromSteam(HWND hwnd) {
    if (g_badge[BTN_IMPORT].mode == Badge::Spinner) return;
    ImportResult result = ImportSteamCategories();
    if (result.games > 0) {
        SaveSettings();
        RefreshGames();
        Relayout(hwnd);
        g_importResult = { Badge::Check,
            std::to_wstring(result.categories) + (result.categories == 1 ? L" Kategorie" : L" Kategorien"), 0, 2200 };
    } else {
        g_importResult = { Badge::Text, L"Nichts Neues", 0, 2200 };
    }
    g_importReady = true;
    ShowBadge(hwnd, BTN_IMPORT, Badge::Spinner, L"Lädt…");
}

void Rescan(HWND hwnd) {
    if (g_badge[BTN_SCAN].mode == Badge::Spinner) return;
    g_scanReady = false;
    ShowBadge(hwnd, BTN_SCAN, Badge::Spinner, L"Scannt…");
    std::thread([hwnd] {
        auto* found = new std::map<std::wstring, Game>(ScanSteamGames());
        PostMessageW(hwnd, WM_SCANDONE, 0, (LPARAM)found);
    }).detach();
}

void OnScanDone(HWND hwnd, std::map<std::wstring, Game>* found) {
    MergeGames(*found);
    delete found;
    if (g_active >= (int)g_categories.size()) g_active = 0;
    g_sel = -1;
    RefreshGames();
    Relayout(hwnd);
    QueueBackgroundWork();
    g_scanReady = true;
}

void OnClassified(HWND hwnd, const std::wstring& appId, bool isGame) {
    g_isGame[appId] = isGame;
    SaveTypes();
    if (isGame) return;
    g_games.erase(std::remove_if(g_games.begin(), g_games.end(),
        [&](const Game& game) { return game.appId == appId; }), g_games.end());
    g_sel = -1;
    SaveSettings();
    RefreshGames();
    Relayout(hwnd);
}

void EndDrag(HWND hwnd) {
    if (GetCapture() == hwnd) ReleaseCapture();
    g_drag = DragState{};
    Redraw(hwnd);
}

void DropOnCategory(HWND hwnd) {
    if (g_drag.active && g_drag.dropChip >= 1 && g_drag.row >= 0 && g_drag.row < (int)g_visible.size()) {
        g_games[g_visible[g_drag.row]].category = g_categories[g_drag.dropChip];
        SaveSettings();
        EndDrag(hwnd);
        RefreshGames();
    } else {
        EndDrag(hwnd);
    }
}

// F11: borderless fullscreen on the current monitor, and back to the previous window position.
void ToggleFullscreen(HWND hwnd) {
    static WINDOWPLACEMENT previous{ sizeof(WINDOWPLACEMENT) };
    if (!g_fullscreen) {
        MONITORINFO monitor{ sizeof(monitor) };
        if (!GetWindowPlacement(hwnd, &previous) || !GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            return;
        SetWindowLongPtrW(hwnd, GWL_STYLE, (WINDOW_STYLE & ~WS_OVERLAPPEDWINDOW) | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = true;
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, WINDOW_STYLE | WS_VISIBLE);
        SetWindowPlacement(hwnd, &previous);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
}

LRESULT CALLBACK RenameEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_F11) { ToggleFullscreen(GetParent(hwnd)); return 0; }
    if (msg == WM_CHAR) {
        if (wp == VK_RETURN) { CommitRename(GetParent(hwnd)); return 0; }
        if (wp == VK_ESCAPE) { EndRename(GetParent(hwnd), true); return 0; }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_F11) { ToggleFullscreen(GetParent(hwnd)); return 0; }
    if (msg == WM_CHAR) {
        if (wp == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_CREATE_CATEGORY, BN_CLICKED), 0);
            return 0;
        }
        if (wp == VK_ESCAPE) { SetFocus(GetParent(hwnd)); return 0; }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

POINT ClientPoint(LPARAM lp) { return { (short)LOWORD(lp), (short)HIWORD(lp) }; }

void OnMouseDown(HWND hwnd, UINT msg, LPARAM lp) {
    SetFocus(hwnd);
    POINT p = ClientPoint(lp);
    if (g_menu.open) {
        // The menu swallows the click: pick an item, or just close it.
        int target = g_menu.chip, picked = g_hover.menuItem;
        CloseMenu(hwnd);
        if (picked == Menu::Rename) StartRename(hwnd, target);
        else if (picked == Menu::Delete) DeleteCategory(hwnd, target);
        return;
    }
    if (In(g_L.pill, p)) {
        SetFocus(g_edit);  // the whole field is clickable; Enter creates the category
        return;
    }
    int chip = HitChip(p), button = HitButton(p), row = HitRow(p);
    if (chip >= 0) {
        g_active = chip;
        g_scroll = 0;
        RefreshGames();
        Redraw(hwnd);
    } else if (button >= 0) {
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_ACTION + button, BN_CLICKED), 0);
    } else if (row >= 0) {
        g_sel = g_visible[row];
        Redraw(hwnd);
        if (msg == WM_LBUTTONDBLCLK || InPlayZone(row, p)) {
            LaunchGame(hwnd, g_games[g_sel]);
        } else {
            g_drag.row = row;
            g_drag.start = p;
            SetCapture(hwnd);
        }
    }
}

void OnMouseMove(HWND hwnd, WPARAM wp, LPARAM lp) {
    if (!g_tracking) {
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&track);
        g_tracking = true;
    }
    POINT p = ClientPoint(lp);
    if (g_drag.row < 0 || !(wp & MK_LBUTTON)) { UpdateHover(hwnd, p); return; }

    if (!g_drag.active && (std::abs(p.x - g_drag.start.x) > S(6) || std::abs(p.y - g_drag.start.y) > S(6)))
        g_drag.active = true;
    if (g_drag.active) {
        g_drag.pos = p;
        int chip = HitChip(p);
        g_drag.dropChip = chip >= 1 ? chip : -1;
        Redraw(hwnd);
    }
}

void OnKeyDown(HWND hwnd, WPARAM key) {
    if (key == VK_F11) {
        ToggleFullscreen(hwnd);
        return;
    }
    if (key == VK_ESCAPE) {
        if (g_menu.open) CloseMenu(hwnd);
        else if (g_fullscreen) ToggleFullscreen(hwnd);
        return;
    }
    bool vertical = key == VK_UP || key == VK_DOWN, horizontal = g_L.grid && (key == VK_LEFT || key == VK_RIGHT);
    if ((!vertical && !horizontal) || g_visible.empty()) return;

    // Up and down move by a row (several cards in the grid), left and right by one card.
    int step = vertical ? g_L.cols : 1;
    int current = -1;
    for (int i = 0; i < (int)g_visible.size(); ++i)
        if (g_visible[i] == g_sel) current = i;
    int direction = (key == VK_DOWN || key == VK_RIGHT) ? 1 : -1;
    int next = std::clamp(current < 0 ? 0 : current + direction * step, 0, (int)g_visible.size() - 1);
    g_sel = g_visible[next];
    EnsureVisible(next);
    Redraw(hwnd);
}

void OnCreate(HWND hwnd) {
    BOOL dark = TRUE;
    COLORREF caption = RGB(0, 0, 0), captionText = RGB(179, 179, 179);
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &captionText, sizeof(captionText));

    g_editFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    g_editBrush = CreateSolidBrush(RGB(31, 31, 31));
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 100, 20, hwnd, (HMENU)(INT_PTR)ID_EDIT, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Neue Kategorie");  // stays visible while focused, like a search box
    SetWindowSubclass(g_edit, EditProc, 1, 0);

    g_renameEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
        0, 0, 100, 20, hwnd, (HMENU)(INT_PTR)ID_RENAME_EDIT, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_renameEdit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    SetWindowSubclass(g_renameEdit, RenameEditProc, 2, 0);
    RefreshGames();
}

void OnDestroy() {
    SaveSettings();
    if (g_editFont) DeleteObject(g_editFont);
    if (g_editBrush) DeleteObject(g_editBrush);
    g_headers.Clear();
    g_portraits.Clear();
    PostQuitMessage(0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_SIZE:
        if (g_edit) Relayout(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        RECT r{ 0, 0, S(kMinClientWidth), S(kMinClientHeight) };
        AdjustWindowRectEx(&r, WINDOW_STYLE, FALSE, 0);
        ((MINMAXINFO*)lp)->ptMinTrackSize = { r.right - r.left, r.bottom - r.top };
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        Paint(hwnd);
        return 0;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGB(255, 255, 255));
        SetBkColor((HDC)wp, RGB(31, 31, 31));
        return (LRESULT)g_editBrush;

    case WM_MOUSEMOVE:
        OnMouseMove(hwnd, wp, lp);
        return 0;

    case WM_MOUSELEAVE:
        g_tracking = false;
        g_hover = Hover{};
        Redraw(hwnd);
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && (g_drag.active || g_hover.Any())) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_MOUSEWHEEL: {
        CloseMenu(hwnd);
        g_scroll -= GET_WHEEL_DELTA_WPARAM(wp) * g_L.wheelStep / WHEEL_DELTA;
        ClampScroll();
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        UpdateHover(hwnd, p);
        Redraw(hwnd);
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
        OnMouseDown(hwnd, msg, lp);
        return 0;

    case WM_LBUTTONUP:
        DropOnCategory(hwnd);
        return 0;

    case WM_RBUTTONUP: {
        POINT p = ClientPoint(lp);
        int chip = HitChip(p);
        CloseMenu(hwnd);
        if (chip >= 1) OpenMenu(hwnd, chip, p);  // the first chip, "Alle Spiele", is built in
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_drag.row >= 0) { g_drag = DragState{}; Redraw(hwnd); }
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(hwnd, wp);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_CREATE_CATEGORY) {
            AddCategory(hwnd);
            return 0;
        }
        if (id == ID_EDIT) {
            WORD code = HIWORD(wp);
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS || code == EN_CHANGE) InvalidateRect(hwnd, &g_L.pill, FALSE);
            return 0;
        }
        if (id == ID_RENAME_EDIT) {
            if (HIWORD(wp) == EN_KILLFOCUS) EndRename(hwnd, false);  // clicking away cancels
            return 0;
        }
        switch (id - ID_ACTION) {
        case BTN_SCAN: Rescan(hwnd); return 0;
        case BTN_IMPORT: ImportFromSteam(hwnd); return 0;
        case BTN_RANDOM: SelectRandomGame(hwnd); return 0;
        }
        break;
    }

    case WM_TIMER:
        if (wp == TIMER_ANIMATION) TickBadges(hwnd);
        return 0;

    case WM_COVER:
        OnCoverDownloaded(hwnd, (unsigned long long)wp);
        return 0;

    case WM_SCANDONE:
        OnScanDone(hwnd, (std::map<std::wstring, Game>*)lp);
        return 0;

    case WM_CLASSIFIED:
        OnClassified(hwnd, std::to_wstring((unsigned long long)wp), lp != 0);
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    HDC screen = GetDC(nullptr);
    g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, screen);

    g_dataDir = KnownFolder(FOLDERID_RoamingAppData) + L"\\GameRandomizer";
    g_coverDir = g_dataDir + L"\\covers";
    CreateDirectoryW(g_dataDir.c_str(), nullptr);
    CreateDirectoryW(g_coverDir.c_str(), nullptr);
    LoadSettings();
    LoadTypes();
    g_steamName = SteamPersonaName();
    MergeGames(ScanSteamGames());

    const wchar_t className[] = L"SteamGameRandomizerWindow";
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);

    RECT frame{ 0, 0, S(kStartClientWidth), S(kStartClientHeight) };
    AdjustWindowRectEx(&frame, WINDOW_STYLE, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, className, L"Steam Game Randomizer", WINDOW_STYLE,
        CW_USEDEFAULT, CW_USEDEFAULT, frame.right - frame.left, frame.bottom - frame.top,
        nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        GdiplusShutdown(gdiToken);
        CoUninitialize();
        return 1;
    }
    g_mainWnd = hwnd;
    Relayout(hwnd);
    QueueBackgroundWork();

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(gdiToken);
    CoUninitialize();
    // Skip static destructors: background threads may still be inside a request.
    ExitProcess((UINT)msg.wParam);
}
