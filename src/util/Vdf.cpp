#include "util/Vdf.h"

namespace playr {

namespace {

// Reads the quoted value that follows a quoted key and collapses VDF's doubled backslashes.
std::wstring UnescapeVdf(const std::wstring& value) {
    std::wstring out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'\\' && i + 1 < value.size() && value[i + 1] == L'\\') ++i;
        out += value[i];
    }
    return out;
}

}  // namespace

// Finds `"key" "value"` after `from` and returns the value. `end` receives the position after it.
std::wstring VdfValue(const std::wstring& text, const std::wstring& key, size_t from, size_t* end) {
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

// Position of the brace that closes the block opened at `open`.
size_t BlockEnd(const std::wstring& text, size_t open) {
    int depth = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == L'{') ++depth;
        else if (text[i] == L'}' && --depth == 0) return i;
    }
    return text.size();
}

}  // namespace playr
