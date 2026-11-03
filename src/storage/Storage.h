#pragma once

#include <string>

#include "util/Fs.h"

namespace playr {

extern std::wstring g_dataDir;
extern std::wstring g_coverDir;

fs::path SettingsPath();
fs::path TypesPath();
fs::path OwnedPath();
fs::path CoverPath(const std::wstring& appId);
fs::path PortraitPath(const std::wstring& appId);

// Sets up %APPDATA%\PLAYR-Launcher (taking over the folder of the old name) and its cover folder.
void InitStorage();

void SaveSettings();
void LoadSettings();
void SaveTypes();
void LoadTypes();
void SaveOwned();
void LoadOwned();

}  // namespace playr
