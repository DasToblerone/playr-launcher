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

Die Releases sind noch nicht mit einem Zertifikat signiert. Windows' Smart App Control blockt unsignierte Programme ohne Ruf, und dafür gibt es keine Ausnahme pro Programm. Die Signatur ist in Arbeit, siehe [CODE_SIGNING.md](CODE_SIGNING.md). Bis dahin hast du zwei Möglichkeiten:

- Das Tool selbst bauen, die Anleitung steht unten. Selbst gebaute Programme wirft Smart App Control allerdings auch hinaus, wenn sie unsigniert sind. Dann hilft nur das Ausschalten von Smart App Control (Windows-Sicherheit, App- und Browsersteuerung), das sich danach nicht wieder einschalten lässt.
- Bei SmartScreen („Der Computer wurde durch Windows geschützt“) auf „Weitere Informationen“ und dann „Trotzdem ausführen“ klicken.

## Datenschutz

Das Tool sendet keine persönlichen Daten und hat keine Telemetrie. Es liest nur Dateien von Steam auf deinem Rechner und fragt Steams öffentliche Server nach Titelbildern und danach, ob eine App ein Spiel ist. Details stehen in [CODE_SIGNING.md](CODE_SIGNING.md).

## Bauen

Windows, Visual Studio mit C++-Werkzeugen (Developer Command Prompt):

```
rc /nologo SteamGameRandomizer.rc
cl /EHsc /O2 /std:c++17 /utf-8 /MT SteamGameRandomizer.cpp /Fe:SteamGameRandomizer.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib SteamGameRandomizer.res
```
