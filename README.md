# Werwolf

*Werwolf* als 3D-Multiplayer-Spiel: Man läuft in der dritten Person durch ein Dorf, redet im
Nähe-Chat, beobachtet, versammelt sich am Feuer zur Abstimmung – und nachts verwandeln sich die
Werwölfe und gehen auf die Jagd. Alles läuft auf einer eigenen, von Grund auf geschriebenen
C++20-Engine ohne externe Bibliotheken; Welt, Gebäude und Figuren werden prozedural erzeugt.

## Bauen

Voraussetzung: Visual Studio 2022/2026 mit dem Workload „Desktopentwicklung mit C++“
(enthält MSVC, CMake und Ninja).

```powershell
.\build.ps1 -Preset release       # optimierter Build (empfohlen zum Spielen)
.\build.ps1 -Preset release -Run  # bauen und starten
.\build.ps1 -Test                 # Debug-Build + alle Tests
```

Die Programme landen in `build\<preset>\bin\`:

| Programm             | Zweck                                                     |
| -------------------- | --------------------------------------------------------- |
| `werwolf.exe`        | Spiel (kann auch selbst einen Server hosten)              |
| `werwolf_server.exe` | Dedizierter Server ohne Grafik (Konsole)                  |
| `test_logic.exe`     | Regeltests + komplette Bot-Partien in Echtzeit            |
| `test_network.exe`   | End-to-End-Test über echtes TCP                           |

## Spielen

1. Eine Person klickt **Spiel hosten**; die Lobby zeigt die IP-Adressen für die anderen.
2. Alle anderen tragen die Adresse ein und klicken **Beitreten**.
3. Die Spielleitung stellt Rollen und Phasenlängen ein, füllt mit **Bots** auf und startet.

Port **27777 (TCP)**. Beim ersten Hosten fragt die Windows-Firewall – Zugriff erlauben. Fürs Internet
den Port im Router weiterleiten oder ein VPN (Tailscale, Radmin) nutzen. 4 bis 16 Spieler.

**Dorf erkunden** im Hauptmenü startet eine Offline-Runde zum Umsehen (ohne Spielregeln).

### Steuerung

| Taste            | Aktion                                                         |
| ---------------- | -------------------------------------------------------------- |
| WASD             | laufen, **Shift** rennen                                        |
| Maus / Mausrad   | umsehen / Kamera-Abstand                                        |
| **E**            | Hauptaktion auf die angeschaute Person (abstimmen, Vision, …)   |
| **F**            | Gifttrank (Hexe)                                                |
| **R**            | verwandeln (Werwolf, nur nachts)                                |
| Linksklick       | reißen (Werwolf in Bestiengestalt)                              |
| Enter            | Chat schreiben – **Tab** wechselt für Werwölfe in den Rudel-Chat |
| Esc              | Pause / Maus freigeben                                          |
| F11, F12         | Vollbild, Screenshot                                            |

### Ablauf

* **Tag** – frei herumlaufen, Spuren suchen, reden. Gesprochen wird im **Nähe-Chat** (24 m Hörweite).
  Ab Tag 2 kann jeder an der **Glocke** auf dem Dorfplatz eine Versammlung einberufen.
* **Versammlung** – alle sitzen am Feuer und hören sich gegenseitig. Danach folgt die
  **Abstimmung**: Person anschauen und **E** drücken (oder in der Stimmenliste klicken).
* **Nacht** – dunkel; wer lebt, trägt eine Laterne. Werwölfe verwandeln sich (R), reißen ein Opfer
  pro Nacht und müssen aufpassen, dabei nicht gesehen zu werden. Sonderrollen handeln nachts.
* **Morgen** – die Toten der Nacht werden verkündet; Leichen liegen, wo sie gefallen sind.

Tote werden zu **Geistern**: sie schweben unsichtbar umher, sehen alle Rollen, lesen den Rudel-Chat
mit und sprechen im Geisterchat.

### Rollen

| Rolle        | Fähigkeit                                                                      |
| ------------ | ------------------------------------------------------------------------------ |
| Dorfbewohner | beobachten, Hinweise sammeln, die Werwölfe überführen                          |
| Werwolf      | nachts verwandeln (R) und ein Opfer reißen (Linksklick); geheimer Rudel-Chat   |
| Seherin      | einmal pro Nacht die Rolle einer Person in Sichtweite sehen (E)                |
| Hexe         | Heiltrank holt ein frisches Opfer zurück (E an der Leiche), Gifttrank tötet (F) |
| Jäger        | nach dem Tod 15 Sekunden für einen letzten Schuss (E)                          |
| Amor         | in der ersten Nacht zwei Spieler verlieben (E) – stirbt einer, stirbt auch der andere |
| Beschützer   | jede Nacht eine Person in der Nähe schützen (E), nie zweimal hintereinander     |

**Sieg:** Das Dorf gewinnt, wenn kein Werwolf mehr lebt; die Werwölfe, sobald sie mindestens so
viele sind wie alle anderen; ein gemischtes Liebespaar, wenn nur noch die beiden leben.

## Auto-Updater

Vor dem Hauptmenü prüft das Spiel, ob es eine neuere Version gibt, lädt sie herunter, prüft jede
Datei per SHA-256, ersetzt sich selbst und startet neu. Ohne Internet oder bei Problemen geht es
nach einer kurzen Meldung normal ins Hauptmenü. Läuft das Spiel direkt aus dem Build-Ordner
(Entwicklung), wird nichts installiert.

Die Updates kommen standardmäßig von GitHub Releases:
`https://github.com/GG2EZBoOm/werwolf/releases/latest/download/update.json`
(änderbar über die CMake-Variable `WERWOLF_UPDATE_URL` oder `update_url=` in der config.txt).
Das Repository muss **öffentlich** sein, damit das Spiel die Dateien ohne Anmeldung laden kann.

