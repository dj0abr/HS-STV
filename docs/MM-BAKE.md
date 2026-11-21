# QO100 MM-Bake: Integration und Teststufen

Die Erweiterung entsteht vollständig in C++/Qt. Es wird kein Python-Decoder und
kein Python-Hilfsprozess eingebunden. Der Bakenmodus ist ausschließlich Empfang.
Die bisherigen 8APSK-/QPSK-Bildmodi und SSTV bleiben eigenständige Betriebsarten.

## Stufe 1: physikalischer Empfang (bereit zum Stationstest)

Rechts unten **QO100 MM-Bake** auswählen. Der obere Bereich zeigt vorerst eine
Empfangstest-Seite. Die drei Textfenster folgen nach dem erfolgreichen Empfangstest.
Unterhalb bleiben die Digitalanzeige mit Spektrum und Konstellation sowie die
RX-Statistik sichtbar. Senden und Waterfall-ID sind in diesem Modus deaktiviert.

Das Profil verwendet 48-kHz-Mono-Audio, 8APSK, 2400 Symbole/s und 7200 bit/s.
Die Vor-Dezimation ist 5, der Symboltracker arbeitet mit 4 Samples/Symbol und
4800-Hz-PLL-Abtastrate. Der RRC-Rolloff bleibt 0,2, die ungefähre Datenbandbreite
beträgt 2880 Hz. Die AFC übernimmt die vorhandene Suche nach Piloten zwischen
20 und 900 Hz; Pilot und Datenmitte liegen 1400 Hz auseinander.

Unter **Setup → Soundkarten…** den gewünschten Eingang auswählen. Neue
Installationen verwenden für Ein- und Ausgang **System-Standard**. Der Standard
wird beim Öffnen des jeweiligen Streams aufgelöst. Ein laufender Stream bleibt
auf diesem Gerät; nach Wiederöffnen gilt der dann aktuelle Systemstandard.
Bereits gespeicherte konkrete Geräte werden beibehalten. Für diese gibt es
weiterhin keinen automatischen Wechsel auf ein anderes Gerät.

### Stationstest und Abnahmekriterien

1. HS-STV neu starten und **QO100 MM-Bake** wählen.
2. Den gewünschten Audioeingang wählen und den externen USB-Empfänger auf die
   Bake abstimmen. HS-STV stellt keine Funkfrequenz ein. `fromQO100` liefert beim
   QO100_TRX2-Adapter RX2, nicht den getrennten RX5-Bakenkanal.
3. Das vollständige Datensignal samt Pilot muss im Audio-Durchlass liegen.
   Bei einem 100–4000-Hz-Filter ist ein Pilot um 500 Hz sinnvoll: Die Datenmitte
   liegt dann bei 1900 Hz, das ungefähre Datenband bei 460–3340 Hz.
4. Auf **RX · Sync** und grüne Konstellationspunkte warten. Der Zähler **OK** muss
   laufend steigen. Ein gefundener Pilot allein reicht nicht: Synchronisation
   wird erst durch FEC-/CRC-gültige Frames bestätigt.
5. Mindestens einige Minuten auf stabile Nachführung und steigende OK-Zähler
   prüfen. Bei unterbrochenem Signal muss die Synchronisation wieder erlöschen;
   bei erneutem Signal muss sie wiederkehren.
6. Zurück zu 8APSK/QPSK oder SSTV wechseln; Bildbedienung und Sendemöglichkeit
   müssen wieder vorhanden sein.

Erst nach diesem Stationstest wird Stufe 2 eingebaut.

### Automatische Prüfungen

`mm_beacon_phy` ist ein C++-Test mit unabhängig erzeugten Frames und prüft
100-/500-Hz-Piloten, 7200 bit/s, AFC/Drift, gültige Nutzdaten, Spektrum und
Konstellationsdaten, falsche CRC, Signalverlust und einen reinen Piloten ohne
Daten. `gui_controls` prüft den Wechsel in und aus dem Bakenmodus einschließlich
Empfang gültiger Frames. `audio_system_defaults` prüft Ein-/Ausgang gegen einen
privaten PulseAudio-Server mit virtuellen Geräten, ohne physische Soundkarte.
Fehlt der Server, wird dieser einzelne Audiotest übersprungen.

```sh
ctest --test-dir build -R 'mm_beacon_phy|audio_system_defaults|gui_controls' --output-on-failure
```

Der PortAudio-Adapter unterstützt dieselbe Auswahl des Systemstandards und wird
zusätzlich unter Linux kompiliert. Das ersetzt keine spätere Windows-Prüfung.

## Stufe 2: CW-Skimmer und DX-Cluster

Die bereits CRC-geprüften Modemframes an einen C++-Nachrichtensammler anbinden.
Frame-Typ 8, Untertyp 0 enthält DX-Cluster-Meldungen mit Längenbyte; Untertyp 2
enthält CW-Skimmer-Text direkt nach dem Typbyte. Zeichensätze, Feldformatierung
und Duplikaterkennung aus QO100_TRX2 übernehmen. Drei Textbereiche werden
angelegt, der Bulletinbereich bleibt zunächst leer. Beide Meldungslisten müssen
laufend wachsen, begrenzt bleiben und manuelles Lesen ohne Scrollsprünge erlauben.
Abnahme: korrekte Rufzeichen, Frequenzen, Zeitangaben und Sonderzeichen aus
Testframes und echtem Empfang. Noch kein neuer Bulletin-Inhalt.

## Stufe 3: Bulletins

Die Bulletin-Protokolllogik aus
`QO100_TRX2/external/mmdecoder/data_collector.py` nach C++ übertragen:
Dateikopf, Blocknummern, Wiederholungen, Datei-CRC, ZIP-Prüfung und Klartext.
Portable ZIP-Unterstützung im CMake-Build einbinden. Entpacken und Dateizugriffe
außerhalb des GUI-Threads ausführen. Nur vollständig geprüfte Bulletins ersetzen
das angezeigte Bulletin. HTML wird als Klartext dargestellt; Größenlimits und
Prüfungen des bisherigen Decoders bleiben erhalten.
Abnahme: vollständige und wiederholte Bulletins sowie fehlende/falsche Blöcke,
ungültige ZIP-Dateien und Unicode. Ein unvollständiges Bulletin darf das letzte
gültige Bulletin nicht ersetzen.

## Stufe 4: Abschluss und Python-freie Tests

Die vorhandenen sieben Python-Test-/Hilfsdateien in hs-stv durch native
C++-/QtTest-Prüfungen ersetzen, ohne die geprüften Signal- und Fehlerfälle zu
verlieren. Danach Python/NumPy aus CMake, prepare.sh und Dokumentation entfernen.
Die neuen Bakenprüfungen sind bereits C++. Dauerhafte Archive und die Fortsetzung
unvollständiger Bulletins über Programmneustarts werden als eigener Umfang
entschieden. Qt-Dateipfade, PortAudio und passende liquid-dsp-/ZIP-Builds bilden
die Grundlage des späteren Windows-Ports; aktuell wird kein Windows-Build als
getestet behauptet.
