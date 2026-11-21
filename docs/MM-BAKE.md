# QO100 MM-Bake: Integration und Teststufen

Die Erweiterung entsteht vollständig in C++/Qt. Es wird kein Python-Decoder und
kein Python-Hilfsprozess eingebunden. Der Bakenmodus ist ausschließlich Empfang.
Die bisherigen 8APSK-/QPSK-Bildmodi und SSTV bleiben eigenständige Betriebsarten.

## Stufe 1: physikalischer Empfang (Stationstest erfolgreich)

Der Stationstest der ersten Stufe wurde am 21.11.2026 erfolgreich bestätigt:
Synchronisation, Konstellation und gültige Frames wurden an der echten Bake
empfangen. Unterhalb der Textbereiche bleiben Spektrum, Konstellation und
RX-Statistik sichtbar. Senden und Waterfall-ID sind im Bakenmodus deaktiviert.

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

Stufe 1 ist abgenommen; die folgende Stufe 2 ist jetzt zum Stationstest bereit.

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

## Stufe 2: CW-Skimmer und DX-Cluster (bereit zum Stationstest)

Die CRC-geprüften Modemframes werden im Bakenmodus an den C++-Nachrichtensammler
`BeaconMessages` übergeben. Frame-Typ 8, Untertyp 0 enthält DX-Cluster-Meldungen
mit Längenbyte; Untertyp 2 enthält CW-Skimmer-Text direkt nach dem Typbyte. Die
Portierung folgt `QO100_TRX2/external/mmdecoder/data_collector.py`: UTF-8 mit
Windows-1252-Fallback, Feldformatierung und kanalbezogene Duplikaterkennung.
Für den Fallback ist unter Qt 6 keine zusätzliche Codec-Bibliothek nötig.

Der obere Bereich enthält links CW-Skimmer und DX-Cluster übereinander, rechts
Bulletins. Die Trennlinien lassen sich verschieben. Beide Meldungslisten zeigen
höchstens die jüngsten 1000 Einträge. Jede Zeile beginnt mit der lokalen
Empfangszeit in UTC; die übertragene UTC-Zeit bleibt ebenfalls lesbar. Gleiche
Meldungen innerhalb von 120 Sekunden werden unterdrückt. Unbekannte Textformate
bleiben als Klartext sichtbar. Beim Lesen älterer Zeilen oder Markieren von Text
springt die Ansicht nicht bei jeder neuen Meldung nach unten. Die Texte bleiben
beim Wechsel der Betriebsart im Fenster erhalten; sie werden noch nicht auf
Datenträger archiviert. Der Bulletinbereich bleibt bis Stufe 3 leer.

### Stationstest und Abnahmekriterien

1. HS-STV neu starten und **QO100 MM-Bake** wählen; Audioeingang und Abstimmung
   wie beim erfolgreichen Test von Stufe 1 einstellen.
2. Auf **RX · Sync**, grüne Konstellation und steigenden OK-Zähler achten.
3. Neue Meldungen müssen im richtigen Fenster erscheinen: CW-Skimmer mit
   Rufzeichen, Frequenz, dB und WPM; DX-Cluster mit Rufzeichen, Frequenz in kHz,
   Kommentar, UTC und gegebenenfalls Locator.
4. Wiederholte gleiche Meldungen sollen innerhalb von zwei Minuten nicht erneut
   angehängt werden; Meldungen mit geänderten Inhalten bleiben sichtbar.
5. Rufzeichen, Frequenzen, Umlaute und Sonderzeichen prüfen. Einzelne noch nicht
   erkannte Formatvarianten erscheinen unverändert als Klartext und können so
   für die Anpassung gemeldet werden.
6. In einer längeren Liste zurückscrollen und Text markieren: Neu empfangene
   Meldungen dürfen die Leseposition nicht zum Listenende versetzen.
7. Zwischen Bildmodus und Bake wechseln. Die empfangenen Texte bleiben erhalten.
   Bulletins werden in dieser Stufe noch nicht dekodiert.

`mm_beacon_messages` prüft die Parser, Zeichensätze, Duplikate und ungültige
Frames. `mm_beacon_text_views` prüft Kanaltrennung, Klartextdarstellung,
Speicherbegrenzung und stabiles Scrollen/Markieren. `gui_controls` speist
synthetisches 7200-bit/s-Audio durch den echten Empfangskern bis in die beiden
Textfenster ein. Alle neuen Prüfungen sind C++ und benötigen kein echtes Audio.

```sh
ctest --test-dir build -R 'mm_beacon_|gui_controls' --output-on-failure
```

Nach erfolgreichem Stationstest dieser Stufe folgt die Bulletin-Dekodierung.

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
