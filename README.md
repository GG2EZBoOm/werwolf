# Werwolf

*Werwolf* als 3D-Multiplayer-Spiel: Man läuft in der dritten Person durch ein Dorf, redet im
Nähe-Chat, beobachtet, versammelt sich am Feuer zur Abstimmung – und nachts verwandeln sich die
Werwölfe und gehen auf die Jagd. Alles läuft auf einer eigenen, von Grund auf geschriebenen
C++20-Engine ohne externe Bibliotheken; Welt, Gebäude und Figuren werden prozedural erzeugt.

> **English:** The whole game is available in English (*Werewolf*). It follows the Windows language;
> switch it under **Settings → Interface → Language**. Players on the same server may use different
> languages – every message from the narrator reaches each player in their own language.

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

1. Eine Person klickt **Ein Dorf gründen**; die Lobby zeigt die Adressen für die anderen (ein Klick kopiert sie).
2. Alle anderen tragen die Adresse ein und klopfen an – oder treten über eine **Discord-Einladung** bei.
3. Die Spielleitung stellt Rollen und Phasenlängen ein, füllt mit **Bots** auf und startet.

Port **27777 (TCP)**, 4 bis 16 Spieler. Beim ersten Hosten fragt die Windows-Firewall – Zugriff erlauben.

### Über das Internet (IPv4 und IPv6)

Der Server lauscht gleichzeitig auf IPv4 und IPv6. Beim Beitreten geht jede Form: `84.12.3.4`,
`84.12.3.4:27777`, `2001:db8::1`, `[2001:db8::1]:27777` oder ein Hostname; hat ein Name mehrere
Adressen, werden alle nacheinander versucht.

- **Router-Freigabe:** Beim Hosten gibt das Spiel den Port per UPnP selbst im Router frei (IPv4-Weiterleitung
  und, falls der Router es kann, die IPv6-Firewall) und räumt beim Beenden wieder auf. Bei einer FRITZ!Box muss
  dafür unter *Internet → Freigaben* für den PC „Selbstständige Portfreigaben erlauben“ an sein. Abschaltbar unter
  **Einstellungen → Oberfläche**; dann den TCP-Port von Hand freigeben.
- **Die Lobby zeigt**, wie man herkommt: Heimnetz, öffentliche IPv4, IPv6 und VPN-Adressen (Radmin, Hamachi,
  Tailscale) sowie den Zustand der Freigabe. Die öffentliche IPv4 fragt das Spiel bei `api.ipify.org` ab.
- **DS-Lite/Carrier-Grade-NAT:** Ohne eigene öffentliche IPv4 erkennt die Lobby das und rät zu IPv6 bzw. einem VPN.

### Discord

Läuft Discord, zeigt es an, dass du Werwolf spielst (Lobby, Phase, Mitspielerzahl – nie deine Rolle).
In der Lobby kann man Freunde über **＋ → Zu Werwolf einladen** in einem Discord-Chat einladen; ein Klick auf
„Beitreten“ startet bei Bedarf das Spiel und verbindet direkt (IPv6, IPv4 oder Heimnetz). Fragt jemand per Discord
an, erscheint in der Lobby ein Zettel zum Einlassen. Abschaltbar unter **Einstellungen → Oberfläche**.
Für das große Bild im Discord-Status muss im Discord-Entwicklerportal (App `1564687671161524365`) unter
*Rich Presence → Art Assets* ein Bild mit dem Namen `logo` hochgeladen sein.

**Dorf erkunden** im Hauptmenü startet eine Offline-Runde zum Umsehen (ohne Spielregeln).

Alle Gebäude sind begehbar. Jedes Wohnhaus gehört einem oder mehreren Spielern. Die Kapelle im Norden gehört
niemandem: Durch das Doppelportal geht es zwischen den Kirchenbänken hindurch bis zum Altar. Wer sich nachts
dorthin flüchtet, kann sie wie ein Haus von innen verriegeln.

### Ton

Wind, Vögel und Grillen begleiten den Tag-Nacht-Wechsel; Lagerfeuer und Kamine knistern in der
Nähe. Schritte verwenden 20 wechselnde Aufnahmen, mit angepasster Lautstärke auf Gras, Pflaster
und Holzböden. Türen, Schläge und
Verwandlungen sind mit Richtung und Entfernung hörbar; Wände dämpfen sie. Glocke, Rollenkarte,
Phasenwechsel, Nachrichten und Spielende haben eigene Klangsignale. Das funktioniert auch offline.

