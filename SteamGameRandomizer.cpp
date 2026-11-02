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
    std::vector<std::wstring> categories;  // a game can be in any number of categories
    int playtimeMinutes = 0;
    long long lastPlayed = 0;  // unix time, 0 when never played
    long long sizeBytes = 0;   // install size from the Steam manifest, 0 when unknown

    bool InCategory(const std::wstring& category) const {
        return std::find(categories.begin(), categories.end(), category) != categories.end();
    }
    // Returns false when the game already was in the category.
    bool AddCategory(const std::wstring& category) {
        if (InCategory(category)) return false;
        categories.push_back(category);
        return true;
    }
    void RemoveCategory(const std::wstring& category) {
        categories.erase(std::remove(categories.begin(), categories.end(), category), categories.end());
    }
};

const std::wstring kAllGames = L"Alle Spiele";

enum class SortKey { Name, Playtime, LastPlayed, Size };
SortKey g_sortKey = SortKey::Name;
bool g_sortDescending = false;

std::vector<Game> g_games;
std::vector<std::wstring> g_categories{ kAllGames };  // sidebar rows; "Alle Spiele" is always the first
std::map<std::wstring, bool> g_isGame;   // appId -> verdict from the Steam store, cached on disk

std::wstring g_dataDir;
std::wstring g_coverDir;
std::wstring g_steamName;  // persona name of the Steam account that signed in last
std::wstring g_steamId;    // its SteamID64, which names the avatar file in Steam's cache

fs::path SettingsPath() { return fs::path(g_dataDir) / L"settings.txt"; }
fs::path TypesPath() { return fs::path(g_dataDir) / L"types.txt"; }
fs::path CoverPath(const std::wstring& appId) { return fs::path(g_coverDir) / (appId + L".jpg"); }
fs::path PortraitPath(const std::wstring& appId) { return fs::path(g_coverDir) / (appId + L"_p.jpg"); }

