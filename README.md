# Steam Game Randomizer

Startet ein zufälliges installiertes Steam-Spiel. Die Oberfläche ist an Spotifys Darkmode angelehnt: fast schwarz, lila Akzent, Arial.

- Erkennt installierte Spiele automatisch aus allen Steam-Bibliotheken
- Titelbilder kommen von Steam und werden lokal zwischengespeichert
- Eigene Kategorien: Spiele per Drag and Drop auf eine Kategorie ziehen, Rechtsklick auf eine Kategorie löscht sie
- „Aus Steam holen“ importiert deine Steam-Sammlungen (nur die mit installierten Spielen)
- Ein Klick auf das Titelbild startet genau dieses Spiel, „Zufällig starten“ wählt eins aus der aktuellen Kategorie

## Bauen

Windows, Visual Studio mit C++-Werkzeugen (Developer Command Prompt):

```
rc /nologo SteamGameRandomizer.rc
cl /EHsc /O2 /std:c++17 /utf-8 /MT SteamGameRandomizer.cpp /Fe:SteamGameRandomizer.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib SteamGameRandomizer.res
```

Einstellungen und Cover liegen unter `%APPDATA%\GameRandomizer`.
