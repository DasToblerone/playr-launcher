# PLAYR-Launcher

Eine kleine Windows-App, die deine installierten Steam-Spiele als Bibliothek zeigt. Du startest ein Spiel mit einem Klick oder lässt dir eins vorschlagen. Die Oberfläche ist an einen dunklen Musik-Player mit Playlists angelehnt.

![Listenansicht](screenshots/liste.png)

![Kartenansicht](screenshots/karten.png)

*Screenshots von Version 1.6.0 mit Demodaten und erfundenen Spielen.*

- Liest die installierten Spiele aus allen Steam-Bibliotheken, mit Titelbild, Spielzeit, zuletzt gespielt, Größe und Erfolgsfortschritt
- Zeigt auch Spiele, die du besitzt, aber nicht installiert hast, grau und ohne Start-Button
- Eigene Kategorien wie Playlists, auch aus deinen Steam-Sammlungen importierbar
- Suche, Sortieren, Listen- und Kartenansicht, zufälliges Spiel auswählen

Geschrieben in C++ mit der Win32-API und GDI+, ohne weitere Abhängigkeiten. Einstellungen und Bilder liegen unter `%APPDATA%\PLAYR-Launcher`. Die App liest Steams Dateien nur und schreibt nur in diesen Ordner.

## Datenschutz

Die App hat keinen Server, kein Konto und keine Telemetrie, der Entwickler erhält keine Daten. Für Titelbilder und Spielinformationen ruft sie den Steam-Store und das Steam-CDN ab, dabei sehen deren Betreiber deine IP-Adresse und die App-IDs deiner Spiele. Details stehen in der [Datenschutzerklärung](PRIVACY.md).

## Hinweise

- PLAYR-Launcher ist ein unabhängiges Projekt und steht in keiner Verbindung zu Valve. „Steam“ ist eine Marke der Valve Corporation, Titelbilder und Spielnamen gehören ihren jeweiligen Inhabern. Die Bilder werden zur Laufzeit von Steam geladen und nicht mit der App verteilt.
- Die fertige `.exe` ist nicht signiert. Windows SmartScreen oder dein Virenscanner können deshalb warnen.
- Lizenz: [MIT](LICENSE). Das Projekt ist kostenlos, quelloffen und nicht kommerziell. Fragen und Fehler gern als [Issue](https://github.com/DasToblerone/playr-launcher/issues).
