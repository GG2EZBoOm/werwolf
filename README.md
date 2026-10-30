# Werwolf

## Herunterladen (Windows)

**[Spiel herunterladen: werwolf.exe](https://github.com/GG2EZBoOm/werwolf/releases/latest/download/werwolf.exe)**

Die EXE in einen eigenen Ordner speichern und starten. Musik und Sounds sind bereits enthalten;
zum Spielen ist kein Visual Studio erforderlich. Der integrierte Auto-Updater lädt neue Versionen.

- [Alle Downloads und Versionshinweise](https://github.com/GG2EZBoOm/werwolf/releases/latest)
- [Optional: dedizierter Server – werwolf_server.exe](https://github.com/GG2EZBoOm/werwolf/releases/latest/download/werwolf_server.exe)

**Hinweis:** „Code → Download ZIP“ enthält den Quellcode. Die fertigen EXE-Dateien findest du über
die Links oben oder rechts auf GitHub unter **Releases → Assets**.

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

Port **27777 (TCP)**, 4 bis 32 Spieler. Beim ersten Hosten fragt die Windows-Firewall – Zugriff erlauben.

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

### Gespräche mit Bots

Bots reagieren auf deutsche und englische Spielernachrichten, zum Beispiel:

- `Anton, Anna ist ein Werwolf` – Beschuldigung merken und nach einer Beobachtung fragen.
- `Anna ist kein Werwolf` – Entlastung statt Beschuldigung; Fragen gelten nicht als Beweis.
- `Anton, warum?` oder `Wen verdächtigst du?` – Verdacht mit Quelle erklären, Gesprächskontext kurz behalten.
- `Anton, wo warst du letzte Nacht?` – Auskunft über tatsächlich besuchte Orte, soweit vorhanden.
- `Ich bin die Seherin` – Rollenbehauptung merken und nach Prüfergebnissen fragen.
- `ich hab Berta gecheckt, sie ist ein Werwolf` oder `Clara ist die Hexe` – Prüfergebnisse einer Seherin zählen
  als starker Hinweis. Wer genannt wird, bestätigt oder widerspricht anhand der eigenen Rolle, die echte Seherin
  meldet sich bei einer falschen Behauptung, und sobald Tote aufgedeckt werden, prüfen die Bots die Angaben nach.
  Eine bestätigte Seherin wird geschützt, wer nachweislich lügt, gilt als verdächtig.
- `Anton, welche Rolle hast du?`, `Wer ist der Jäger?`, `Bist du die Hexe?` – manche Bots nennen ihre Rolle, wenn es
  dem Dorf hilft (der Jäger schreckt damit ab, Hexe oder Seherin schweigen lieber), Werwölfe und Vampire lügen
  („nur ein einfacher Dorfbewohner“, ein falscher Jäger). Jeder bleibt bei dem, was er einmal gesagt hat; wer die
  Rolle wirklich hat, widerspricht einer falschen Behauptung, und zwei Ansprüche auf dieselbe Rolle machen misstrauisch.
- `Möchte jemand seine Rolle nennen?`, `Welche Rollen habt ihr?` – eine Frage an die ganze Runde beantworten bis zu
  drei Bots nacheinander, die meisten halten sich bedeckt. Von sich aus nennen Bots ihre Rolle nur, wo es Sinn ergibt:
  Jäger, Dorfälteste, Dorfdepp, Ritter, Geschwister oder ein einfacher Dorfbewohner, unter Druck öfter. Seherin,
  Hexe, Beschützer, Fuchs und andere Nachtrollen schweigen. Wölfe und Vampire geben sich dabei als Dorfbewohner
  oder Jäger aus.
- `Ich bin der Beschützer. Seherin, sag mir heimlich, wer du bist – ich passe heute Nacht auf dich auf.` – die Runde
  warnt die Seherin, sich nicht vor allen zu verraten. Die echte Seherin flüstert es am nächsten Tag im Nähe-Chat,
  wenn sie dem Beschützer traut (kein Lügner, nicht verdächtig, kein zweiter „Beschützer“). Der Beschützer-Bot
  bewacht nachts eine glaubwürdige Seherin. Wölfe nutzen genau das: Sie bieten selbst falschen Schutz an, um die
  Seherin zu finden, oder flüstern ein falsches „Ich bin die Seherin“. Wer ihnen die Seherin verrät, ist nachts ihr
  nächstes Opfer. Der echte Beschützer widerspricht einem falschen Angebot.
- Eine Seherin kann vor der ersten Nacht noch niemanden geprüft haben und danach höchstens eine Person pro Nacht.
  Mitgezählt werden nur ausdrücklich genannte Prüfungen („gecheckt“, „geprüft“), keine bloßen Meinungen.
- `Emil verhält sich komisch`, `Berta raus!`, `wir voten alle Berta`, `Anton du bist sus` – auch Umgangssprache
  zählt als Verdacht oder Stimme; wer schon tot ist, wird als tot erkannt.
- `Ich habe Anna heute Nacht bei der Leiche gesehen`, `Anna war beim Haus von Berta` – verdächtig ist, wer gesehen
  wurde, nicht das Opfer. `Anna war die ganze Nacht bei mir` gibt Anna dagegen ein Alibi.

Ein passender Bot antwortet nach kurzer Bedenkzeit. Mehrere Fragen an denselben Bot beantwortet er
nacheinander, statt sie zu übergehen. Namen werden unabhängig von Großschreibung erkannt;
im Nähe-Chat gelten weiterhin Hörweite und Wände. Private Antworten bleiben im passenden Kanal.
Bots hören auch einander zu. Seherprüfungen, beobachtete Verwandlungen und Spuren werden getrennt von
persönlichen Meinungen, Unterstützung und Vorwürfen behandelt. Der Beschuldigte widerspricht, Freunde
stellen sich bei schwachen Anschuldigungen schützend vor jemanden; Antworten lösen keine weiteren Bot-Antworten aus.
Widersprüchliche Rollen, Alibis und Beschuldigungen senken die Glaubwürdigkeit; eine ausdrückliche
Korrektur (`Ich lag falsch ...`) wird berücksichtigt. Wiederholungen zählen nicht als zusätzliche Zeugen.
Verdacht und Entlastung beeinflussen Wahlen; neue Hinweise können auch eine bereits abgegebene Stimme ändern.
Eigene Seherprüfungen haben Vorrang, Gerüchte werden nicht als eigene Augenzeugenberichte ausgegeben.

Jeder Bot hat eine eigene Persönlichkeit: gesprächig oder still, gutgläubig oder skeptisch, vorschnell oder
vorsichtig, Mitläufer oder Einzelgänger – und schreibt förmlich, locker („hab Berta geprüft“) oder knapp.
Bots ziehen Schlüsse aus dem Spielverlauf („Berta hat für Clara gestimmt, und Clara war unschuldig“, wer einen
entlarvten Wolf verteidigt hat), reagieren auf Tote und Urteile und können sich nachts auch einmal irren.
Nachts merken sie sich, wann und wo sie wen in Menschengestalt gesehen haben. Wer ungefähr zur Tatzeit dort war,
wo jemand starb, ist stark verdächtig, wer irgendwann in der Nacht dort oder beim Haus des Opfers war, weniger.
Wer im selben Augenblick woanders stand als die Bestie, war es nicht. Wer die ganze Nacht in der Nähe eines
Bots war, während anderswo gemordet wurde, bekommt am Morgen ein Alibi. Werwolf oder Vampir ist er damit nur
vermutlich nicht, denn ein Rudelgefährte kann gemordet haben. Wölfe täuschen: Sie geben sich als Seherin aus,
drehen eine Beschuldigung um, geben einem bedrängten Gefährten ein falsches Alibi und holen sich nachts
bevorzugt die Seherin, lautstarke Ankläger oder Zeugen. Wer sich erst nach einer Beschuldigung als Seherin
meldet, wirkt allerdings verdächtig. Wer als falsche Seherin entlarvt und hingerichtet wurde, zählt danach
nicht mehr als zweite Seherin. In der Welt drehen sich Bots zu dem, der
spricht, gehen tagsüber mit Vertrauten mit, halten Abstand zu Verdächtigen, sehen sich morgens den Fundort an und
zögern nachts, bevor sie losgehen.

Gefühle bleiben über mehrere Phasen bestehen und klingen langsam ab. Bekannt gewordene Morde machen
Bots ängstlicher: Sie gehen eher nach Hause und laufen bei großer Angst nicht jedem Geräusch hinterher.
Nach einem öffentlich aufgedeckten Fehlurteil entstehen Wut und Groll gegen die damaligen Wähler;
das beeinflusst spätere Stimmen. Gemeinsame Zeit und Unterstützung stärken Freundschaften, wiederholte
Beschuldigungen schaffen Feindschaften. Freunde werden eher als Begleiter und Hauptmann gewählt.
Unter Druck tippen Bots schneller und schreiben mit hektischer Zeichensetzung. Wiederholte gleiche
Nachrichten verstärken diese Gefühle nicht beliebig; bekannte Rollen und eigene Seherprüfungen haben Vorrang.

Jäger drohen unter Druck mit ihrem letzten Schuss, Hexen deuten die tatsächlich noch verfügbaren Tränke an.
Die Seherin hält harmlose Prüfungen zunächst zurück und enthüllt sich für ein verwertbares Wolfsergebnis,
zur Rettung eines geprüften Unschuldigen oder bei akuter eigener Gefahr. Wölfe können einen stark belasteten
Partner öffentlich fallenlassen und gegen ihn stimmen, wenn das Rudel dafür nicht einen nahen Sieg aufgibt.
Wer öffentlich gegen einen entlarvten Wolf gestimmt hat, gewinnt Vertrauen – auch ein täuschender Mitwolf.

Ohne Zusatzpaket ist das lokale, regelbasierte Spiel-KI. Sie erkennt typische Aussagen zum Spiel;
Ironie und beliebig verschachtelte Sätze werden nicht zuverlässig verstanden.

### Optionales lokales KI-Sprachpaket

Ab Version **0.10.0** können Bots Spielernachrichten zusätzlich mit einem lokalen Sprachmodell beantworten.
Der Host lädt einmal das [KI-Installationspaket](https://github.com/GG2EZBoOm/werwolf/releases/latest/download/Werwolf-KI-Paket.zip),
entpackt es und startet `Installieren.cmd`. Das lädt etwa **2,6 GB** mit geprüften SHA-256-Prüfsummen.
Danach unter **Einstellungen → Bots → Lokale Bot-KI** einschalten und ein Spiel hosten.
Nur der Host braucht das Paket; während der Partie läuft die Inferenz offline, ohne Konto, API-Schlüssel oder Gebühren.

Das Modell bekommt den vom Bot gehörten Verlauf desselben Chatkanals, Stimmung, Beziehung zum Sprecher
und die von der Spiellogik erlaubte Antwortabsicht. Geheime Rollentabellen, unbekannte Seherergebnisse und
unhörbare Gespräche werden nicht übergeben. Rollenenthüllungen, Beweise und strategische Aussagen werden
unverändert an einen optionalen KI-Kommentar angehängt. Abstimmung, Erinnerungen an Beweise, Fähigkeiten
und Bewegung bleiben regelbasiert; das Modell entscheidet nicht eigenständig über Aktionen.
Freie Gespräche gehen auf den Wortlaut ein, können aber weiterhin unpassend formuliert sein.

Antworten entstehen auf einem Hintergrundthread; eine begrenzte Warteschlange, ein Zeitlimit von sieben
Sekunden und die Prüfung von Phase, Kanal und lebenden Gesprächspartnern verhindern blockiertes oder
verspätetes Chatten. Bei Ausfällen oder verworfenen Modellantworten greift der bisherige Dialog.
Deutsch und Englisch werden gemeinsam erzeugt. Ohne installiertes Paket startet kein KI-Prozess.

Technik: **Qwen3-4B-Instruct-2507 Q4_K_M** (GGUF von Unsloth, Apache-2.0), **llama.cpp b11960** (MIT)
mit Vulkan. Lizenzen liegen dem Paket bei. Ausgangspunkt: 16 GB RAM, moderne Vulkan-GPU mit etwa
4 GB freiem VRAM, 6 GB freier Plattenspeicher für die Installation; tatsächliche Leistung hängt von
Hardware und Treiber ab. Das ist eine Integration eines vorhandenen Modells, kein eigenes Modelltraining.

Installation unter `%APPDATA%\Werwolf\ki` oder portabel im Ordner `ki` neben der EXE.
Der normale Auto-Updater verändert diesen Modellordner nicht. Zum Ausschalten den Schalter deaktivieren
und neu hosten. Für eigene lokale Dienste: `--ai-port 8080` (ausschließlich `127.0.0.1`),
für einen anderen Paketordner `--ai-dir <Pfad>`, zum Abschalten `--no-ai`.
Quellen: [Modell](https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507),
[GGUF](https://huggingface.co/unsloth/Qwen3-4B-Instruct-2507-GGUF), [Laufzeit](https://github.com/ggml-org/llama.cpp).

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
  Wer eine Bestie sieht, flieht: Bots rufen um Hilfe, rennen in ein Haus, das sie vor ihr erreichen, und
  verriegeln die Tür – oder laufen von ihr weg.
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
| Pfarrer      | einmal im Spiel Weihwasser (E): ein Werwolf verbrennt, ein Vampir zerfällt zu Staub |
| Bürgermeister | allen bekannt, Stimme zählt doppelt                                           |
| Dorfdepp     | überlebt die erste Hinrichtung, darf danach nicht mehr abstimmen                |
| Verfluchter  | wird zum Werwolf, wenn ihn ein Werwolf angreift                                 |
| Urwolf       | Werwolf, der einmal ein Opfer infizieren kann (F) statt es zu töten             |
| Wolfsjunges  | Werwolf; stirbt es, reißt das Rudel in der nächsten Nacht zwei Opfer            |
| Narr         | gewinnt allein, wenn das Dorf ihn hinrichtet                                    |
| Serienmörder | tötet jede Nacht mit dem Messer (Linksklick), Werwölfe können ihm nichts anhaben |
| Brandstifter | tränkt Häuser mit Öl (E an der Tür) und zündet alle auf einmal an (F)           |
| Vampir       | eigener Clan (1, ab 10 Spielern 2, ab 22 Spielern 3) mit geheimem Chat (Tab): nimmt nachts die Vampirgestalt an (R) – dann glühen die Augen rot – und beißt lautlos ein Opfer (Linksklick), das blutleer mit Bissspuren gefunden wird; kommt durch keine geschlossene Tür. Gewinnt, wenn kein Werwolf mehr lebt und der Clan gleichauf mit dem Rest ist |
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
vergeben, wie Spieler da sind. Automatisch gibt es 1 Werwolf bis 6 Spieler, 2 bis 10, 3 bis 14, 4 bis 19,
5 bis 24, 6 bis 28 und 7 bei bis zu 32 Spielern; von Hand lassen sich bis zu 10 einstellen.

Über einem verwandelten Werwolf oder Vampir steht kein Namensschild, und im Nähe-Chat spricht er als
„Unbekannte Gestalt“ – nur sein Rudel bzw. Clan und die Toten wissen, wer es ist.

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
