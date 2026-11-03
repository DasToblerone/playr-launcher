#include "util/Text.h"

#include <fstream>

namespace playr {

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

std::wstring Utf8ToWide(const std::string& bytes) {
    if (bytes.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), (int)bytes.size(), result.data(), size);
    return result;
}

}  // namespace playr