Tagsüber wechseln sich zwei ruhige Stücke aus „A Place of Contemplation“ weich ab. Nachts erklingen Horror-Stinger mit
ruhigen Abständen. Die Musik blendet beim Phasenwechsel über und tritt bei wichtigen Signalen zurück.

Unter **Einstellungen → Ton** gibt es Regler für Gesamtlautstärke, Effekte, Umgebung und Musik, eine
Stummschaltung und eine Glocke zum Probehören. Standardmäßig verstummt das Spiel im Hintergrund;
das lässt sich dort abschalten. Die Einstellungen werden gespeichert. Die übrigen Geräusche entstehen im
Spiel selbst, die ausgewählten Musiktracks und Schritte sind in der EXE eingebettet; zusätzliche Dateien sind
beim Spielen nicht nötig. Hinweise zum lokalen Musikimport stehen in `game/assets/audio/README.md`.

### Steuerung

| Taste            | Aktion                                                         |
| ---------------- | -------------------------------------------------------------- |
| WASD             | laufen, **Shift** rennen                                        |
| Maus / Mausrad   | umsehen / Kamera-Abstand                                        |
| **E**            | Hauptaktion auf die angeschaute Person (abstimmen, Vision, …)   |
| **F**            | Zweitaktion (Gifttrank, Infizieren, Häuser anzünden)            |
| **Q**            | Tür öffnen/schließen – von innen geschlossen ist sie verriegelt |
| **R**            | verwandeln (Werwolf, nur nachts)                                |
| Linksklick       | reißen, erstechen oder eine verriegelte Tür einschlagen          |
| Enter            | Chat schreiben – **Tab** wechselt in den Rudel-, Geister- bzw. Geschwister-Chat |
| Esc              | Pause / Maus freigeben                                          |
| F11, F12         | Vollbild, Screenshot                                            |

### Ablauf

* **Tag** – frei herumlaufen, Spuren suchen, reden. Gesprochen wird im **Nähe-Chat** (24 m Hörweite).
  Ab Tag 2 kann jeder an der **Glocke** auf dem Dorfplatz eine Versammlung einberufen.
* **Versammlung** – alle sitzen am Feuer und hören sich gegenseitig. Danach folgt die
  **Abstimmung**: Person anschauen und **E** drücken (oder in der Stimmenliste klicken).
