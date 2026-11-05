# Datenschutzerklärung

Stand: November 2026

PLAYR-Launcher ist ein Programm, das lokal auf deinem Rechner läuft. Es gibt keinen Server des Entwicklers, kein Benutzerkonto, keine Telemetrie und keine Statistik. **Der Entwickler erhält keine Daten von dir.**

Dies ist ein privates, kostenloses und nicht kommerzielles Open-Source-Projekt. Ansprechpartner ist der Besitzer des Repositorys (GitHub-Nutzer DasToblerone), erreichbar über die [Issues](https://github.com/DasToblerone/playr-launcher/issues).

## Was die App auf deinem Rechner liest

Die App liest ausschließlich lesend aus dem Ordner deiner Steam-Installation:

- die Liste deiner Bibliotheksordner und der installierten Spiele,
- den Namen und die SteamID64 des zuletzt angemeldeten Steam-Kontos sowie sein Profilbild aus dem Steam-Cache,
- Spielzeit, zuletzt gespielt und Erfolgsstände aus Steams lokalen Dateien,
- deine Steam-Sammlungen, wenn du „Steam-Sammlungen“ benutzt.

Diese Daten bleiben auf deinem Rechner und werden nur zur Anzeige verwendet. Name und SteamID64 werden nicht ins Netz gesendet.

## Was die App speichert

Unter `%APPDATA%\PLAYR-Launcher` legt die App ab:

- `settings.txt`: deine Kategorien, Zuordnungen, Sortierung und die Filter des Würfels,
- `types.txt` und `owned.txt`: zwischengespeicherte Auskünfte des Steam-Stores (ob eine App ein Spiel ist, Namen nicht installierter Spiele),
- `covers\`: heruntergeladene Titelbilder.

Zum Löschen entfernst du diesen Ordner.

## Verbindungen ins Internet

Die App ruft von sich aus nur zwei Dienste ab, um Titelbilder und Spielinformationen zu laden:

1. **Steam Store** (`store.steampowered.com`, Valve Corporation): Dabei wird die **App-ID** eines Spiels übertragen, um Typ und Namen zu erfragen. Da das eine normale Internetverbindung ist, sieht Valve auch deine IP-Adresse. Auf dem Weg dorthin werden also App-IDs deiner Bibliothek übertragen, einschließlich Spielen, die du besitzt, aber nicht installiert hast.
2. **Steam-CDN** (`cdn.cloudflare.steamstatic.com` und weitere Steam-Bildserver, betrieben mit Cloudflare): Dort werden die Titelbilder abgerufen. Auch hier sehen die Betreiber deine IP-Adresse und die angefragte App-ID.

Welche Daten Valve und Cloudflare dabei verarbeiten, richtet sich nach deren eigenen Datenschutzerklärungen:
[Valve](https://store.steampowered.com/privacy_agreement/), [Cloudflare](https://www.cloudflare.com/privacypolicy/).

Rechtsgrundlage für diese Abrufe ist dein Wunsch, die App zu benutzen (Art. 6 Abs. 1 lit. b DSGVO). Du kannst die Verbindungen mit einer Firewall-Regel für `PLAYR-Launcher.exe` blockieren. Die App funktioniert dann mit bereits gespeicherten Bildern weiter, zeigt aber für neue Spiele keine Titelbilder.

Wenn du ein Spiel startest, übergibt die App den Link `steam://rungameid/<App-ID>` an Windows, und der Steam-Client startet das Spiel.

## Dieses GitHub-Repository und Downloads

Der Quellcode und die Downloads liegen auf GitHub (GitHub, Inc. / Microsoft). Beim Besuch des Repositorys oder beim Herunterladen verarbeitet GitHub technische Daten wie deine IP-Adresse. Darauf hat der Entwickler keinen Einfluss; siehe die [Datenschutzerklärung von GitHub](https://docs.github.com/de/site-policy/privacy-policies/github-general-privacy-statement).

## Deine Rechte

Da der Entwickler keine personenbezogenen Daten von dir verarbeitet, gibt es bei ihm nichts zu löschen oder auszukunftern. Bei Fragen eröffne ein [Issue](https://github.com/DasToblerone/playr-launcher/issues). Gegenüber Valve, Cloudflare und GitHub kannst du deine Rechte nach der DSGVO bei diesen Unternehmen geltend machen. Du hast außerdem das Recht, dich bei einer Datenschutz-Aufsichtsbehörde zu beschweren.
