#pragma once

#include <functional>
#include <iosfwd>
#include <string>

#include "platform/Win32.h"
#include "util/Fs.h"

namespace playr {

std::wstring KnownFolder(REFKNOWNFOLDERID id);
std::wstring RegistryString(HKEY root, const wchar_t* subkey, const wchar_t* valueName);

// Reads a UTF-8 file (with or without BOM) as wide text; empty when it cannot be read.
std::wstring ReadTextFile(const fs::path& path);
void WriteUtf8(std::ofstream& file, const std::wstring& value);
std::wstring Utf8ToWide(const std::string& bytes);

// Calls `handle` with every line of a text file, without the line break.
void ForEachLine(const std::wstring& text, const std::function<void(const std::wstring&)>& handle);

}  // namespace playr