* **Nacht** – dunkel; wer lebt, trägt eine Laterne. Jeder hat ein eigenes **Haus** (auf der Karte
  markiert) und kann sich darin einschließen. Werwölfe verwandeln sich (R), reißen ein Opfer pro Nacht
  und brechen dafür auch Türen auf – das Krachen hört man in der Nachbarschaft. Sonderrollen handeln nachts.
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
| Dorfältester | übersteht den ersten Wolfsangriff                                              |
| Kleines Mädchen | liest nachts den Rudel-Chat mit – ohne Namen                               |
| Medium       | spricht nachts mit den Toten (Tab)                                             |
| Fährtenleser | folgt nachts einer Person (E), sieht sie durch Wände und erfährt morgens ihre Spur |
| Schmied      | beschlägt oder repariert tagsüber eine Tür (E) – sie hält doppelt so lange      |
| Pfarrer      | einmal im Spiel Weihwasser (E): ein Werwolf verbrennt                           |
| Bürgermeister | allen bekannt, Stimme zählt doppelt                                           |
| Dorfdepp     | überlebt die erste Hinrichtung, darf danach nicht mehr abstimmen                |
| Verfluchter  | wird zum Werwolf, wenn ihn ein Werwolf angreift                                 |
| Urwolf       | Werwolf, der einmal ein Opfer infizieren kann (F) statt es zu töten             |
| Wolfsjunges  | Werwolf; stirbt es, reißt das Rudel in der nächsten Nacht zwei Opfer            |
| Narr         | gewinnt allein, wenn das Dorf ihn hinrichtet                                    |
| Serienmörder | tötet jede Nacht mit dem Messer (Linksklick), Werwölfe können ihm nichts anhaben |
| Brandstifter | tränkt Häuser mit Öl (E an der Tür) und zündet alle auf einmal an (F)           |
| Dieb         | stiehlt in der ersten Nacht die Rolle eines Spielers in der Nähe (E)             |
| Sündenbock   | stirbt bei Gleichstand in der Abstimmung                                        |
| Fuchs        | wittert nachts, ob bei einer Person oder in ihrer Nähe ein Werwolf ist (E); ohne Fund verliert er die Gabe |
| Bärenführer  | allen bekannt; sein Bär brummt, wenn ein Werwolf neben ihm sitzt oder ihm nahe kommt |
| Ritter       | tötet ihn ein Werwolf, stirbt dieser in der folgenden Nacht an der rostigen Klinge |
| Ergebene Magd | übernimmt beim Urteil die Rolle des Hingerichteten (E), die dann geheim bleibt |
| Stotternder Richter | verlangt einmal heimlich eine zweite Abstimmung am selben Abend (F)      |
| Schauspieler | spielt drei Nächte lang je eine andere Sonderrolle                               |
| Wildes Kind  | wählt ein Vorbild (E); stirbt es, wird das Kind zum Werwolf                       |
| Wolfshund    | entscheidet sich in der ersten Nacht für Dorf (E) oder Rudel (F)                  |
| Zwei Schwestern / Drei Brüder | kennen sich und haben einen geheimen Chat (Tab)                  |
| Dorfbewohner-Dorfbewohner | allen als ehrlicher Dorfbewohner bekannt                             |
| Rabe         | belastet nachts jemanden (E) mit zwei Stimmen bei der nächsten Abstimmung        |
| Großer böser Wolf | Werwolf; solange kein Wolf gestorben ist, ein zusätzliches Opfer pro Nacht   |
| Weißer Werwolf | jagt mit dem Rudel, reißt jede zweite Nacht einen Werwolf und gewinnt allein   |
| Flötenspieler | verzaubert nachts bis zu zwei Spieler (E); gewinnt, wenn alle verzaubert sind  |
| Engel        | gewinnt, wenn er in der ersten Nacht oder bei der ersten Hinrichtung stirbt       |
| Sektierer    | gewinnt, wenn nur noch Bewohner seiner Dorfhälfte (Ost/West) leben               |

**Hauptmann:** Ist die Hauptmannwahl eingeschaltet, wählt das Dorf am ersten Abend einen Hauptmann.
Seine Stimme zählt doppelt; stirbt er, bestimmt er in 15 Sekunden einen Nachfolger (E).

In der Lobby werden Sonderrollen einzeln zugeschaltet; gibt es mehr als Plätze, werden nur so viele
vergeben, wie Spieler da sind.

**Sieg:** Das Dorf gewinnt, wenn kein Werwolf und kein mordender Einzelgänger mehr lebt; die Werwölfe,
sobald sie mindestens so viele sind wie alle anderen; ein gemischtes Liebespaar, wenn nur noch die
beiden leben; Serienmörder, Brandstifter und weißer Werwolf, wenn höchstens noch eine weitere Person
lebt; Narr, Engel, Flötenspieler und Sektierer nach ihren eigenen Bedingungen.

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
  audio/                Stereo-Mixer mit Windows-Ausgabe, Lautstärkegruppen und weichen Übergängen
game/
  audio/                prozedurale Klänge und zustandsbasierte Geräuschereignisse
  world/                deterministisch erzeugtes Dorf (Gelände, Häuser, Kapelle, Wald, Felder),
                        Kollision, Bewegung
  logic/                Echtzeit-Regelwerk (Match) – testbar ohne Netz und Grafik
  protocol/             Nachrichten Client <-> Server
  server/               autoritativer Server, Nähe-Chat, Bots mit Tagesroutine, Verdacht und Zeugen
  client/               Netzwerk-Client mit Interpolation, 3D-Spielansicht, HUD, Menüs, Updater
  update/               Update-Kern: Manifest, Versionen, sicheres Ersetzen mit Rollback
tests/                  Regel-, Netzwerk- und Updater-Tests
                        sowie Audio-Mixer, Klangdaten und Geräuschereignisse
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