**Neue Version veröffentlichen:**

1. Version in `CMakeLists.txt` erhöhen (`project(Werwolf VERSION 0.3.0 …)`).
2. `.\tools\make_release.ps1 -Notes "Was ist neu"` ausführen – baut das Spiel und legt
   `werwolf.exe`, `werwolf_server.exe` und `update.json` in `dist\<version>\` ab.
3. Auf GitHub ein Release mit dem Tag `v<version>` anlegen und die drei Dateien als Assets hochladen.

Spieler mit älterer Version bekommen das Update beim nächsten Start automatisch.

## Architektur

```
engine/                 eigene Engine
  core/                 Log, Zeit, UTF-8, Zufall, 3D-Mathematik, Serialisierung, PNG-Export,
                        SHA-256, JSON
  net/                  nicht blockierendes TCP mit Nachrichten-Framing, HTTPS-Download (WinHTTP)
  platform/win32/       Fenster, Eingabe (inkl. roher Maus), DPI, OpenGL-3.3-Kontext
  gfx/                  GL-Loader, 3D-Renderer (Schattenkarte, Punktlichter, Nebel, Himmel mit
                        Sonne/Mond/Sternen, MSAA, HDR, Bloom, Tonemapping, Color-Grading,
                        Partikel), prozedurale Meshes, 2D-Renderer, Schriftatlas
  ui/                   Immediate-Mode-UI
game/
  world/                deterministisch erzeugtes Dorf (Gelände, Häuser, Kapelle, Wald, Felder),
                        Kollision, Bewegung
  logic/                Echtzeit-Regelwerk (Match) – testbar ohne Netz und Grafik
  protocol/             Nachrichten Client <-> Server
  server/               autoritativer Server, Nähe-Chat, Bots mit Tagesroutine, Verdacht und Zeugen
  client/               Netzwerk-Client mit Interpolation, 3D-Spielansicht, HUD, Menüs, Updater
  update/               Update-Kern: Manifest, Versionen, sicheres Ersetzen mit Rollback
tests/                  Regel-, Netzwerk- und Updater-Tests
tools/                  make_release.ps1 (Release für den Auto-Updater erzeugen)
```

Der Server ist autoritativ: Clients melden ihre Position (20×/s, wird auf Tempo geprüft) und ihre
Absichten; der Server prüft Reichweite, Sichtlinie und Regeln und schickt jedem eine persönliche
Ansicht. Geheimes (Rollen, Rudel-Chat, Geister) verlässt den Server nur an Berechtigte.

## Entwickleroptionen

```powershell
werwolf.exe --host --bots 7 --start --timescale 3   # sofort eine Bot-Runde im Zeitraffer
werwolf.exe --join 127.0.0.1 --name Max              # zweites Fenster tritt bei
werwolf.exe --autoplay                               # eigene Figur läuft/handelt automatisch
werwolf.exe --sandbox [--tod 0.9] [--wolf]           # offline herumlaufen (Tageszeit, Wolfsgestalt)
werwolf.exe --preview --camera 2 --tod 0.75          # Kameravorschau des Dorfs
werwolf.exe --screenshots C:\temp\shots --shot-every 2 --quit-after 30
werwolf.exe --no-update                              # Update-Prüfung überspringen
werwolf.exe --update-url <url> --force-update        # Updater mit anderer Quelle testen
```

Logdatei: `%APPDATA%\Werwolf\werwolf.log`, Einstellungen: `%APPDATA%\Werwolf\config.txt`.
