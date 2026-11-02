# Steam Game Randomizer

Zeigt deine installierten Steam-Spiele und hilft dir, eins auszuwählen. Die Oberfläche ist an Spotifys Darkmode angelehnt: fast schwarz, lila Akzent, Arial.

## Anleitung

**Start**
Steam muss installiert sein. Beim Start liest das Tool alle Steam-Bibliotheken ein, auch auf anderen Laufwerken. Steam selbst muss nicht laufen, nur zum Starten eines Spiels.

**Oben**
In der Leiste ganz oben stehen dein Steam-Profilbild, dein Name und die Zahl der installierten Spiele.

**Ein Spiel starten**
- Fahr über ein Spiel und klick auf den Play-Button am Titelbild (in der Kartenansicht der runde Button unten rechts am Cover). Ein Doppelklick auf das Spiel startet es auch.
- „Zufällig auswählen“ unten rechts markiert ein zufälliges Spiel der geöffneten Kategorie. Gestartet wird es erst, wenn du es selbst startest. Ein weiterer Klick wählt ein anderes.

**Kategorien**
- Links in der Seitenleiste „Bibliothek“ stehen „Alle Spiele“ und deine Kategorien wie Playlists. Ein Klick öffnet eine Kategorie.
- „+ Erstellen“ legt eine neue Kategorie an. Tipp den Namen und bestätige mit Enter, mit Esc bleibt der Vorschlag „Neue Kategorie“.
- Zieh ein Spiel auf eine Kategorie in der Seitenleiste, um es hinzuzufügen. Das kopiert: Das Spiel bleibt in „Alle Spiele“ und kann in beliebig vielen Kategorien sein.
- Rechtsklick auf eine Kategorie: Umbenennen oder Löschen. Beim Löschen bleiben die Spiele erhalten.
- Rechtsklick auf ein Spiel in einer Kategorie: „Aus Kategorie entfernen“. Das Spiel bleibt in der Bibliothek.
- „Kategorien aus Steam“ unten holt deine Steam-Sammlungen als Kategorien, aber nur die, in denen installierte Spiele stecken.

**Sortieren**
Der Button oben rechts im Spielebereich sortiert nach Name, Spielzeit, Zuletzt gespielt oder Größe. Wählst du dieselbe Sortierung noch einmal, dreht sich die Richtung um. Die Auswahl wird gemerkt.

**Ansichten**
Bei schmalem Fenster zeigt das Tool eine Liste. Wird das Fenster breit genug, etwa maximiert, wechselt es zu großen Karten mit Hochformat-Cover. F11 schaltet Vollbild um, Esc beendet es. Mit den Pfeiltasten bewegst du die Auswahl.

**Neu scannen**
„Neu scannen“ unten liest die Steam-Bibliotheken neu ein, zum Beispiel nach einer Installation. Spielzeit und Größe werden dabei aktualisiert.

**Gut zu wissen**
- Steams Hilfsprogramme und Tools wie Wallpaper Engine werden ausgeblendet. Das Tool fragt dafür beim ersten Mal die Steam-Store-API und merkt sich das Ergebnis.
- Spielzeit und „Zuletzt gespielt“ stammen aus Steams lokaler Konfiguration, die Steam nicht sofort schreibt.
- Titelbilder lädt das Tool von Steam und speichert sie lokal.
- Kategorien, Sortierung und Zwischenspeicher liegen unter `%APPDATA%\GameRandomizer`. Zum Zurücksetzen kannst du den Ordner löschen.

## Windows meldet Smart App Control oder SmartScreen

Die Releases sind noch nicht mit einem Zertifikat signiert, deshalb warnt Windows.

- **SmartScreen** („Der Computer wurde durch Windows geschützt“): auf „Weitere Informationen“ und dann „Trotzdem ausführen“ klicken. Alternativ das Tool selbst bauen, siehe unten, denn eine selbst gebaute Datei trägt keine Download-Markierung und löst die Warnung nicht aus.
- **Smart App Control** blockt unsignierte Programme ohne Ruf und hat keine Ausnahme pro Programm. Auch eine selbst gebaute exe ist unsigniert und wird dort meist ebenfalls blockiert. Dann hilft nur, Smart App Control in der Windows-Sicherheit unter „App- und Browsersteuerung“ auszuschalten.

## Datenschutz

Das Tool sendet keine persönlichen Daten und hat keine Telemetrie. Es liest nur Dateien von Steam auf deinem Rechner und fragt Steams öffentliche Server nach Titelbildern und danach, ob eine App ein Spiel ist.

## Selbst bauen

Du brauchst Windows 10 oder 11 und den C++-Compiler von Microsoft. Das Tool hat keine weiteren Abhängigkeiten.

1. **Compiler installieren** (einmalig, etwa 3 GB). Entweder die „Build Tools für Visual Studio“ von [visualstudio.microsoft.com](https://visualstudio.microsoft.com/downloads/) mit dem Workload „Desktopentwicklung mit C++“, oder in einer Eingabeaufforderung:
   ```
   winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
2. **Quellcode holen:** auf GitHub „Code“, dann „Download ZIP“ und entpacken. Mit Git geht auch `git clone https://github.com/DasToblerone/steam-game-randomizer`.
3. **`build.bat` doppelklicken.** Das Skript findet Visual Studio selbst und baut die exe, nach etwa einer halben Minute liegt `SteamGameRandomizer.exe` im selben Ordner.

Fehlt der Compiler, sagt das Skript es dir und nennt den Installationsbefehl. Wer die Befehle selbst eingeben will, öffnet die „x64 Native Tools Command Prompt für VS“ im Ordner des Quellcodes:

```
rc /nologo SteamGameRandomizer.rc
cl /EHsc /O2 /std:c++17 /utf-8 /MT SteamGameRandomizer.cpp /Fe:SteamGameRandomizer.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib SteamGameRandomizer.res
```
