# Steam Game Randomizer

Eine kleine Windows-App, die deine installierten Steam-Spiele zeigt und dir hilft, eins auszuwählen. Die Oberfläche ist an Spotifys Darkmode angelehnt.

- Liest die installierten Spiele aus allen Steam-Bibliotheken, mit Titelbild, Spielzeit, zuletzt gespielt und Größe
- Eigene Kategorien wie Playlists, auch aus deinen Steam-Sammlungen importierbar
- Sortieren, Listen- und Kartenansicht, zufälliges Spiel auswählen

Geschrieben in C++ mit der Win32-API und GDI+, ohne weitere Abhängigkeiten. Einstellungen und Bilder liegen unter `%APPDATA%\GameRandomizer`.

## Selbst bauen

Du brauchst Windows 10 oder 11 und den C++-Compiler von Microsoft.

1. **Compiler installieren** (einmalig, etwa 3 GB): die „Build Tools für Visual Studio“ von [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads/) mit dem Workload „Desktopentwicklung mit C++“, oder in einer Eingabeaufforderung:
   ```
   winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
2. **Quellcode holen:** auf GitHub „Code“, dann „Download ZIP“ und entpacken, oder `git clone https://github.com/DasToblerone/steam-game-randomizer`.
3. **`build.bat` doppelklicken.** Nach etwa einer halben Minute liegt `SteamGameRandomizer.exe` im selben Ordner.

Ohne das Skript, in der „x64 Native Tools Command Prompt für VS“ im Ordner des Quellcodes:

```
rc /nologo SteamGameRandomizer.rc
cl /EHsc /O2 /std:c++17 /utf-8 /MT SteamGameRandomizer.cpp /Fe:SteamGameRandomizer.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib SteamGameRandomizer.res
```
