#pragma once

#include <string>

namespace playr {

// Finds `"key" "value"` after `from` and returns the value. `end` receives the position after it.
std::wstring VdfValue(const std::wstring& text, const std::wstring& key, size_t from = 0, size_t* end = nullptr);

// Reads a JSON string body starting right after the opening quote and decodes its escapes.
std::wstring ReadJsonString(const std::wstring& text, size_t pos);

// Position of the brace that closes the block opened at `open`.
size_t BlockEnd(const std::wstring& text, size_t open);

}  // namespace playr
