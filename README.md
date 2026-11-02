# Steam Game Randomizer

Eine kleine Windows-App, die deine installierten Steam-Spiele zeigt und dir hilft, eins auszuwählen. Die Oberfläche ist an Spotifys Darkmode angelehnt.

![Listenansicht](screenshots/liste.png)

![Kartenansicht](screenshots/karten.png)

*Screenshots von Version 1.6.0.*

- Liest die installierten Spiele aus allen Steam-Bibliotheken, mit Titelbild, Spielzeit, zuletzt gespielt, Größe und Erfolgsfortschritt
- Zeigt auch Spiele, die du besitzt, aber nicht installiert hast, grau und ohne Start-Button
- Eigene Kategorien wie Playlists, auch aus deinen Steam-Sammlungen importierbar
- Suche, Sortieren, Listen- und Kartenansicht, zufälliges Spiel auswählen

Geschrieben in C++ mit der Win32-API und GDI+, ohne weitere Abhängigkeiten. Einstellungen und Bilder liegen unter `%APPDATA%\GameRandomizer`.

## Selbst bauen

Du brauchst Windows 10 oder 11 und den C++-Compiler von Microsoft.

1. **Compiler installieren** (einmalig, etwa 3 GB): die „Build Tools für Visual Studio“ von [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads/) mit dem Workload „Desktopentwicklung mit C++“, oder in einer Eingabeaufforderung:
   ```
   winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
2. **Quellcode holen:** auf GitHub „Code“, dann „Download ZIP“ und entpacken, oder `git clone https://github.com/DasToblerone/steam-game-randomizer`.
3. **Bauen:** im Startmenü die „x64 Native Tools Command Prompt für VS“ öffnen, mit `cd` in den Ordner des Quellcodes wechseln und diese zwei Befehle eingeben. Nach etwa einer halben Minute liegt `SteamGameRandomizer.exe` im selben Ordner.

```
rc /nologo SteamGameRandomizer.rc
cl /EHsc /O2 /std:c++17 /utf-8 /MT SteamGameRandomizer.cpp /Fe:SteamGameRandomizer.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib SteamGameRandomizer.res
```
