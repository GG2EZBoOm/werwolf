# Musik und Schritte

Die lokalen Audiodateien stammen aus den vom Nutzer bereitgestellten Paketen von
[ZapSplat](https://www.zapsplat.com/):

| Verwendung | Künstler / Titel | Lokale Datei |
| --- | --- | --- |
| Tag, abwechselnd | DeepTheme Audio – A Brief Pause (A Place of Contemplation) | day_contemplation_1.wav |
| Tag, abwechselnd | DeepTheme Audio – Linger (A Place of Contemplation) | day_contemplation_2.wav |
| Nacht | DeepTheme Audio – Foreshadow | night_foreshadow.wav |
| Nacht | DeepTheme Audio – Eyes in the Dark | night_eyes_in_the_dark.wav |
| Nacht | DeepTheme Audio – Creeping Death | night_creeping_death.wav |
| Schritte auf allen Böden | Footsteps on Paper, Einzelaufnahmen 001–020 | footstep_01.wav bis footstep_20.wav |

Die [ZapSplat Standard License](https://www.zapsplat.com/license-type/standard-license/)
erlaubt die Einbettung in das Spiel. Die Audiodateien bleiben außerhalb der Git-Verwaltung;
sie dürfen nicht als eigenständige Audiosammlung veröffentlicht werden. Es werden keine
Musik-Credits im Spiel eingeblendet. Die Lizenz der Musik wird nicht auf den Spielcode übertragen.

Einmalig lokal importieren (Python mit NumPy):

```powershell
python tools/import_music.py <horror_stingers.zip> <a_place_of_contemplation.zip>
python tools/import_footsteps.py <footsteps_on_paper.zip>
```

Danach normal bauen. CMake bettet die fünf Musikdateien und 20 Schritte als Windows-Ressourcen in werwolf.exe ein;
der bestehende Updater braucht keine zusätzlichen Dateien. Die Originale werden nicht verändert.
Fehlen die Pakete bei einem anderen Entwickler, baut der Client mit den prozeduralen Klängen
weiter; CMake meldet fehlende Aufnahmen ausdrücklich. Der Release-Befehl verweigert das Verpacken,
wenn eine benötigte Aufnahme fehlt.

Die Schrittaufnahmen werden auf 44,1 kHz/16 Bit konvertiert, an den Rändern gekürzt und sanft
ein-/ausgeblendet. Alle 20 Varianten spielen in zufälliger Reihenfolge, bevor eine neue Reihenfolge
beginnt; direkte Wiederholungen sind ausgeschlossen. Leichte Lautstärkeunterschiede je Boden und
maximal zwei Prozent Tonhöhenvariation erhalten den Charakter der Aufnahmen.
