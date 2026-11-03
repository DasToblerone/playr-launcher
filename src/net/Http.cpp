#include "net/Http.h"

#include <fstream>
#include <map>

#include "platform/Win32.h"
#include <winhttp.h>

namespace playr {

namespace {

// Each thread keeps its WinHTTP session and one connection per host. Opening a new TLS connection for every image
// costs more than the download itself, and reusing it makes the following requests nearly free.
struct Connections {
    HINTERNET session = nullptr;
    std::map<std::wstring, HINTERNET> hosts;

    ~Connections() {
        for (auto& host : hosts) WinHttpCloseHandle(host.second);
        if (session) WinHttpCloseHandle(session);
    }

    HINTERNET Connect(const std::wstring& host) {
        if (!session) {
            session = WinHttpOpen(L"PLAYR-Launcher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session) return nullptr;
            WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);
        }
        auto it = hosts.find(host);
        if (it != hosts.end()) return it->second;
        HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connect) hosts[host] = connect;
        return connect;
    }

    void Drop(const std::wstring& host) {
        auto it = hosts.find(host);
        if (it == hosts.end()) return;
        WinHttpCloseHandle(it->second);
        hosts.erase(it);
    }
};

thread_local Connections t_connections;

enum class Result { Ok, Failed, HttpError };

Result Request(const std::wstring& host, const std::wstring& target, const ByteSink& sink) {
    HINTERNET connect = t_connections.Connect(host);
    if (!connect) return Result::Failed;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) return Result::Failed;

    Result result = Result::Failed;
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            std::vector<char> buffer(16384);
            DWORD read = 0;
            result = Result::Ok;
            while (WinHttpReadData(request, buffer.data(), (DWORD)buffer.size(), &read) && read)
                sink(buffer.data(), read);
        } else {
            result = Result::HttpError;
        }
    }
    WinHttpCloseHandle(request);
    return result;
}

}  // namespace

// Fetches an https URL and streams a 200 response body into `sink`.
bool HttpGet(const std::wstring& url, const ByteSink& sink) {
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

    Result result = Request(host, target, sink);
    if (result == Result::Failed) {  // the server may have closed a kept-alive connection: reconnect once
        t_connections.Drop(host);
        result = Request(host, target, sink);
    }
    return result == Result::Ok;
}

bool HttpGetToFile(const std::wstring& url, const fs::path& out) {
    std::ofstream file(out, std::ios::binary | std::ios::trunc);
    bool ok = HttpGet(url, [&](const char* data, DWORD size) { file.write(data, size); });
    file.close();
    return ok && !file.fail();
}

}  // namespace playr
