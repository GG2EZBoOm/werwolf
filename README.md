# Werwolf

Das Gesellschaftsspiel *Werwolf* als Multiplayer-Computerspiel – mit einer eigenen, von Grund auf
geschriebenen C++20-Engine. Keine externen Bibliotheken: Fenster, OpenGL-Loader, Schriftrendering,
UI, Netzwerk und Spiellogik sind komplett selbst gebaut.

## Bauen

Voraussetzung: Visual Studio 2022/2026 mit dem Workload „Desktopentwicklung mit C++“
(enthält MSVC, CMake und Ninja).

```powershell
.\build.ps1              # Debug-Build
.\build.ps1 -Test        # bauen und alle Tests ausführen
.\build.ps1 -Run         # bauen und Spiel starten
.\build.ps1 -Preset release
```

Alternativ den Ordner direkt in Visual Studio öffnen (CMake-Projekt) und `werwolf.exe` starten.

Die Programme landen in `build\<preset>\bin\`:

| Programm             | Zweck                                                     |
| -------------------- | --------------------------------------------------------- |
| `werwolf.exe`        | Grafischer Client (kann auch selbst einen Server hosten)  |
| `werwolf_server.exe` | Dedizierter Server ohne Grafik (Konsole)                  |
| `test_logic.exe`     | Regeltests + 300 komplette Bot-Partien                    |
| `test_network.exe`   | End-to-End-Test über echtes TCP                           |

## Spielen

1. Eine Person klickt **Spiel hosten**. Die Lobby zeigt die IP-Adressen, über die andere beitreten können.
2. Alle anderen tragen diese Adresse ein und klicken **Beitreten**.
3. Die Spielleitung stellt Rollen und Zeiten ein, füllt bei Bedarf mit **Bots** auf und startet.

Standard-Port ist **27777 (TCP)**. Beim ersten Hosten fragt die Windows-Firewall nach – Zugriff
erlauben. Für Spiele übers Internet muss der Port im Router weitergeleitet werden (oder ihr nutzt
ein VPN wie Tailscale/Radmin und dessen IP). Mindestens 4, höchstens 16 Spieler.

Tastatur: **Enter** springt in den Chat, **F11** oder **Alt+Enter** schaltet Vollbild um,
**F12** speichert einen Screenshot unter `%APPDATA%\Werwolf\screenshots`.

### Rollen

| Rolle        | Fähigkeit                                                                  |
| ------------ | -------------------------------------------------------------------------- |
| Dorfbewohner | keine – findet die Werwölfe                                                |
| Werwolf      | wählt nachts gemeinsam mit dem Rudel ein Opfer, eigener Rudel-Chat         |
| Seherin      | sieht jede Nacht die Rolle eines Spielers                                  |
| Hexe         | ein Heiltrank (rettet das Opfer) und ein Gifttrank (tötet), je einmal      |
| Jäger        | nimmt beim Tod einen Spieler mit                                           |
| Amor         | verliebt in der ersten Nacht zwei Spieler – stirbt einer, stirbt der andere |
| Beschützer   | schützt jede Nacht einen Spieler, nie zweimal hintereinander denselben     |

**Ablauf:** Nacht (Werwölfe, Seherin, Beschützer, Amor gleichzeitig) → Hexe → Morgengrauen →
Tag (Diskussion, per Mehrheit überspringbar) → Abstimmung → Urteil → nächste Nacht.

**Sieg:** Das Dorf gewinnt, wenn kein Werwolf mehr lebt. Die Werwölfe gewinnen, sobald sie
mindestens so viele sind wie alle anderen. Ein gemischtes Liebespaar (Werwolf + Dorf) gewinnt,
wenn nur noch die beiden leben.

Tote werden zu Geistern: Sie sehen alle Rollen, lesen den Rudel-Chat mit und haben einen eigenen
Geisterchat. Wer die Verbindung verliert, kann über **Erneut verbinden** an seinen Platz zurück.

## Dedizierter Server

```powershell
build\release\bin\werwolf_server.exe --port 27777
```

Optionen: `--port <n>`, `--timescale <f>` (Zeitraffer zum Testen), `--quiet` (Chat nicht loggen).
Der Server ist plattformunabhängig (Winsock/BSD-Sockets) und lässt sich auch unter Linux bauen
(`-DWERWOLF_BUILD_CLIENT=OFF`).

## Architektur

```
engine/                 eigene Engine (Bibliotheken)
  core/                 Log, Zeit, UTF-8, Zufall, Binär-Serialisierung, PNG-Export
  net/                  nicht blockierendes TCP mit Nachrichten-Framing (Länge + Nutzdaten)
  platform/win32/       Fenster, Eingabe, DPI-Skalierung, OpenGL-3.3-Kontext
  gfx/                  eigener GL-Loader, gebündelter 2D-Renderer mit SDF-Formen,
                        Schriftatlas (GDI-Rasterung, Fallback-Schrift für Symbole)
  ui/                   Immediate-Mode-UI (Buttons, Texteingabe, Checkboxen, Stepper)
game/
  logic/                Spielregeln als reine Zustandsmaschine (ohne Netz/Grafik, testbar)
  protocol/             Nachrichtentypen Client <-> Server
  server/               autoritativer Server: Lobby, Chat-Routing, Wiederverbinden, Bots
  client/               NetClient + grafische Oberfläche (Menü, Lobby, Spieltisch, Chat)
tests/                  Regel- und Netzwerktests
```

Der Server ist autoritativ: Clients schicken nur Absichten (Auswahl, Bestätigen, Chat), der Server
prüft alles und sendet jedem Spieler eine **persönliche Ansicht** des Spiels. Geheime Informationen
(fremde Rollen, Wolfsstimmen, Visionen) verlassen den Server nur an die, die sie sehen dürfen.

## Entwickleroptionen (Client)

```powershell
werwolf.exe --host --bots 7 --start --timescale 4        # sofort eine Bot-Runde im Zeitraffer
werwolf.exe --join 127.0.0.1 --name Max                  # zweites Fenster tritt bei
werwolf.exe --autoplay                                   # eigener Spieler handelt zufällig
werwolf.exe --screenshots C:\temp\shots --shot-every 2 --quit-after 30
```

Logdatei: `%APPDATA%\Werwolf\werwolf.log`, Einstellungen: `%APPDATA%\Werwolf\config.txt`.
