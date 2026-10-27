# Musik

Die lokalen Musikdateien stammen aus den vom Nutzer bereitgestellten Paketen von
[ZapSplat](https://www.zapsplat.com/):

| Verwendung | Künstler / Titel | Lokale Datei |
| --- | --- | --- |
| Tag, abwechselnd | Dark Harmonics – Ticking Tension No. 1, Stripped Back Version | day_tension_1.wav |
| Tag, abwechselnd | Dark Harmonics – Ticking Tension No. 2, Stripped Back Version | day_tension_2.wav |
| Nacht | DeepTheme Audio – Foreshadow | night_foreshadow.wav |
| Nacht | DeepTheme Audio – Eyes in the Dark | night_eyes_in_the_dark.wav |
| Nacht | DeepTheme Audio – Creeping Death | night_creeping_death.wav |

Die [ZapSplat Standard License](https://www.zapsplat.com/license-type/standard-license/)
erlaubt die Einbettung in das Spiel. Die Audiodateien bleiben außerhalb der Git-Verwaltung;
sie dürfen nicht als eigenständige Audiosammlung veröffentlicht werden. Es werden keine
Musik-Credits im Spiel eingeblendet. Die Lizenz der Musik wird nicht auf den Spielcode übertragen.

Einmalig lokal importieren (Python mit NumPy):

```powershell
python tools/import_music.py <horror_stingers.zip> <ticking_tension.zip>
```

Danach normal bauen. CMake bettet die fünf Dateien als Windows-Ressourcen in werwolf.exe ein;
der bestehende Updater braucht keine zusätzlichen Dateien. Die Originale werden nicht verändert.
Fehlen die Pakete bei einem anderen Entwickler, baut der Client mit den prozeduralen Klängen
weiter; CMake meldet die fehlende Musik ausdrücklich.
