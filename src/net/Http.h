#pragma once

#include <functional>
#include <string>

#include "util/Fs.h"

namespace playr {

using ByteSink = std::function<void(const char*, unsigned long)>;

// Fetches an https URL and streams a 200 response body into `sink`.
bool HttpGet(const std::wstring& url, const ByteSink& sink);
bool HttpGetToFile(const std::wstring& url, const fs::path& out);

}  // namespace playr
