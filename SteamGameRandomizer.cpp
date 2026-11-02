#define UNICODE
#define _UNICODE
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winreg.h>
#include <winhttp.h>
#include <dwmapi.h>

#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>

#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Gdiplus.lib")
#pragma comment(lib, "Winhttp.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

namespace {
    struct Game {
        std::wstring appId;
        std::wstring name;
        std::wstring category = L"Unsortiert";
    };

    std::vector<Game> g_games;
    std::vector<std::wstring> g_categories{ L"Alle Spiele", L"Unsortiert" };
    std::wstring g_configPath;
    std::wstring g_coverDir;

    std::wstring GetAppDataFolder() {
        PWSTR knownFolder = nullptr;
        std::wstring result = L".";
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &knownFolder))) {
            result = knownFolder;
            CoTaskMemFree(knownFolder);
        }
        result += L"\\GameRandomizer";
        CreateDirectoryW(result.c_str(), nullptr);
        return result;
    }

    std::wstring GetRegistryString(HKEY root, const wchar_t* subkey, const wchar_t* valueName) {
        wchar_t buffer[32768]{};
        DWORD bytes = sizeof(buffer);
        DWORD type = 0;
        if (RegGetValueW(root, subkey, valueName, RRF_RT_REG_SZ, &type, buffer, &bytes) == ERROR_SUCCESS)
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

    std::wstring ExtractVdfValue(const std::wstring& text, const std::wstring& key) {
        // Reads simple VDF key/value pairs, including escaped backslashes in paths.
        std::wstring marker = L"\"" + key + L"\"";
        size_t pos = text.find(marker);
        if (pos == std::wstring::npos) return {};
        pos = text.find(L'"', pos + marker.size());
        if (pos == std::wstring::npos) return {};
        size_t end = text.find(L'"', pos + 1);
        if (end == std::wstring::npos) return {};
        std::wstring value = text.substr(pos + 1, end - pos - 1);
        std::wstring fixed;
        for (size_t i = 0; i < value.size(); ++i) {
            if (value[i] == L'\\' && i + 1 < value.size() && value[i + 1] == L'\\') {
                fixed += L'\\';
                ++i;
            } else fixed += value[i];
        }
        return fixed;
    }

    std::wstring GetSteamPath() {
        std::wstring steam = GetRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
        if (steam.empty())
            steam = GetRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
        if (steam.empty()) {
            wchar_t programFiles[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"PROGRAMFILES(X86)", programFiles, MAX_PATH))
                steam = std::wstring(programFiles) + L"\\Steam";
        }
        return steam;
    }

    std::vector<fs::path> FindSteamLibraries() {
        std::set<std::wstring> unique;
        std::vector<fs::path> libraries;

        std::wstring steam = GetSteamPath();
        if (steam.empty()) return libraries;

        fs::path steamPath(steam);
        fs::path vdf = steamPath / L"steamapps" / L"libraryfolders.vdf";
        unique.insert(steamPath.wstring());

        std::wstring contents = ReadTextFile(vdf);
        size_t pos = 0;
        while ((pos = contents.find(L"\"path\"", pos)) != std::wstring::npos) {
            size_t keyEnd = pos + 6;
            size_t valueStart = contents.find(L'"', keyEnd);
            if (valueStart == std::wstring::npos) break;
            size_t valueEnd = contents.find(L'"', valueStart + 1);
            if (valueEnd == std::wstring::npos) break;
            std::wstring raw = contents.substr(valueStart + 1, valueEnd - valueStart - 1);
            std::wstring decoded;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == L'\\' && i + 1 < raw.size() && raw[i + 1] == L'\\') {
                    decoded += L'\\'; ++i;
                } else decoded += raw[i];
            }
            if (!decoded.empty()) unique.insert(decoded);
            pos = valueEnd + 1;
        }

        for (const auto& item : unique) {
            fs::path p(item);
            std::error_code ec;
            if (fs::exists(p / L"steamapps", ec))
                libraries.push_back(p);
        }
        return libraries;
    }

    std::map<std::wstring, Game> ScanSteamGames() {
        std::map<std::wstring, Game> found;
        for (const auto& library : FindSteamLibraries()) {
            fs::path steamApps = library / L"steamapps";
            std::error_code ec;
            for (fs::directory_iterator it(steamApps, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                std::wstring filename = it->path().filename().wstring();
                if (filename.size() < 19 || filename.rfind(L"appmanifest_", 0) != 0 ||
                    it->path().extension() != L".acf") continue;

                std::wstring vdf = ReadTextFile(it->path());
                std::wstring appId = ExtractVdfValue(vdf, L"appid");
                std::wstring name = ExtractVdfValue(vdf, L"name");
                if (appId.empty() || name.empty()) continue;
                // Skip Steam's own tools, they are not games.
                if (name.rfind(L"Steamworks Common", 0) == 0 || name.rfind(L"Steam Linux Runtime", 0) == 0 ||
                    name.rfind(L"Proton", 0) == 0 || name.rfind(L"Steam Controller", 0) == 0) continue;

                Game game;
                game.appId = appId;
                game.name = name;
                found[appId] = game;
            }
        }
        return found;
    }

    void LoadSettings() {
        g_configPath = GetAppDataFolder() + L"\\settings.txt";
        std::wstring data = ReadTextFile(g_configPath);
        size_t start = 0;
        while (start < data.size()) {
            size_t end = data.find(L'\n', start);
            if (end == std::wstring::npos) end = data.size();
            std::wstring line = data.substr(start, end - start);
            if (!line.empty() && line.back() == L'\r') line.pop_back();

            if (line.rfind(L"C\t", 0) == 0) {
                std::wstring category = line.substr(2);
                if (!category.empty() && std::find(g_categories.begin(), g_categories.end(), category) == g_categories.end())
                    g_categories.push_back(category);
            } else if (line.rfind(L"G\t", 0) == 0) {
                size_t split = line.find(L'\t', 2);
                if (split != std::wstring::npos) {
                    std::wstring id = line.substr(2, split - 2);
                    std::wstring category = line.substr(split + 1);
                    // Saved assignments are applied after the Steam scan.
                    if (!id.empty() && !category.empty()) {
                        // Temporarily store as a synthetic entry; merged after scanning.
                        g_games.push_back({ id, L"", category });
                    }
                }
            }
            start = end + 1;
        }
    }

    void SaveSettings() {
        std::ofstream file(fs::path(g_configPath), std::ios::binary | std::ios::trunc);
        if (!file) return;
        for (const auto& category : g_categories) {
            if (category == L"Alle Spiele" || category == L"Unsortiert") continue;
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

    void MergeGames(std::map<std::wstring, Game> detected) {
        std::map<std::wstring, std::wstring> oldAssignments;
        for (const auto& game : g_games)
            if (!game.appId.empty() && !game.category.empty())
                oldAssignments[game.appId] = game.category;

        g_games.clear();
        for (auto& pair : detected) {
            Game game = pair.second;
            auto saved = oldAssignments.find(game.appId);
            if (saved != oldAssignments.end() &&
                std::find(g_categories.begin(), g_categories.end(), saved->second) != g_categories.end())
                game.category = saved->second;
            g_games.push_back(game);
        }
        std::sort(g_games.begin(), g_games.end(), [](const Game& a, const Game& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        SaveSettings();
    }

    void ScanAndMerge() { MergeGames(ScanSteamGames()); }

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

    // Steam keeps collections in userdata/<account>/config/cloudstorage. Only collections that contain
    // at least one installed game are imported, and only games without a category are moved into them.
    // Smaller collections go first so a game lands in its most specific collection.
    ImportResult ImportSteamCategories() {
        struct Collection {
            std::wstring name;
            std::vector<Game*> members;
        };
        std::vector<Collection> collections;

        std::wstring steam = GetSteamPath();
        std::error_code ec;
        if (!steam.empty()) {
            for (fs::directory_iterator it(fs::path(steam) / L"userdata", ec), end; !ec && it != end; it.increment(ec)) {
                std::wstring text = ReadTextFile(it->path() / L"config" / L"cloudstorage" / L"cloud-storage-namespace-1.json");
                const std::wstring keyMarker = L"\"key\":\"user-collections.";
                size_t pos = 0;
                while ((pos = text.find(keyMarker, pos)) != std::wstring::npos) {
                    size_t next = pos + keyMarker.size();
                    size_t entryEnd = text.find(L"}],[", pos);
                    size_t valuePos = text.find(L"\"value\":\"", pos);
                    bool hidden = ReadJsonString(text, next) == L"hidden";
                    pos = next;
                    if (hidden || valuePos == std::wstring::npos || valuePos > entryEnd) continue;

                    std::wstring inner = ReadJsonString(text, valuePos + 9);
                    size_t namePos = inner.find(L"\"name\":\"");
                    size_t addedPos = inner.find(L"\"added\":[");
                    if (namePos == std::wstring::npos || addedPos == std::wstring::npos) continue;

                    Collection collection;
                    collection.name = ReadJsonString(inner, namePos + 8);
                    size_t listEnd = inner.find(L']', addedPos);
                    std::wstring list = inner.substr(addedPos + 9, listEnd - addedPos - 9);
                    size_t start = 0;
                    while (start < list.size()) {
                        size_t comma = list.find(L',', start);
                        if (comma == std::wstring::npos) comma = list.size();
                        std::wstring id = list.substr(start, comma - start);
                        for (auto& game : g_games)
                            if (!game.name.empty() && game.appId == id) collection.members.push_back(&game);
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
            if (name == L"Alle Spiele" || name == L"Unsortiert" || name.find_first_of(L"\t\r\n") != std::wstring::npos)
                continue;
            bool exists = std::find(g_categories.begin(), g_categories.end(), name) != g_categories.end();
            int assigned = 0;
            for (Game* game : collection.members)
                if (game->category == L"Unsortiert") ++assigned;
            if (assigned == 0) continue;
            if (!exists) {
                g_categories.push_back(name);
                ++result.categories;
            }
            for (Game* game : collection.members)
                if (game->category == L"Unsortiert") game->category = name;
            result.games += assigned;
        }
        return result;
    }

    // ---------------------------------------------------------------- covers

    constexpr UINT WM_COVER = WM_APP + 1;

    HWND g_mainWnd = nullptr;
    std::mutex g_queueMutex;
    std::deque<std::wstring> g_queue;
    std::atomic<bool> g_worker{ false };
    std::atomic<bool> g_quit{ false };

    fs::path CoverPath(const std::wstring& appId) {
        return fs::path(g_coverDir) / (appId + L".jpg");
    }

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

    // Newer Steam games store art under a hashed path, so ask the store API where the header image lives.
    std::wstring StoreHeaderUrl(const std::wstring& appId) {
        std::string json;
        HttpGet(L"https://store.steampowered.com/api/appdetails?appids=" + appId + L"&filters=basic",
            [&](const char* data, DWORD size) { json.append(data, size); });
        const std::string key = "\"header_image\":\"";
        size_t start = json.find(key);
        if (start == std::string::npos) return {};
        start += key.size();
        std::string url;
        for (size_t i = start; i < json.size() && json[i] != '"'; ++i) {
            if (json[i] == '\\' && i + 1 < json.size()) {
                if (json[i + 1] == 'u' && json.compare(i + 2, 4, "0026") == 0) { url += '&'; i += 5; continue; }
                url += json[++i];
            } else url += json[i];
        }
        return std::wstring(url.begin(), url.end());
    }

    bool DownloadCover(const std::wstring& appId) {
        fs::path finalPath = CoverPath(appId);
        std::error_code ec;
        if (fs::exists(finalPath, ec)) return false;
        fs::path tmp = finalPath;
        tmp += L".tmp";
        std::wstring cdn = L"https://cdn.cloudflare.steamstatic.com/steam/apps/" + appId + L"/header.jpg";
        bool got = HttpGetToFile(cdn, tmp);
        if (!got) {
            std::wstring url = StoreHeaderUrl(appId);
            got = !url.empty() && HttpGetToFile(url, tmp);
        }
        if (!got) {
            fs::remove(tmp, ec);
            return false;
        }
        // Virus scanners can briefly lock a freshly written file, so retry the rename.
        for (int attempt = 0; attempt < 10; ++attempt) {
            if (MoveFileExW(tmp.c_str(), finalPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return true;
            Sleep(150);
        }
        fs::remove(tmp, ec);
        return false;
    }

    void CoverWorker() {
        for (;;) {
            std::wstring appId;
            {
                std::lock_guard<std::mutex> lock(g_queueMutex);
                if (g_queue.empty()) { g_worker = false; return; }
                appId = g_queue.front();
                g_queue.pop_front();
            }
            if (g_quit) return;
            if (DownloadCover(appId))
                PostMessageW(g_mainWnd, WM_COVER, (WPARAM)wcstoull(appId.c_str(), nullptr, 10), 0);
        }
    }

    void QueueCovers() {
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            for (const auto& game : g_games) {
                std::error_code ec;
                if (!game.appId.empty() && !game.name.empty() && !fs::exists(CoverPath(game.appId), ec))
                    g_queue.push_back(game.appId);
            }
        }
        bool expected = false;
        if (g_worker.compare_exchange_strong(expected, true))
            std::thread(CoverWorker).detach();
    }

    // -------------------------------------------------------------------- ui

    using namespace Gdiplus;

    constexpr int ID_EDIT = 2001;
    constexpr int ID_ACTION = 3000;
    enum { BTN_CREATE, BTN_SCAN, BTN_IMPORT, BTN_LAUNCH, BTN_COUNT };
    constexpr DWORD WINDOW_STYLE = (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN;
    constexpr UINT WM_SCANDONE = WM_APP + 2;

    const Color kBg(255, 18, 18, 18);
    const Color kBar(255, 0, 0, 0);
    const Color kSurface(255, 42, 42, 42);
    const Color kSurfaceHover(255, 31, 31, 31);
    const Color kChipHover(255, 62, 62, 62);
    const Color kCover(255, 40, 40, 40);
    const Color kAccent(255, 168, 85, 247);
    const Color kAccentHover(255, 192, 132, 252);
    const Color kText(255, 255, 255, 255);
    const Color kText2(255, 179, 179, 179);
    const Color kLine(255, 31, 31, 31);
    const Color kOutline(255, 114, 114, 114);

    struct Layout {
        RECT list{};
        RECT bar{};
        RECT pill{};
        RECT btn[BTN_COUNT]{};
        std::vector<RECT> chips;
        int rowH = 0;
    };

    float g_scale = 1.0f;
    Layout g_L;
    HWND g_edit = nullptr;
    HFONT g_editFont = nullptr;
    HBRUSH g_editBrush = nullptr;
    std::vector<int> g_visible;
    int g_active = 0;
    int g_sel = -1;
    int g_scroll = 0;
    int g_hoverRow = -1, g_hoverBtn = -1, g_hoverChip = -1;
    int g_importState = 0;       // 0 idle, 2 showing the import result
    bool g_importFound = false;
    std::wstring g_importText;
    ULONGLONG g_importTick = 0;
    int g_scanState = 0;         // 0 idle, 1 scanning, 2 done (shows a check mark)
    bool g_scanReady = false;
    ULONGLONG g_scanTick = 0, g_checkTick = 0;
    bool g_hoverPlay = false;
    int g_dragIdx = -1;          // visible index of the pressed row, -1 when none
    bool g_dragging = false;     // true once the pointer moved far enough to count as a drag
    POINT g_dragStart{}, g_dragPos{};
    int g_dropChip = -1;
    bool g_tracking = false;
    std::map<unsigned long long, Bitmap*> g_covers;
    std::set<unsigned long long> g_coverTried;

    int S(float v) { return (int)std::lround(v * g_scale); }

    RectF RF(const RECT& r) {
        return RectF((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
    }

    bool In(const RECT& r, POINT p) { return PtInRect(&r, p) != 0; }

    std::wstring SelectedCategory() {
        if (g_active < 0 || g_active >= (int)g_categories.size()) return L"Alle Spiele";
        return g_categories[g_active];
    }

    int MaxScroll() {
        int content = (int)g_visible.size() * g_L.rowH;
        int view = g_L.list.bottom - g_L.list.top;
        return std::max(0, content - view);
    }

    void ClampScroll() { g_scroll = std::clamp(g_scroll, 0, MaxScroll()); }

    void RefreshGames() {
        std::wstring filter = SelectedCategory();
        g_visible.clear();
        bool selStillVisible = false;
        for (int i = 0; i < (int)g_games.size(); ++i) {
            if (g_games[i].name.empty()) continue;
            if (filter != L"Alle Spiele" && g_games[i].category != filter) continue;
            g_visible.push_back(i);
            if (i == g_sel) selStillVisible = true;
        }
        if (!selStillVisible) g_sel = -1;
        ClampScroll();
    }

    void Relayout(HWND hwnd) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int W = rc.right, H = rc.bottom;

        g_L.rowH = S(64);
        g_L.chips.clear();
        {
            Graphics g(hwnd);
            FontFamily family(L"Arial");
            Font font(&family, (REAL)S(13), FontStyleBold, UnitPixel);
            int x = S(24), y = S(92), h = S(32), gap = S(8);
            for (const auto& category : g_categories) {
                RectF box;
                g.MeasureString(category.c_str(), -1, &font, PointF(0, 0), &box);
                int w = (int)box.Width + S(28);
                if (x + w > W - S(24) && x > S(24)) { x = S(24); y += h + gap; }
                g_L.chips.push_back({ x, y, x + w, y + h });
                x += w + gap;
            }
            int chipsBottom = y + h;
            int barH = S(76);
            g_L.bar = { 0, H - barH, W, H };
            g_L.list = { 0, chipsBottom + S(16), W, std::max((int)(chipsBottom + S(16)), (int)g_L.bar.top) };
        }

        int ph = S(40);
        int py = g_L.bar.top + (S(76) - ph) / 2;
        g_L.pill = { S(20), py, S(20) + S(150), py + ph };
        int x = g_L.pill.right + S(8);
        const int widths[] = { S(88), S(112), S(140) };
        for (int i = 0; i < 3; ++i) {
            g_L.btn[i] = { x, py, x + widths[i], py + ph };
            x += widths[i] + S(8);
        }
        g_L.btn[BTN_LAUNCH] = { W - S(20) - S(180), py, W - S(20), py + ph };

        if (g_edit) {
            int eh = S(18);
            SetWindowPos(g_edit, nullptr, g_L.pill.left + S(16), py + (ph - eh) / 2,
                g_L.pill.right - g_L.pill.left - S(32), eh, SWP_NOZORDER);
        }
        ClampScroll();
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    Bitmap* GetCover(const std::wstring& appId) {
        unsigned long long id = wcstoull(appId.c_str(), nullptr, 10);
        auto it = g_covers.find(id);
        if (it != g_covers.end()) return it->second;
        if (g_coverTried.count(id)) return nullptr;
        g_coverTried.insert(id);
        std::error_code ec;
        fs::path path = CoverPath(appId);
        if (!fs::exists(path, ec)) return nullptr;
        Bitmap* bitmap = Bitmap::FromFile(path.c_str());
        if (!bitmap || bitmap->GetLastStatus() != Ok) { delete bitmap; return nullptr; }
        g_covers[id] = bitmap;
        return bitmap;
    }

    void OnCoverDownloaded(HWND hwnd, unsigned long long id) {
        auto it = g_covers.find(id);
        if (it != g_covers.end()) { delete it->second; g_covers.erase(it); }
        g_coverTried.erase(id);
        InvalidateRect(hwnd, nullptr, FALSE);
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

    void DrawLabel(Graphics& g, const std::wstring& text, REAL px, bool bold, const Color& color,
        RectF r, StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentNear) {
        FontFamily family(L"Arial");
        Font font(&family, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
        StringFormat format;
        format.SetAlignment(h);
        format.SetLineAlignment(v);
        format.SetTrimming(StringTrimmingEllipsisCharacter);
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        SolidBrush brush(color);
        g.DrawString(text.c_str(), -1, &font, r, &format, &brush);
    }

    RECT CoverRect(int rowTop) {
        int left = S(14) + S(52);
        int top = rowTop + S(2) + (g_L.rowH - S(4) - S(45)) / 2;
        return { left, top, left + S(96), top + S(45) };
    }

    bool InCover(int row, POINT p) {
        if (row < 0) return false;
        RECT r = CoverRect(g_L.list.top + row * g_L.rowH - g_scroll);
        return PtInRect(&r, p) != 0;
    }

    void DrawRow(Graphics& g, int visibleIndex, int rowTop, int W) {
        const Game& game = g_games[g_visible[visibleIndex]];
        bool selected = g_visible[visibleIndex] == g_sel;
        bool hover = visibleIndex == g_hoverRow;

        RectF slot((REAL)S(14), (REAL)rowTop + S(2), (REAL)(W - S(28)), (REAL)(g_L.rowH - S(4)));
        if (selected) FillRound(g, slot, (REAL)S(8), kSurface);
        else if (hover) FillRound(g, slot, (REAL)S(8), kSurfaceHover);

        RectF numberCell(slot.X, slot.Y, (REAL)S(52), slot.Height);
        DrawLabel(g, std::to_wstring(visibleIndex + 1), (REAL)S(15), false, selected ? kAccentHover : kText2,
            numberCell, StringAlignmentCenter, StringAlignmentCenter);

        RectF cover = RF(CoverRect(rowTop));
        GraphicsPath clip;
        RoundPath(clip, cover, (REAL)S(5));
        Bitmap* bitmap = GetCover(game.appId);
        if (bitmap) {
            GraphicsState state = g.Save();
            g.SetClip(&clip, CombineModeIntersect);
            g.DrawImage(bitmap, cover);
            g.Restore(state);
        } else {
            SolidBrush brush(kCover);
            g.FillPath(&brush, &clip);
            DrawLabel(g, game.name.substr(0, 1), (REAL)S(18), true, kAccentHover, cover,
                StringAlignmentCenter, StringAlignmentCenter);
        }

        if (hover && !g_dragging) {
            bool hot = g_hoverPlay;
            SolidBrush dim(Color(110, 0, 0, 0));
            g.FillPath(&dim, &clip);
            REAL diameter = (REAL)S(30);
            REAL cx = cover.X + cover.Width / 2, cy = cover.Y + cover.Height / 2;
            SolidBrush disc(hot ? kAccent : Color(230, 42, 42, 42));
            g.FillEllipse(&disc, cx - diameter / 2, cy - diameter / 2, diameter, diameter);
            REAL size = (REAL)S(12);
            PointF triangle[3] = { PointF(cx - size * 0.3f, cy - size / 2), PointF(cx - size * 0.3f, cy + size / 2),
                PointF(cx + size * 0.6f, cy) };
            SolidBrush arrow(hot ? Color(255, 0, 0, 0) : kText);
            g.FillPolygon(&arrow, triangle, 3);
        }

        REAL tx = cover.GetRight() + S(16);
        REAL tw = slot.GetRight() - tx - S(16);
        DrawLabel(g, game.name, (REAL)S(15), true, selected ? kAccentHover : kText,
            RectF(tx, slot.Y + S(10), tw, (REAL)S(22)));
        DrawLabel(g, game.category, (REAL)S(13), false, kText2,
            RectF(tx, slot.Y + S(32), tw, (REAL)S(18)));
    }

    enum StatusIcon { ICON_SPINNER, ICON_CHECK, ICON_NONE };

    // Button status: spinner, animated check mark (since = tick it started) or text only.
    void DrawStatusContent(Graphics& g, RectF r, const std::wstring& text, StatusIcon mode, ULONGLONG since) {
        FontFamily family(L"Arial");
        Font font(&family, (REAL)S(13), FontStyleBold, UnitPixel);
        RectF box;
        g.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &box);
        REAL icon = mode == ICON_NONE ? 0.0f : (REAL)S(16);
        REAL gap = mode == ICON_NONE ? 0.0f : (REAL)S(6), textW = box.Width - S(8);
        REAL startX = r.X + (r.Width - (icon + gap + textW)) / 2;
        REAL cx = startX + icon / 2, cy = r.Y + r.Height / 2;

        Pen pen(kAccentHover, (REAL)S(2));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        pen.SetLineJoin(LineJoinRound);
        if (mode == ICON_SPINNER) {
            REAL angle = (REAL)(GetTickCount64() % 800) / 800.0f * 360.0f;
            g.DrawArc(&pen, cx - icon / 2 + 1, cy - icon / 2 + 1, icon - 2, icon - 2, angle, 270);
        } else if (mode == ICON_CHECK) {
            double t = std::min(1.0, (double)(GetTickCount64() - since) / 260.0);
            double e = 1.0 - std::pow(1.0 - t, 3.0);
            REAL k = (REAL)S(1);
            PointF a(cx - 5 * k, cy + 0.5f * k), b(cx - 1.5f * k, cy + 4 * k), c(cx + 6 * k, cy - 4 * k);
            REAL l1 = std::hypot(b.X - a.X, b.Y - a.Y), l2 = std::hypot(c.X - b.X, c.Y - b.Y);
            REAL drawn = (REAL)e * (l1 + l2);
            REAL f1 = std::min(drawn, l1) / l1;
            g.DrawLine(&pen, a, PointF(a.X + (b.X - a.X) * f1, a.Y + (b.Y - a.Y) * f1));
            if (drawn > l1) {
                REAL f2 = (drawn - l1) / l2;
                g.DrawLine(&pen, b, PointF(b.X + (c.X - b.X) * f2, b.Y + (c.Y - b.Y) * f2));
            }
        }
        DrawLabel(g, text, (REAL)S(13), true, kText,
            RectF(startX + icon + gap, r.Y, textW + S(12), r.Height), StringAlignmentNear, StringAlignmentCenter);
    }

    void DrawUi(Graphics& g, HWND hwnd, int W, int H) {
        SolidBrush bg(kBg);
        g.FillRectangle(&bg, 0, 0, W, H);

        DrawLabel(g, L"Deine Spiele", (REAL)S(30), true, kText, RectF((REAL)S(24), (REAL)S(18), (REAL)(W - S(48)), (REAL)S(40)));
        std::wstring sub = SelectedCategory() == L"Alle Spiele"
            ? std::to_wstring(g_visible.size()) + L" installierte Spiele"
            : std::to_wstring(g_visible.size()) + L" Spiele in „" + SelectedCategory() + L"“";
        if (g_dragging) sub = L"Lass das Spiel auf einer Kategorie los, um es zuzuordnen";
        else if (g_categories.size() <= 2) sub += L"  ·  Lege unten eine Kategorie an und zieh Spiele hinein";
        else sub += L"  ·  Zieh ein Spiel auf eine Kategorie, um es zuzuordnen";
        DrawLabel(g, sub, (REAL)S(13), false, kText2, RectF((REAL)S(24), (REAL)S(60), (REAL)(W - S(48)), (REAL)S(20)));

        for (int i = 0; i < (int)g_L.chips.size(); ++i) {
            bool active = i == g_active;
            RectF r = RF(g_L.chips[i]);
            bool drop = g_dragging && i >= 1;
            bool target = drop && i == g_dropChip;
            bool dim = g_dragging && i == 0;
            FillRound(g, r, r.Height / 2, target ? kAccent : (active && !g_dragging ? kAccent : (!g_dragging && i == g_hoverChip ? kChipHover : kSurface)));
            if (drop && !target) {
                GraphicsPath path;
                RoundPath(path, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), r.Height / 2);
                Pen pen(kAccent, 1.5f);
                pen.SetDashStyle(DashStyleDash);
                g.DrawPath(&pen, &path);
            }
            bool darkText = target || (active && !g_dragging);
            DrawLabel(g, g_categories[i], (REAL)S(13), true, darkText ? Color(255, 0, 0, 0) : (dim ? kOutline : kText), r,
                StringAlignmentCenter, StringAlignmentCenter);
        }

        RectF listRect = RF(g_L.list);
        g.SetClip(listRect);
        if (g_visible.empty()) {
            DrawLabel(g, L"Hier ist noch nichts", (REAL)S(18), true, kText,
                RectF(listRect.X, listRect.Y + S(40), listRect.Width, (REAL)S(26)), StringAlignmentCenter);
            DrawLabel(g, L"Wähle eine andere Kategorie oder scanne Steam neu.", (REAL)S(13), false, kText2,
                RectF(listRect.X, listRect.Y + S(70), listRect.Width, (REAL)S(20)), StringAlignmentCenter);
        } else {
            int first = g_scroll / g_L.rowH;
            for (int i = first; i < (int)g_visible.size(); ++i) {
                int top = g_L.list.top + i * g_L.rowH - g_scroll;
                if (top >= g_L.list.bottom) break;
                DrawRow(g, i, top, W);
            }
            int maxScroll = MaxScroll();
            if (maxScroll > 0) {
                REAL view = (REAL)(g_L.list.bottom - g_L.list.top);
                REAL content = (REAL)(g_visible.size() * g_L.rowH);
                REAL thumbH = std::max((REAL)S(32), view * view / content);
                REAL thumbY = g_L.list.top + (view - thumbH) * g_scroll / maxScroll;
                FillRound(g, RectF((REAL)(W - S(8)), thumbY, (REAL)S(4), thumbH), (REAL)S(2), Color(90, 255, 255, 255));
            }
        }
        g.ResetClip();

        SolidBrush bar(kBar);
        g.FillRectangle(&bar, 0, (INT)g_L.bar.top, W, H - (INT)g_L.bar.top);
        SolidBrush line(kLine);
        g.FillRectangle(&line, 0, (INT)g_L.bar.top, W, 1);

        RectF pill = RF(g_L.pill);
        FillRound(g, pill, pill.Height / 2, kSurface);
        if (GetFocus() == g_edit) {
            GraphicsPath path;
            RoundPath(path, RectF(pill.X + 0.5f, pill.Y + 0.5f, pill.Width - 1, pill.Height - 1), pill.Height / 2);
            Pen pen(kAccent, 1.5f);
            g.DrawPath(&pen, &path);
        }

        const wchar_t* labels[BTN_COUNT] = { L"Erstellen", L"Neu scannen", L"Aus Steam holen", L"Zufällig starten" };
        for (int i = 0; i < BTN_COUNT; ++i) {
            RectF r = RF(g_L.btn[i]);
            bool hover = i == g_hoverBtn;
            if (i == BTN_LAUNCH) {
                FillRound(g, r, r.Height / 2, hover ? kAccentHover : kAccent);
                DrawLabel(g, labels[i], (REAL)S(14), true, Color(255, 0, 0, 0), r, StringAlignmentCenter, StringAlignmentCenter);
            } else {
                GraphicsPath path;
                RoundPath(path, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), r.Height / 2);
                bool scanBusy = i == BTN_SCAN && g_scanState != 0;
                bool importBusy = i == BTN_IMPORT && g_importState != 0;
                bool busy = scanBusy || importBusy;
                Pen pen(busy ? kAccent : (hover ? kText : kOutline), 1.0f);
                g.DrawPath(&pen, &path);
                if (scanBusy) DrawStatusContent(g, r, g_scanState == 1 ? L"Scannt…" : L"Fertig",
                    g_scanState == 1 ? ICON_SPINNER : ICON_CHECK, g_checkTick);
                else if (importBusy) DrawStatusContent(g, r, g_importText,
                    g_importFound ? ICON_CHECK : ICON_NONE, g_importTick);
                else DrawLabel(g, labels[i], (REAL)S(13), true, kText, r, StringAlignmentCenter, StringAlignmentCenter);
            }
        }

        if (g_dragging && g_dragIdx >= 0 && g_dragIdx < (int)g_visible.size()) {
            const Game& dragged = g_games[g_visible[g_dragIdx]];
            RectF ghost((REAL)g_dragPos.x + S(14), (REAL)g_dragPos.y + S(10), (REAL)S(220), (REAL)S(40));
            FillRound(g, RectF(ghost.X + 1, ghost.Y + 3, ghost.Width, ghost.Height), (REAL)S(20), Color(70, 0, 0, 0));
            FillRound(g, ghost, (REAL)S(20), kAccent);
            DrawLabel(g, dragged.name, (REAL)S(14), true, Color(255, 0, 0, 0),
                RectF(ghost.X + S(18), ghost.Y, ghost.Width - S(36), ghost.Height),
                StringAlignmentNear, StringAlignmentCenter);
        }
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
                DrawUi(g, hwnd, W, H);
            }
            BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
    }

    int HitRow(POINT p) {
        if (!In(g_L.list, p)) return -1;
        int index = (p.y - g_L.list.top + g_scroll) / g_L.rowH;
        return index >= 0 && index < (int)g_visible.size() ? index : -1;
    }

    int HitBtn(POINT p) {
        for (int i = 0; i < BTN_COUNT; ++i)
            if (In(g_L.btn[i], p)) return i;
        return -1;
    }

    int HitChip(POINT p) {
        for (int i = 0; i < (int)g_L.chips.size(); ++i)
            if (In(g_L.chips[i], p)) return i;
        return -1;
    }

    void EnsureVisible(int visibleIndex) {
        int top = visibleIndex * g_L.rowH;
        int view = g_L.list.bottom - g_L.list.top;
        if (top < g_scroll) g_scroll = top;
        else if (top + g_L.rowH > g_scroll + view) g_scroll = top + g_L.rowH - view;
        ClampScroll();
    }

    // --------------------------------------------------------------- actions

    void AddCategory(HWND hwnd) {
        int length = GetWindowTextLengthW(g_edit);
        if (length <= 0) return;
        std::wstring name(length + 1, L'\0');
        GetWindowTextW(g_edit, name.data(), length + 1);
        name.resize(length);
        if (name.find_first_of(L"\t\r\n") != std::wstring::npos) {
            MessageBoxW(hwnd, L"Der Kategoriename darf keine Tabulatoren oder Zeilenumbrüche enthalten.",
                L"Ungültiger Name", MB_OK | MB_ICONWARNING);
            return;
        }
        if (name == L"Alle Spiele" || name == L"Unsortiert" ||
            std::find(g_categories.begin(), g_categories.end(), name) != g_categories.end()) {
            MessageBoxW(hwnd, L"Diese Kategorie existiert bereits oder ist reserviert.",
                L"Kategorie", MB_OK | MB_ICONINFORMATION);
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

    void EndDrag(HWND hwnd) {
        if (GetCapture() == hwnd) ReleaseCapture();
        g_dragIdx = -1;
        g_dragging = false;
        g_dropChip = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void DeleteCategory(HWND hwnd, int index) {
        std::wstring name = g_categories[index];
        std::wstring question = L"Kategorie „" + name + L"“ löschen? Die Spiele darin werden wieder unsortiert.";
        if (MessageBoxW(hwnd, question.c_str(), L"Kategorie löschen", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        for (auto& game : g_games)
            if (game.category == name) game.category = L"Unsortiert";
        g_categories.erase(g_categories.begin() + index);
        g_active = 0;
        g_scroll = 0;
        RefreshGames();
        Relayout(hwnd);
        SaveSettings();
    }

    void ImportFromSteam(HWND hwnd) {
        ImportResult result = ImportSteamCategories();
        if (result.games > 0) {
            SaveSettings();
            RefreshGames();
            Relayout(hwnd);
        }
        g_importFound = result.games > 0;
        g_importText = g_importFound
            ? std::to_wstring(result.categories) + (result.categories == 1 ? L" Kategorie" : L" Kategorien")
            : L"Nichts Neues";
        g_importState = 2;
        g_importTick = GetTickCount64();
        SetTimer(hwnd, 2, 16, nullptr);
        InvalidateRect(hwnd, &g_L.btn[BTN_IMPORT], FALSE);
    }

    void Rescan(HWND hwnd) {
        if (g_scanState == 1) return;
        g_scanState = 1;
        g_scanReady = false;
        g_scanTick = GetTickCount64();
        SetTimer(hwnd, 1, 16, nullptr);
        std::thread([hwnd] {
            auto* found = new std::map<std::wstring, Game>(ScanSteamGames());
            PostMessageW(hwnd, WM_SCANDONE, 0, (LPARAM)found);
        }).detach();
    }

    void LaunchGame(HWND hwnd, const Game& game) {
        std::wstring uri = L"steam://rungameid/" + game.appId;
        HINSTANCE result = ShellExecuteW(hwnd, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)result <= 32)
            MessageBoxW(hwnd, L"Steam konnte das Spiel nicht starten.",
                L"Start fehlgeschlagen", MB_OK | MB_ICONERROR);
        else
            DestroyWindow(hwnd);
    }

    void LaunchRandom(HWND hwnd) {
        std::vector<const Game*> choices;
        std::wstring filter = SelectedCategory();
        for (const auto& game : g_games)
            if (!game.name.empty() && (filter == L"Alle Spiele" || game.category == filter))
                choices.push_back(&game);

        if (choices.empty()) {
            MessageBoxW(hwnd, L"In dieser Kategorie sind keine installierten Spiele.",
                L"Keine Spiele", MB_OK | MB_ICONINFORMATION);
            return;
        }

        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<size_t> pick(0, choices.size() - 1);
        LaunchGame(hwnd, *choices[pick(rng)]);
    }

    LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
        if (msg == WM_CHAR) {
            if (wp == VK_RETURN) {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_ACTION + BTN_CREATE, BN_CLICKED), 0);
                return 0;
            }
            if (wp == VK_ESCAPE) { SetFocus(GetParent(hwnd)); return 0; }
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    void UpdateHover(HWND hwnd, POINT p) {
        int row = HitRow(p), btn = HitBtn(p), chip = HitChip(p);
        bool play = InCover(row, p);
        if (row != g_hoverRow || btn != g_hoverBtn || chip != g_hoverChip || play != g_hoverPlay) {
            g_hoverRow = row; g_hoverBtn = btn; g_hoverChip = chip; g_hoverPlay = play;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_CREATE: {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            COLORREF caption = RGB(0, 0, 0), captionText = RGB(179, 179, 179);
            DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
            DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));

            g_editFont = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
            g_editBrush = CreateSolidBrush(RGB(42, 42, 42));
            g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                0, 0, 100, 20, hwnd, (HMENU)(INT_PTR)ID_EDIT, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
            SendMessageW(g_edit, EM_SETCUEBANNER, FALSE, (LPARAM)L"Neue Kategorie");
            SetWindowSubclass(g_edit, EditProc, 1, 0);

            RefreshGames();
            return 0;
        }

        case WM_SIZE:
            if (g_edit) Relayout(hwnd);
            return 0;

        case WM_GETMINMAXINFO: {
            RECT r{ 0, 0, S(760), S(520) };
            AdjustWindowRectEx(&r, WINDOW_STYLE, FALSE, 0);
            ((MINMAXINFO*)lp)->ptMinTrackSize = { r.right - r.left, r.bottom - r.top };
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            Paint(hwnd);
            return 0;

        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(42, 42, 42));
            return (LRESULT)g_editBrush;
        }

        case WM_MOUSEMOVE: {
            if (!g_tracking) {
                TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&t);
                g_tracking = true;
            }
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            if (g_dragIdx >= 0 && (wp & MK_LBUTTON)) {
                if (!g_dragging && (std::abs(p.x - g_dragStart.x) > S(6) || std::abs(p.y - g_dragStart.y) > S(6)))
                    g_dragging = true;
                if (g_dragging) {
                    g_dragPos = p;
                    int chip = HitChip(p);
                    g_dropChip = chip >= 1 ? chip : -1;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            UpdateHover(hwnd, p);
            return 0;
        }

        case WM_LBUTTONUP:
            if (g_dragging && g_dropChip >= 1 && g_dragIdx >= 0 && g_dragIdx < (int)g_visible.size()) {
                g_games[g_visible[g_dragIdx]].category = g_categories[g_dropChip];
                SaveSettings();
                EndDrag(hwnd);
                RefreshGames();
            } else {
                EndDrag(hwnd);
            }
            return 0;

        case WM_CAPTURECHANGED:
            if (g_dragIdx >= 0) { g_dragIdx = -1; g_dragging = false; g_dropChip = -1; InvalidateRect(hwnd, nullptr, FALSE); }
            return 0;

        case WM_RBUTTONUP: {
            int chip = HitChip({ (short)LOWORD(lp), (short)HIWORD(lp) });
            if (chip >= 2) DeleteCategory(hwnd, chip);
            return 0;
        }

        case WM_MOUSELEAVE:
            g_tracking = false;
            g_hoverRow = g_hoverBtn = g_hoverChip = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && g_dragging) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (LOWORD(lp) == HTCLIENT && (g_hoverRow >= 0 || g_hoverBtn >= 0 || g_hoverChip >= 0)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;

        case WM_MOUSEWHEEL:
            g_scroll -= GET_WHEEL_DELTA_WPARAM(wp) * g_L.rowH / WHEEL_DELTA;
            ClampScroll();
            {
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(hwnd, &p);
                UpdateHover(hwnd, p);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            POINT p{ (short)LOWORD(lp), (short)HIWORD(lp) };
            int chip = HitChip(p), btn = HitBtn(p), row = HitRow(p);
            if (chip >= 0) {
                g_active = chip;
                g_scroll = 0;
                RefreshGames();
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (btn >= 0) {
                SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_ACTION + btn, BN_CLICKED), 0);
            } else if (row >= 0) {
                g_sel = g_visible[row];
                InvalidateRect(hwnd, nullptr, FALSE);
                if (msg == WM_LBUTTONDBLCLK || InCover(row, p)) {
                    LaunchGame(hwnd, g_games[g_sel]);
                } else {
                    g_dragIdx = row;
                    g_dragStart = p;
                    SetCapture(hwnd);
                }
            }
            return 0;
        }

        case WM_KEYDOWN:
            if ((wp == VK_UP || wp == VK_DOWN) && !g_visible.empty()) {
                int current = -1;
                for (int i = 0; i < (int)g_visible.size(); ++i)
                    if (g_visible[i] == g_sel) current = i;
                int next = std::clamp(current + (wp == VK_DOWN ? 1 : -1), 0, (int)g_visible.size() - 1);
                g_sel = g_visible[next];
                EnsureVisible(next);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == ID_EDIT) {
                if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS)
                    InvalidateRect(hwnd, &g_L.pill, FALSE);
                return 0;
            }
            switch (id - ID_ACTION) {
            case BTN_CREATE: AddCategory(hwnd); return 0;
            case BTN_SCAN: Rescan(hwnd); return 0;
            case BTN_IMPORT: ImportFromSteam(hwnd); return 0;
            case BTN_LAUNCH: LaunchRandom(hwnd); return 0;
            }
            break;
        }

        case WM_COVER:
            OnCoverDownloaded(hwnd, (unsigned long long)wp);
            return 0;

        case WM_SCANDONE: {
            auto* found = (std::map<std::wstring, Game>*)lp;
            MergeGames(std::move(*found));
            delete found;
            if (g_active >= (int)g_categories.size()) g_active = 0;
            g_sel = -1;
            RefreshGames();
            Relayout(hwnd);
            QueueCovers();
            g_scanReady = true;
            return 0;
        }

        case WM_TIMER:
            if (wp == 2) {
                if (GetTickCount64() - g_importTick >= 2200) {
                    g_importState = 0;
                    KillTimer(hwnd, 2);
                }
                InvalidateRect(hwnd, &g_L.btn[BTN_IMPORT], FALSE);
            }
            if (wp == 1) {
                ULONGLONG now = GetTickCount64();
                if (g_scanState == 1 && g_scanReady && now - g_scanTick >= 700) {
                    g_scanState = 2;
                    g_checkTick = now;
                } else if (g_scanState == 2 && now - g_checkTick >= 1800) {
                    g_scanState = 0;
                    KillTimer(hwnd, 1);
                }
                InvalidateRect(hwnd, &g_L.btn[BTN_SCAN], FALSE);
            }
            return 0;

        case WM_DESTROY:
            g_quit = true;
            SaveSettings();
            if (g_editFont) DeleteObject(g_editFont);
            if (g_editBrush) DeleteObject(g_editBrush);
            for (auto& entry : g_covers) delete entry.second;
            g_covers.clear();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    HDC screen = GetDC(nullptr);
    g_scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, screen);

    LoadSettings();
    ScanAndMerge();
    g_coverDir = GetAppDataFolder() + L"\\covers";
    CreateDirectoryW(g_coverDir.c_str(), nullptr);

    const wchar_t CLASS_NAME[] = L"SteamGameRandomizerWindow";
    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);

    RECT frame{ 0, 0, S(780), S(640) };
    AdjustWindowRectEx(&frame, WINDOW_STYLE, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Steam Game Randomizer",
        WINDOW_STYLE, CW_USEDEFAULT, CW_USEDEFAULT,
        frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        GdiplusShutdown(gdiToken);
        CoUninitialize();
        return 1;
    }
    g_mainWnd = hwnd;
    Relayout(hwnd);
    QueueCovers();

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(gdiToken);
    CoUninitialize();
    return (int)msg.wParam;
}