bool IsReservedCategory(const std::wstring& name) { return name == kAllGames; }

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
            game.sizeBytes = _wtoi64(VdfValue(vdf, L"SizeOnDisk").c_str());
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
// least one installed game are imported; their games are added to the category of the same name.
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

    ImportResult result;
    for (const auto& collection : collections) {
        const std::wstring& name = collection.name;
        if (IsReservedCategory(name) || name.find_first_of(L"\t\r\n") != std::wstring::npos) continue;
        int added = (int)std::count_if(collection.members.begin(), collection.members.end(),
            [&](const Game* game) { return !game->InCategory(name); });
        bool isNew = std::find(g_categories.begin(), g_categories.end(), name) == g_categories.end();
        if (added == 0 && !isNew) continue;
        if (isNew) {
            g_categories.push_back(name);
            ++result.categories;
        }
        for (Game* game : collection.members) game->AddCategory(name);
        result.games += added;
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
    file << "S\t" << (int)g_sortKey << '\t' << (g_sortDescending ? 1 : 0) << "\r\n";
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

// Reads the account that signed in last from loginusers.vdf: its SteamID64 (the key of its block) and persona name.
void LoadSteamUser() {
    std::wstring text = ReadTextFile(fs::path(SteamPath()) / L"config" / L"loginusers.vdf");
    size_t nameKey = std::wstring::npos;
    // Several accounts can be listed; prefer the one marked as the most recent.
    for (size_t pos = 0; (pos = text.find(L"\"MostRecent\"", pos)) != std::wstring::npos; pos += 12) {
        if (VdfValue(text, L"MostRecent", pos) != L"1") continue;
        nameKey = text.rfind(L"\"PersonaName\"", pos);
        break;
    }
    if (nameKey == std::wstring::npos) nameKey = text.find(L"\"PersonaName\"");
    if (nameKey == std::wstring::npos) return;

    g_steamName = VdfValue(text, L"PersonaName", nameKey);
    size_t idKey = text.rfind(L"\"7656", nameKey);  // SteamID64s all start with 7656
    if (idKey != std::wstring::npos) g_steamId = ReadJsonString(text, idKey + 1);
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
    std::map<std::wstring, std::vector<std::wstring>> assignments;
    for (const auto& game : g_games)
        for (const auto& category : game.categories)
            if (!game.appId.empty()) assignments[game.appId].push_back(category);

    g_games.clear();
    for (const auto& entry : detected) {
        Game game = entry.second;
        auto verdict = g_isGame.find(game.appId);
        if (verdict != g_isGame.end() && !verdict->second) continue;
        for (const auto& category : assignments[game.appId])
            if (std::find(g_categories.begin(), g_categories.end(), category) != g_categories.end())
                game.AddCategory(category);  // categories that no longer exist are dropped
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
constexpr float kPanelGap = 8, kSidebarWidth = 300, kSideHeader = 56, kSideRow = 64, kSideTile = 48;
constexpr float kMainHeader = 82, kTopBar = 60;
constexpr float kMenuWidth = 176, kMenuItemHeight = 38, kMenuPad = 6;
constexpr float kBarHeight = 76, kControlHeight = 40, kBarMargin = 20, kGap = 8;
constexpr float kRandomWidth = 190;
constexpr float kMinClientWidth = 830, kMinClientHeight = 520;
constexpr float kStartClientWidth = 1120, kStartClientHeight = 680;

constexpr int ID_RENAME_EDIT = 2002;
constexpr int ID_ACTION = 3000;  // + button index
constexpr UINT_PTR TIMER_ANIMATION = 1;
constexpr DWORD WINDOW_STYLE = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

// A main panel at least this wide switches from a list to a grid of cards.
constexpr float kGridMinWidth = 820, kCardPad = 12, kCardGap = 8, kCardTargetWidth = 200, kCardTextHeight = 58;


enum { BTN_SCAN, BTN_IMPORT, BTN_RANDOM, BTN_COUNT };
enum class Icon { None, Refresh, Download, Plus, Library, Folder, Sort };
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
    RECT btn[BTN_COUNT]{};
    RECT sidebar{}, main{}, sideList{}, create{};  // create: "+ Erstellen" in the sidebar header
    RECT topBar{};
    RECT sortBtn{};                             // "Sortieren" at the top right of the main panel
    std::vector<RECT> chips;  // one row of the sidebar per category
    int rowH = 0;  // vertical step between rows of items (list rows or card rows)
    int wheelStep = 0;
    bool grid = false;
    int cols = 1, cellW = 0, cellH = 0, cardGap = 0, cardPad = 0, artW = 0, artH = 0;
};

struct Hover {
    int row = -1, button = -1, chip = -1;
    bool cover = false;
    int menuItem = -1;
    bool sideCreate = false;
    bool sortBtn = false;
    bool Any() const { return row >= 0 || button >= 0 || chip >= 0 || menuItem >= 0 || sideCreate || sortBtn; }
};

// Context menu, opened with the right mouse button on a category row or on a game.
struct Menu {
    enum Action { Rename, Delete, RemoveFromCategory, SortByName, SortByPlaytime, SortByLastPlayed, SortBySize };
    static constexpr int kMaxItems = 4;
    bool open = false;
    int chip = -1;  // the category it belongs to
    int game = -1;  // index into g_games for a game menu
    int count = 0;
    Action actions[kMaxItems]{};
    RECT rect{};
    RECT items[kMaxItems]{};
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

Bitmap* g_avatar = nullptr;        // the Steam profile picture
Bitmap* g_avatarBaked = nullptr;   // it, round and scaled for the top bar
int g_avatarSize = 0;

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
HFONT g_editFont = nullptr;
HBRUSH g_editBrush = nullptr;
std::vector<int> g_visible;  // indices into g_games for the current category
int g_active = 0;            // selected category chip
int g_sel = -1;              // selected game (index into g_games)
int g_scroll = 0;
int g_sideScroll = 0;        // scroll position of the sidebar

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

int MaxSideScroll() {
    int content = (int)g_categories.size() * S(kSideRow), view = (int)(g_L.sideList.bottom - g_L.sideList.top);
    return std::max(0, content - view);
}

void ClampSideScroll() { g_sideScroll = std::clamp(g_sideScroll, 0, MaxSideScroll()); }

// The field that replaces a category's name while it is renamed.
RECT RenameRect(int chip) {
    const RECT& row = g_L.chips[chip];
    int left = row.left + S(8) + S(kSideTile) + S(12) - S(10), top = row.top + S(8);
    return { left, top, row.right - S(6), top + S(28) };
}

void Redraw(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void LayoutToast(HWND hwnd);

void RefreshGames() {
    const std::wstring& filter = SelectedCategory();
    g_visible.clear();
    bool selectionVisible = false;
    for (int i = 0; i < (int)g_games.size(); ++i) {
        if (filter != kAllGames && !g_games[i].InCategory(filter)) continue;
        g_visible.push_back(i);
        selectionVisible |= i == g_sel;
    }
    // Numbers sort largest first by default; ties and names fall back to the alphabet.
    std::stable_sort(g_visible.begin(), g_visible.end(), [](int a, int b) {
        const Game& x = g_games[a];
        const Game& y = g_games[b];
        long long keyX = 0, keyY = 0;
        switch (g_sortKey) {
        case SortKey::Playtime: keyX = x.playtimeMinutes; keyY = y.playtimeMinutes; break;
        case SortKey::LastPlayed: keyX = x.lastPlayed; keyY = y.lastPlayed; break;
        case SortKey::Size: keyX = x.sizeBytes; keyY = y.sizeBytes; break;
        case SortKey::Name: break;
        }
        if (keyX != keyY) return g_sortDescending ? keyX > keyY : keyX < keyY;
        int byName = _wcsicmp(x.name.c_str(), y.name.c_str());
        return g_sortKey == SortKey::Name && g_sortDescending ? byName > 0 : byName < 0;
    });
    if (!selectionVisible) g_sel = -1;
    ClampScroll();
}

void Relayout(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // Two rounded panels on a black window, like Spotify: the library on the left, the games on the right.
    int gap = S(kPanelGap), panelBottom = H - S(kBarHeight);
    g_L.bar = { 0, panelBottom, W, H };
    int top = S(kTopBar);
    g_L.topBar = { 0, 0, W, top };
    g_L.sidebar = { gap, top, gap + S(kSidebarWidth), std::max(top + S(100), panelBottom - S(4)) };
    g_L.main = { g_L.sidebar.right + gap, top, W - gap, g_L.sidebar.bottom };
    g_L.sideList = { g_L.sidebar.left, g_L.sidebar.top + S(kSideHeader), g_L.sidebar.right, g_L.sidebar.bottom - S(8) };
    int createW = S(104), createH = S(34);
    g_L.create = { g_L.sidebar.right - S(12) - createW, g_L.sidebar.top + (S(kSideHeader) - createH) / 2,
        g_L.sidebar.right - S(12), g_L.sidebar.top + (S(kSideHeader) - createH) / 2 + createH };
    int sortW = S(200), sortH = S(34);
    g_L.sortBtn = { g_L.main.right - S(kMargin) - sortW, g_L.main.top + S(24), g_L.main.right - S(kMargin),
        g_L.main.top + S(24) + sortH };
    int listTop = g_L.main.top + S(kMainHeader);
    g_L.list = { g_L.main.left, listTop, g_L.main.right, std::max(listTop, (int)g_L.main.bottom - S(8)) };

    int mainWidth = g_L.main.right - g_L.main.left;
    g_L.rowH = S(kRowHeight);
    g_L.cols = 1;
    g_L.grid = mainWidth >= S(kGridMinWidth);
    if (g_L.grid) {
        // Portrait art in a grid; the cells stretch so the columns fill the panel.
        g_L.cardPad = S(kCardPad);
        g_L.cardGap = S(kCardGap);
        int available = mainWidth - 2 * (S(kMargin) - g_L.cardPad);
        g_L.cols = std::max(1, (available + g_L.cardGap) / (S(kCardTargetWidth) + g_L.cardGap));
        g_L.cellW = (available - g_L.cardGap * (g_L.cols - 1)) / g_L.cols;
        g_L.artW = g_L.cellW - 2 * g_L.cardPad;
        g_L.artH = g_L.artW * 3 / 2;
        g_L.cellH = g_L.cardPad + g_L.artH + S(10) + S(kCardTextHeight) + g_L.cardPad;
        g_L.rowH = g_L.cellH + g_L.cardGap;
    }
    g_L.wheelStep = g_L.grid ? g_L.rowH / 3 : g_L.rowH;
    // The categories are rows of the sidebar, which scrolls on its own when there are many.
    ClampSideScroll();
    g_L.chips.clear();
    for (int i = 0; i < (int)g_categories.size(); ++i) {
        int top = g_L.sideList.top + i * S(kSideRow) - g_sideScroll;
        g_L.chips.push_back({ g_L.sidebar.left + S(8), top, g_L.sidebar.right - S(8), top + S(kSideRow) });
    }

    int ph = S(kControlHeight);
    int py = g_L.bar.top + (S(kBarHeight) - ph) / 2;
    // The buttons live in a column of at most 1300 pixels that stays centered, so they are easy to find in a big window.
    int margin = std::max(S(kBarMargin), (W - S(1300)) / 2);
    int x = margin;
    for (int i = 0; i < BTN_RANDOM; ++i) {
        g_L.btn[i] = { x, py, x + S(kButtonWidths[i]), py + ph };
        x += S(kButtonWidths[i]) + S(kGap);
    }
    g_L.btn[BTN_RANDOM] = { W - margin - S(kRandomWidth), py, W - margin, py + ph };

    if (g_renameEdit && g_renameChip >= 0 && g_renameChip < (int)g_L.chips.size()) {
        RECT field = RenameRect(g_renameChip);
        int eh = S(18);
        SetWindowPos(g_renameEdit, HWND_TOP, field.left + S(10), field.top + (field.bottom - field.top - eh) / 2,
            field.right - field.left - S(20), eh, SWP_SHOWWINDOW);
    }
    ClampScroll();
    ClampSideScroll();
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

// Art scaled once to the size it is drawn at, with rounded corners, so painting only has to blit it.
// Scaling the full-size images on every repaint made hovering over many cards feel laggy.
struct BakedArt {
    int w = 0, h = 0;
    Bitmap* bitmap = nullptr;
};
using BakedCache = std::map<unsigned long long, BakedArt>;
BakedCache g_bakedRows, g_bakedCards, g_bakedTilesSmall, g_bakedTilesFull;

void ForgetBaked(unsigned long long id) {
    for (BakedCache* cache : { &g_bakedRows, &g_bakedCards, &g_bakedTilesSmall, &g_bakedTilesFull }) {
        auto it = cache->find(id);
        if (it == cache->end()) continue;
        delete it->second.bitmap;
        cache->erase(it);
    }
}

void ClearBaked() {
    for (BakedCache* cache : { &g_bakedRows, &g_bakedCards, &g_bakedTilesSmall, &g_bakedTilesFull }) {
        for (auto& entry : *cache) delete entry.second.bitmap;
        cache->clear();
    }
}

void OnCoverDownloaded(HWND hwnd, unsigned long long id) {
    unsigned long long appId = id & ~kPortraitFlag;
    if (id & kPortraitFlag) g_portraits.Forget(appId);
    else g_headers.Forget(appId);
    ForgetBaked(appId);
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

// Strokes a rounded outline inside `r` so the line stays within the rectangle.
void StrokeRound(Graphics& g, RectF r, REAL radius, const Color& color, REAL width, bool dashed = false) {
    GraphicsPath path;
    REAL inset = width / 2;
    RoundPath(path, RectF(r.X + inset, r.Y + inset, r.Width - 2 * inset, r.Height - 2 * inset), radius - inset);
    Pen pen(color, width);
    if (dashed) pen.SetDashStyle(DashStyleDash);
    g.DrawPath(&pen, &path);
}

void StrokePill(Graphics& g, RectF r, const Color& color, REAL width, bool dashed = false) {
    StrokeRound(g, r, r.Height / 2, color, width, dashed);
}

// Creating fonts is slow, and a repaint draws dozens of labels, so they are created once per size.
const Font& CachedFont(REAL px, bool bold) {
    static FontFamily family(L"Arial");
    static std::map<std::pair<int, bool>, std::unique_ptr<Font>> fonts;
    auto& font = fonts[{ (int)std::lround(px * 10), bold }];
    if (!font) font = std::make_unique<Font>(&family, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    return *font;
}

// Scales `source` to w x h with rounded corners; `crop` fills the box with the middle of the image.
Bitmap* BakeArt(Bitmap* source, int w, int h, REAL radius, bool crop) {
    Bitmap* baked = new Bitmap(w, h, PixelFormat32bppPARGB);
    Graphics g(baked);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    GraphicsPath clip;
    RoundPath(clip, RectF(0, 0, (REAL)w, (REAL)h), radius);
    g.SetClip(&clip);
    RectF dest(0, 0, (REAL)w, (REAL)h);
    if (crop) {
        REAL sourceH = (REAL)source->GetHeight(), sourceW = sourceH * w / h;
        g.DrawImage(source, dest, ((REAL)source->GetWidth() - sourceW) / 2, 0, sourceW, sourceH, UnitPixel);
    } else {
        g.DrawImage(source, dest);
    }
    return baked;
}

Bitmap* GetBaked(BakedCache& cache, const std::wstring& appId, Bitmap* source, int w, int h, REAL radius, bool crop) {
    BakedArt& entry = cache[wcstoull(appId.c_str(), nullptr, 10)];
    if (!entry.bitmap || entry.w != w || entry.h != h) {
        delete entry.bitmap;
        entry = { w, h, BakeArt(source, w, h, radius, crop) };
    }
    return entry.bitmap;
}

// `wrap` lets the text use as many lines as the rectangle holds and ends the last one with an ellipsis.
void DrawLabel(Graphics& g, const std::wstring& text, REAL px, bool bold, const Color& color, RectF r,
    StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentNear, bool wrap = false) {
    const Font& font = CachedFont(px, bold);
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
    RectF box;
    g.MeasureString(badge.text.c_str(), -1, &CachedFont((REAL)S(13), true), PointF(0, 0), &box);
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
    if (!g_L.grid) return { (int)g_L.list.left, top, (int)g_L.list.right, top + g_L.rowH };
    int left = g_L.list.left + S(kMargin) - g_L.cardPad + (i % g_L.cols) * (g_L.cellW + g_L.cardGap);
    return { left, top, left + g_L.cellW, top + g_L.cellH };
}

RECT ArtRect(int i) {
    RECT cell = ItemCell(i);
    if (g_L.grid) {
        int left = cell.left + g_L.cardPad, top = cell.top + g_L.cardPad;
        return { left, top, left + g_L.artW, top + g_L.artH };
    }
    int left = cell.left + S(kRowInset) + S(kNumberColumn);
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

struct PlayParts {
    std::wstring playtime;  // "36 Std."
    std::wstring when;      // "vor 3 Tagen"
    std::wstring size;      // "54,3 GB"
    bool played = false;
};

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
    std::wstring text = parts.playtime;
    auto append = [&](const std::wstring& part) {
        if (part.empty()) return;
        text += text.empty() ? L"" : L"  ·  ";
        text += part;
    };
    if (!parts.when.empty()) append(L"Zuletzt gespielt " + parts.when);
    append(parts.size);
    return text;
}

void DrawRow(Graphics& g, int visibleIndex, int W) {
    int rowTop = ItemTop(visibleIndex);
    const Game& game = g_games[g_visible[visibleIndex]];
    bool selected = g_visible[visibleIndex] == g_sel;
    bool hover = visibleIndex == g_hover.row;

    RECT cellRect = ItemCell(visibleIndex);
    RectF slot((REAL)cellRect.left + S(kRowInset), (REAL)rowTop + S(2),
        (REAL)(cellRect.right - cellRect.left - 2 * S(kRowInset)), (REAL)(g_L.rowH - S(4)));
    if (selected) FillRound(g, slot, (REAL)S(8), kSurface);
    else if (hover) FillRound(g, slot, (REAL)S(8), kSurfaceHover);

    DrawCentered(g, std::to_wstring(visibleIndex + 1), (REAL)S(15), false, selected ? kAccentHover : kText2,
        RectF(slot.X, slot.Y, (REAL)S(kNumberColumn), slot.Height));

    RectF cover = RF(ArtRect(visibleIndex));
    GraphicsPath clip;
    RoundPath(clip, cover, (REAL)S(5));
    if (Bitmap* bitmap = GetCover(game.appId)) {
        Bitmap* baked = GetBaked(g_bakedRows, game.appId, bitmap, (int)cover.Width, (int)cover.Height, (REAL)S(5), false);
        g.DrawImage(baked, Rect((int)cover.X, (int)cover.Y, (int)cover.Width, (int)cover.Height));
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
    Bitmap* portrait = GetPortrait(game.appId);
    Bitmap* source = portrait ? portrait : GetCover(game.appId);  // without portrait art, use the middle of the header
    if (source) {
        Bitmap* baked = GetBaked(g_bakedCards, game.appId, source, g_L.artW, g_L.artH, (REAL)S(6), portrait == nullptr);
        g.DrawImage(baked, Rect((int)art.X, (int)art.Y, g_L.artW, g_L.artH));
    } else {
        SolidBrush brush(kCover);
        g.FillPath(&brush, &clip);
        DrawCentered(g, game.name.substr(0, 1), (REAL)S(40), true, kAccentHover, art);
    }

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
    // Play time (with the size next to it) and last played on their own lines.
    PlayParts stats = PlayStats(game);
    std::wstring first = stats.playtime, second;
    if (stats.played) {
        if (!stats.size.empty()) first += L"  ·  " + stats.size;
        second = L"Zuletzt " + stats.when;
    } else {
        second = stats.size;  // "Noch nicht gespielt" is long, so the size gets the next line
    }
    DrawLabel(g, first, (REAL)S(13), false, kText2, RectF(art.X, textTop + S(22), art.Width, (REAL)S(17)));
    if (!second.empty())
        DrawLabel(g, second, (REAL)S(13), false, kText2, RectF(art.X, textTop + S(39), art.Width, (REAL)S(17)));
}

// ---------------------------------------------------------------- frame

void DrawList(Graphics& g, int W) {
    RectF listRect = RF(g_L.list);
    GraphicsState clipState = g.Save();
    g.SetClip(listRect, CombineModeIntersect);
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
            FillRound(g, RectF((REAL)(g_L.list.right - S(8)), thumbY, (REAL)S(4), thumbH), (REAL)S(2), Color(90, 255, 255, 255));
        }
    }
    g.Restore(clipState);
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
    case Icon::Plus:
        g.DrawLine(&pen, cx - h * 0.7f, cy, cx + h * 0.7f, cy);
        g.DrawLine(&pen, cx, cy - h * 0.7f, cx, cy + h * 0.7f);
        break;
    case Icon::Sort:  // three bars that get shorter
        g.DrawLine(&pen, cx - h * 0.85f, cy - h * 0.6f, cx + h * 0.85f, cy - h * 0.6f);
        g.DrawLine(&pen, cx - h * 0.85f, cy, cx + h * 0.4f, cy);
        g.DrawLine(&pen, cx - h * 0.85f, cy + h * 0.6f, cx - h * 0.05f, cy + h * 0.6f);
        break;
    case Icon::Library:  // three bars, the last one leaning, like Spotify's library icon
        g.DrawLine(&pen, cx - h * 0.7f, cy - h * 0.75f, cx - h * 0.7f, cy + h * 0.75f);
        g.DrawLine(&pen, cx - h * 0.1f, cy - h * 0.75f, cx - h * 0.1f, cy + h * 0.75f);
        g.DrawLine(&pen, cx + h * 0.45f, cy - h * 0.7f, cx + h * 0.85f, cy + h * 0.75f);
        break;
    case Icon::Folder: {
        PointF outline[6] = { PointF(cx - h * 0.85f, cy + h * 0.6f), PointF(cx - h * 0.85f, cy - h * 0.6f),
            PointF(cx - h * 0.2f, cy - h * 0.6f), PointF(cx + h * 0.05f, cy - h * 0.3f),
            PointF(cx + h * 0.85f, cy - h * 0.3f), PointF(cx + h * 0.85f, cy + h * 0.6f) };
        g.DrawPolygon(&pen, outline, 6);
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

// The tile of a category: its first games as a mosaic, like a playlist cover.
void DrawCategoryTile(Graphics& g, int category, RectF tile) {
    GraphicsPath shape;
    RoundPath(shape, tile, (REAL)S(6));
    GraphicsState state = g.Save();
    g.SetClip(&shape, CombineModeIntersect);

    std::vector<const Game*> games;
    for (const auto& game : g_games) {
        if (category == 0 || game.InCategory(g_categories[category])) games.push_back(&game);
        if (games.size() == 4) break;
    }
    REAL cx = tile.X + tile.Width / 2, cy = tile.Y + tile.Height / 2;
    if (category == 0) {
        SolidBrush brush(kAccent);
        g.FillRectangle(&brush, tile);
        DrawIcon(g, Icon::Library, cx, cy, (REAL)S(22), kOnAccent);
    } else if (games.empty()) {
        SolidBrush brush(kSurface);
        g.FillRectangle(&brush, tile);
        DrawIcon(g, Icon::Folder, cx, cy, (REAL)S(24), kText2);
    } else {
        int cells = games.size() == 1 ? 1 : 2;
        REAL cell = tile.Width / cells;
        SolidBrush empty(kCover);
        g.FillRectangle(&empty, tile);
        for (int i = 0; i < (int)games.size() && i < cells * cells; ++i) {
            Bitmap* header = GetCover(games[i]->appId);
            if (!header) continue;
            BakedCache& cache = cells == 1 ? g_bakedTilesFull : g_bakedTilesSmall;
            int size = (int)cell;
            Bitmap* baked = GetBaked(cache, games[i]->appId, header, size, size, 1.0f, true);
            g.DrawImage(baked, Rect((int)(tile.X + (i % cells) * cell), (int)(tile.Y + (i / cells) * cell), size, size));
        }
    }
    g.Restore(state);
}

// The library: "Alle Spiele" and the categories as rows, like playlists in Spotify's sidebar.
void DrawSidebar(Graphics& g) {
    RectF panel = RF(g_L.sidebar);
    FillRound(g, panel, (REAL)S(8), kBg);
    DrawLabel(g, L"Bibliothek", (REAL)S(17), true, kText,
        RectF(panel.X + S(20), panel.Y, (REAL)S(140), (REAL)S(kSideHeader)), StringAlignmentNear, StringAlignmentCenter);
    RectF create = RF(g_L.create);
    FillRound(g, create, create.Height / 2, g_hover.sideCreate ? kChipHover : kSurface);
    DrawButtonContent(g, create, Icon::Plus, L"Erstellen", kText);

    GraphicsState state = g.Save();
    g.SetClip(RF(g_L.sideList), CombineModeIntersect);
    for (int i = 0; i < (int)g_L.chips.size(); ++i) {
        if (g_L.chips[i].bottom < g_L.sideList.top || g_L.chips[i].top > g_L.sideList.bottom) continue;
        RectF r = RF(g_L.chips[i]);
        bool dragging = g_drag.active;
        bool target = dragging && i >= 1 && i == g_drag.dropChip;
        bool active = i == g_active;
        bool hovered = !dragging && i == g_hover.chip;

        if (target) {
            FillRound(g, r, (REAL)S(8), Color(60, 168, 85, 247));
            StrokeRound(g, r, (REAL)S(8), kAccent, 2.0f);
        } else if (dragging && i >= 1) {
            StrokeRound(g, r, (REAL)S(8), kAccent, 1.5f, true);
        } else if (active) {
            FillRound(g, r, (REAL)S(8), kSurface);  // same colors as the game cards: selected is lighter than hovered
        } else if (hovered) {
            FillRound(g, r, (REAL)S(8), kSurfaceHover);
        }

        REAL tileSize = (REAL)S(kSideTile);
        RectF tile(r.X + S(8), r.Y + (r.Height - tileSize) / 2, tileSize, tileSize);
        DrawCategoryTile(g, i, tile);

        REAL tx = tile.GetRight() + S(12), tw = r.GetRight() - tx - S(8);
        if (i == g_renameChip) {  // the edit control sits on top of the name
            RectF field = RF(RenameRect(i));
            FillRound(g, field, (REAL)S(6), kField);
            StrokeRound(g, field, (REAL)S(6), kText, 2.0f);
        } else {
            Color name = active && !dragging ? kAccentHover : (dragging && i == 0 ? kOutline : kText);
            DrawLabel(g, g_categories[i], (REAL)S(15), true, name, RectF(tx, r.Y + S(12), tw, (REAL)S(22)));
        }
        int count = i == 0 ? (int)g_games.size()
            : (int)std::count_if(g_games.begin(), g_games.end(), [&](const Game& game) { return game.InCategory(g_categories[i]); });
        DrawLabel(g, std::to_wstring(count) + (count == 1 ? L" Spiel" : L" Spiele"), (REAL)S(13), false, kText2,
            RectF(tx, r.Y + S(36), tw, (REAL)S(18)));
    }
    g.Restore(state);
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

// Picking the current key again flips the direction; a new key starts with its natural one
// (alphabetical for names, largest or most recent first for the rest).
void SelectSort(HWND hwnd, SortKey key) {
    if (key == g_sortKey) {
        g_sortDescending = !g_sortDescending;
    } else {
        g_sortKey = key;
        g_sortDescending = key != SortKey::Name;
    }
    g_scroll = 0;
    RefreshGames();
    SaveSettings();
    Redraw(hwnd);
}

void DrawSortButton(Graphics& g) {
    RectF r = RF(g_L.sortBtn);
    FillRound(g, r, r.Height / 2, g_hover.sortBtn ? kChipHover : kSurface);
    DrawButtonContent(g, r, Icon::Sort, std::wstring(SortName(g_sortKey)) + L"  " + SortArrow(g_sortDescending), kText);
}

void DrawBar(Graphics& g, int W, int H) {
    SolidBrush bar(kBar);
    g.FillRectangle(&bar, 0, (INT)g_L.bar.top, W, H - (INT)g_L.bar.top);

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
    RectF r = RF(g_menu.rect);
    FillRound(g, RectF(r.X, r.Y + 4, r.Width, r.Height), (REAL)S(10), Color(90, 0, 0, 0));
    FillRound(g, r, (REAL)S(10), kChipHover);
    FillRound(g, RectF(r.X + 1, r.Y + 1, r.Width - 2, r.Height - 2), (REAL)S(9), Color(255, 36, 36, 36));
    for (int i = 0; i < g_menu.count; ++i) {
        RectF item = RF(g_menu.items[i]);
        bool hover = i == g_hover.menuItem;
        if (hover) FillRound(g, item, (REAL)S(6), kChipHover);
        Menu::Action action = g_menu.actions[i];
        std::wstring label;
        Color text = kText;
        switch (action) {
        case Menu::Rename: label = L"Umbenennen"; break;
        case Menu::Delete: label = L"Löschen"; text = hover ? kDangerHover : kDanger; break;
        case Menu::RemoveFromCategory:
            label = L"Aus „" + g_categories[g_menu.chip] + L"“ entfernen";
            text = hover ? kDangerHover : kDanger;
            break;
        default: {  // sort options: the current one is marked with the accent color and its direction
            SortKey key = SortKeyOf(action);
            label = SortName(key);
            if (key == g_sortKey) {
                label += std::wstring(L"  ") + SortArrow(g_sortDescending);
                text = kAccentHover;
            }
        }
        }
        DrawLabel(g, label, (REAL)S(14), false, text,
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

// Steam keeps the profile picture of signed-in accounts in config/avatarcache.
void LoadAvatar() {
    if (g_steamId.empty()) return;
    std::error_code ec;
    fs::path path = fs::path(SteamPath()) / L"config" / L"avatarcache" / (g_steamId + L".png");
    if (!fs::exists(path, ec)) return;
    Bitmap* bitmap = Bitmap::FromFile(path.c_str());
    if (!bitmap || bitmap->GetLastStatus() != Ok) { delete bitmap; return; }
    g_avatar = bitmap;
}

// The top bar: the Steam account and how many games the library holds in total.
void DrawTopBar(Graphics& g) {
    RectF bar = RF(g_L.topBar);
    REAL avatar = (REAL)S(38), x = (REAL)S(kPanelGap + 8), cy = bar.Y + bar.Height / 2;
    RectF circle(x, cy - avatar / 2, avatar, avatar);
    std::wstring name = g_steamName.empty() ? L"Steam-Bibliothek" : g_steamName;
    if (g_avatar) {
        int size = (int)avatar;
        if (!g_avatarBaked || g_avatarSize != size) {  // round, scaled once to the size it is drawn at
            delete g_avatarBaked;
            g_avatarBaked = BakeArt(g_avatar, size, size, size / 2.0f, false);
            g_avatarSize = size;
        }
        g.DrawImage(g_avatarBaked, Rect((int)circle.X, (int)circle.Y, size, size));
    } else {  // no picture in the cache: the first letter of the name
        SolidBrush brush(kAccent);
        g.FillEllipse(&brush, circle);
        DrawCentered(g, name.substr(0, 1), (REAL)S(18), true, kOnAccent, circle);
    }

    REAL tx = circle.GetRight() + S(12), tw = (REAL)S(kSidebarWidth) - S(avatar / g_scale + 12);
    DrawLabel(g, name, (REAL)S(16), true, kText, RectF(tx, cy - S(20), tw + S(200), (REAL)S(22)));
    DrawLabel(g, std::to_wstring(g_games.size()) + (g_games.size() == 1 ? L" Spiel installiert" : L" Spiele installiert"),
        (REAL)S(13), false, kText2, RectF(tx, cy + S(1), tw + S(200), (REAL)S(18)));
}

void DrawUi(Graphics& g, int W, int H) {
    SolidBrush window(kBar);
    g.FillRectangle(&window, 0, 0, W, H);
    FillRound(g, RF(g_L.main), (REAL)S(8), kBg);

    DrawTopBar(g);

    // The main panel is titled with the category that is open.
    RectF header((REAL)(g_L.main.left + S(kMargin)), (REAL)g_L.main.top, (REAL)(g_L.main.right - g_L.main.left - 2 * S(kMargin)), 0);
    DrawLabel(g, SelectedCategory(), (REAL)S(32), true, kText,
        RectF(header.X, header.Y + S(18), header.Width - S(220), (REAL)S(46)));

    DrawSidebar(g);
    DrawSortButton(g);
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
        // The back buffer lives as long as the window size does, and only the invalid part is drawn and copied.
        static HDC mem = nullptr;
        static HBITMAP bmp = nullptr;
        static int bufferW = 0, bufferH = 0;
        if (!mem) mem = CreateCompatibleDC(hdc);
        if (bufferW != W || bufferH != H) {
            HBITMAP created = CreateCompatibleBitmap(hdc, W, H);
            SelectObject(mem, created);
            if (bmp) DeleteObject(bmp);
            bmp = created;
            bufferW = W;
            bufferH = H;
        }
        {
            Graphics g(mem);
            const RECT& dirty = ps.rcPaint;
            g.SetClip(Rect(dirty.left, dirty.top, dirty.right - dirty.left, dirty.bottom - dirty.top));
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.SetInterpolationMode(InterpolationModeBilinear);
            g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
            DrawUi(g, W, H);
        }
        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
            ps.rcPaint.bottom - ps.rcPaint.top, mem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------- hit testing

int HitRow(POINT p) {
    if (!In(g_L.list, p)) return -1;
    int row = (p.y - g_L.list.top + g_scroll) / g_L.rowH, col = 0;
    if (g_L.grid) {
        int x = p.x - (g_L.list.left + S(kMargin) - g_L.cardPad);
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
    if (!In(g_L.sideList, p)) return -1;  // rows scrolled out of the sidebar are not clickable
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
    next.sideCreate = In(g_L.create, p);
    next.sortBtn = In(g_L.sortBtn, p);
    if (g_menu.open) {
        // The menu sits on top, so it hides whatever is underneath.
        next = Hover{};
        for (int i = 0; i < g_menu.count; ++i)
            if (In(g_menu.items[i], p)) next.menuItem = i;
    }
    bool changed = next.row != g_hover.row || next.button != g_hover.button || next.chip != g_hover.chip ||
        next.cover != g_hover.cover || next.menuItem != g_hover.menuItem || next.sideCreate != g_hover.sideCreate ||
        next.sortBtn != g_hover.sortBtn;
    if (!changed) return;

    // Repaint only what changed: the cards that lose and gain the highlight, the buttons, the chips.
    Hover previous = g_hover;
    g_hover = next;
    if (g_menu.open || previous.menuItem != next.menuItem) { Redraw(hwnd); return; }
    auto touch = [&](RECT r, int margin) {
        InflateRect(&r, margin, margin);
        InvalidateRect(hwnd, &r, FALSE);
    };
    for (int row : { previous.row, next.row })
        if (row >= 0 && row < (int)g_visible.size()) touch(ItemCell(row), S(6));
    for (int button : { previous.button, next.button })
        if (button >= 0) touch(g_L.btn[button], S(2));
    for (int chip : { previous.chip, next.chip })
        if (chip >= 0 && chip < (int)g_L.chips.size()) touch(g_L.chips[chip], S(2));
    if (previous.sideCreate != next.sideCreate) touch(g_L.create, S(2));
    if (previous.sortBtn != next.sortBtn) touch(g_L.sortBtn, S(2));
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

void OpenMenu(HWND hwnd, POINT at, int chip, int game, std::initializer_list<Menu::Action> actions) {
    RECT client;
    GetClientRect(hwnd, &client);
    int w = S(game >= 0 ? kMenuWidth + 56 : kMenuWidth), pad = S(kMenuPad), item = S(kMenuItemHeight);
    int h = (int)actions.size() * item + 2 * pad;
    int x = std::min((int)at.x, (int)client.right - w - S(8)), y = std::min((int)at.y, (int)client.bottom - h - S(8));
    g_menu = Menu{};
    g_menu.open = true;
    g_menu.chip = chip;
    g_menu.game = game;
    g_menu.rect = { x, y, x + w, y + h };
    for (Menu::Action action : actions) {
        int i = g_menu.count++;
        g_menu.actions[i] = action;
        g_menu.items[i] = { x + pad, y + pad + i * item, x + w - pad, y + pad + (i + 1) * item };
    }
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
        if (game.InCategory(old)) {
            game.RemoveCategory(old);
            game.AddCategory(name);
        }
    SaveSettings();
    EndRename(hwnd, true);
}

// "+ Erstellen" in the sidebar: adds a category with a default name and lets you type the real one right away.
void CreateCategoryRow(HWND hwnd) {
    std::wstring name = L"Neue Kategorie";
    for (int number = 2; std::find(g_categories.begin(), g_categories.end(), name) != g_categories.end(); ++number)
        name = L"Neue Kategorie " + std::to_wstring(number);
    g_categories.push_back(name);
    g_active = (int)g_categories.size() - 1;
    g_scroll = 0;
    g_sideScroll = MaxSideScroll() + S(kSideRow);
    RefreshGames();
    Relayout(hwnd);
    SaveSettings();
    StartRename(hwnd, g_active);
}

void DeleteCategory(HWND hwnd, int index) {
    const std::wstring name = g_categories[index];
    for (auto& game : g_games)
        game.RemoveCategory(name);
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
        // Dropping copies: the game stays where it is and also joins the category.
        Game& game = g_games[g_visible[g_drag.row]];
        const std::wstring& category = g_categories[g_drag.dropChip];
        bool added = game.AddCategory(category);
        SaveSettings();
        EndDrag(hwnd);
        RefreshGames();
        ShowToast(hwnd, added ? L"Zu „" + category + L"“ hinzugefügt" : L"Schon in „" + category + L"“");
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

POINT ClientPoint(LPARAM lp) { return { (short)LOWORD(lp), (short)HIWORD(lp) }; }

void OnMouseDown(HWND hwnd, UINT msg, LPARAM lp) {
    SetFocus(hwnd);
    POINT p = ClientPoint(lp);
    if (g_menu.open) {
        // The menu swallows the click: pick an item, or just close it.
        Menu menu = g_menu;
        int picked = g_hover.menuItem;
        CloseMenu(hwnd);
        if (picked < 0 || picked >= menu.count) return;
        switch (menu.actions[picked]) {
        case Menu::Rename: StartRename(hwnd, menu.chip); break;
        case Menu::Delete: DeleteCategory(hwnd, menu.chip); break;
        case Menu::RemoveFromCategory:
            g_games[menu.game].RemoveCategory(g_categories[menu.chip]);
            SaveSettings();
            RefreshGames();
            Redraw(hwnd);
            break;
        default: SelectSort(hwnd, SortKeyOf(menu.actions[picked])); break;
        }
        return;
    }
    if (In(g_L.create, p)) {
        CreateCategoryRow(hwnd);
        return;
    }
    if (In(g_L.sortBtn, p)) {
        POINT below{ g_L.sortBtn.right - S(kMenuWidth), g_L.sortBtn.bottom + S(4) };
        OpenMenu(hwnd, below, -1, -1, { Menu::SortByName, Menu::SortByPlaytime, Menu::SortByLastPlayed, Menu::SortBySize });
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
    ClearBaked();
    delete g_avatar;
    delete g_avatarBaked;
    g_avatar = g_avatarBaked = nullptr;
    PostQuitMessage(0);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_SIZE:
        if (g_renameEdit) Relayout(hwnd);
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
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (In(g_L.sidebar, p)) {
            g_sideScroll -= delta * S(kSideRow) / WHEEL_DELTA;
            Relayout(hwnd);  // moves the rows
        } else {
            g_scroll -= delta * g_L.wheelStep / WHEEL_DELTA;
            ClampScroll();
        }
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
        int chip = HitChip(p), row = HitRow(p);
        CloseMenu(hwnd);
        if (chip >= 1) {
            OpenMenu(hwnd, p, chip, -1, { Menu::Rename, Menu::Delete });  // "Alle Spiele", the first row, is built in
        } else if (row >= 0 && g_active >= 1) {
            // A game inside a category can be taken out of it again; it stays in the library and other categories.
            OpenMenu(hwnd, p, g_active, g_visible[row], { Menu::RemoveFromCategory });
        }
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
    LoadSteamUser();
    LoadAvatar();
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
