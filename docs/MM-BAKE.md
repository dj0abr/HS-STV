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

Stufe 1 ist abgenommen; der DX-Stationstest von Stufe 2 ist ebenfalls bestätigt.

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

## Stufe 2: CW-Skimmer und DX-Cluster (DX-Stationstest erfolgreich)

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
Datenträger archiviert. Der Bulletinbereich wird durch die folgende Stufe 3 ergänzt.

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
   In Stufe 3 werden dort zusätzlich die Bulletins dekodiert.

`mm_beacon_messages` prüft die Parser, Zeichensätze, Duplikate und ungültige
Frames. `mm_beacon_text_views` prüft Kanaltrennung, Klartextdarstellung,
Speicherbegrenzung und stabiles Scrollen/Markieren. `gui_controls` speist
synthetisches 7200-bit/s-Audio durch den echten Empfangskern bis in die beiden
Textfenster ein. Alle neuen Prüfungen sind C++ und benötigen kein echtes Audio.

```sh
ctest --test-dir build -R 'mm_beacon_|gui_controls' --output-on-failure
```

Der DX-Cluster-Empfang wurde im Stationstest bestätigt. CW-Skimmer war dabei
mangels Aktivität nicht zu beobachten; der vollständige CW-Signalweg besteht
den automatischen Test. Der Bulletin-Empfang wurde ebenfalls im Stationstest bestätigt.

## Stufe 3: Bulletins (Stationstest erfolgreich)

Datei- und Bulletin-Protokoll sind aus QO100_TRX2 nach C++/Qt übertragen. Der
Modemkern liefert CRC-geprüfte Frames. `BulletinReceiver` verarbeitet die
Bulletin-Dateien in einem eigenen Worker-Thread, außerhalb von Audio und GUI.
Er liest den 55-Byte-Dateikopf, sammelt nummerierte Blöcke der Dateitypen 3–5
und ergänzt fehlende Teile aus Wiederholungen. Ein neuer Stream braucht wieder
einen Dateikopf; bis zu 32 angefangene Bulletins bleiben während der Laufzeit
im Speicher. Interleaved CW-/DX- und Spektrummeldungen unterbrechen das nicht.

Erst nach vollständigem Empfang folgen Datei-CRC16, ZIP-Verzeichnisprüfung,
Entpacken und ZIP-CRC32. Der begrenzte ZIP-Leser unterstützt Stored und Deflate,
auch Daten-Deskriptoren mit und ohne Signatur sowie Unterverzeichnisse. Er liest
genau eine `.blt`-Datei vollständig im Speicher; keine ZIP-Datei wird auf Platte
extrahiert. Die neue Bibliotheksabhängigkeit ist **zlib**. Unter Debian/Ubuntu
installiert `prepare.sh` dafür `zlib1g-dev`; CMake verwendet `ZLIB::ZLIB`, auch für
einen späteren Windows-Build.

UTF-8 und Windows-1252, Bulletin-Kategorie/Seitennummer/Titel sowie HTML-Klartext
werden unterstützt. Script-/Style-Inhalte werden entfernt und HTML-Ressourcen
nicht geladen. HTTP-/HTTPS-Linkziele bleiben im Klartext lesbar. Beim Eintreffen
eines vollständig geprüften neuen Bulletins wird das rechte Textfenster von oben
neu angezeigt. Duplikate versetzen die Leseposition nicht. Fortschritt oder
Fehler stehen unter dem Bulletin; dabei bleibt der letzte gültige Text erhalten.

Grenzen: höchstens 1024 Funkblöcke je komprimierter Datei, 4 MiB entpackter
Inhalt und 65536 Zeichen in der Anzeige. Eine gekürzte Anzeige trägt einen Hinweis.
Die Worker-Queue ist auf 512 Frames begrenzt und verwirft bei einem Neustart alte
Ergebnisse. Unvollständige Dateien werden nach einem Programmneustart nicht
fortgesetzt. Vollständige Bulletins werden inzwischen wie unten beschrieben archiviert.

### Stationstest und Abnahmekriterien

1. HS-STV neu starten, **QO100 MM-Bake** und den bisherigen Audioeingang wählen.
2. Bei **RX · Sync** auf ein Bulletin warten. Unter dem rechten Fenster erscheint
   der Dateiname mit **empfangene/erwartete Blöcke**. Fehlt der Anfang, auf den
   nächsten Dateikopf im Bakenzyklus warten.
3. Nach vollständiger Prüfung muss der Bulletintext lesbar erscheinen und die
   Statuszeile **Bulletin vollständig geprüft und gespeichert** melden.
4. Umlaute, Zeilenumbrüche und Links kontrollieren. HTML-Tags oder Script-Inhalte
   sollen nicht als ausgeführte Inhalte erscheinen.
5. Während des nächsten Bulletin-Empfangs muss der vorige gültige Text erhalten
   bleiben. Ein CRC-/ZIP-Fehler darf ihn nicht ersetzen. Bei einer Wiederholung
   sollen fehlende Blöcke ergänzt werden.
6. CW-/DX-Ausgabe, Synchronisation und Bedienung müssen währenddessen weiterlaufen.

`mm_beacon_bulletins` prüft ZIP-Varianten und fehlerhafte Archive, Blockverluste,
Wiederholungen, Datei-CRC-Reparatur, Metadaten, Zeichensätze, Klartext und die
Zustellung vom Worker auf dem GUI-Thread. `mm_beacon_text_views` prüft das
Beibehalten des letzten Bulletins bei Fehlern, Duplikate und Anzeigegrenzen.
`gui_controls` erzeugt zusätzlich echte 7200-bit/s-Audiodaten einer Bulletin-Datei
und prüft den kompletten Weg über den Empfangskern und Worker bis ins Textfenster.
Alle neuen Prüfungen sind C++.

```sh
ctest --test-dir build -R 'mm_beacon_|gui_controls' --output-on-failure
```

### Archiv und Lesefenster

Jedes vollständig geprüfte Bulletin wird automatisch im Unterordner `bulletins`
des Projekt-/Arbeitsverzeichnisses gespeichert. Das Archiv enthält den vollständigen
dekodierten Text mit übertragenem Dateinamen, Titel, Kategorie, Seitennummer,
Empfangszeit und Prüfsummen in atomar geschriebenen JSON-Dateien. Die ursprüngliche
HTML-/ZIP-Datei wird nicht separat abgelegt. Die Vorschaugrenze von 65536 Zeichen
gilt nicht für das Archiv oder das Lesefenster.

Gleicher Dateiname und Inhalt erzeugen keinen zweiten Eintrag. Eine geänderte
Fassung desselben Dateinamens bleibt als eigene Version erhalten. Das Archiv
bleibt nach einem Programmneustart verfügbar und ist in Git ausgeschlossen.
Schreibfehler erscheinen unter der Vorschau; der gültige Text bleibt sichtbar,
und eine erneute gültige Übertragung versucht das Speichern nochmals.
Speichern und Laden laufen auf dem Bulletin-Worker, außerhalb des GUI-Threads.

Ein Klick in den Bulletintext öffnet ein zusätzliches, nicht modales Lesefenster.
Mit fokussiertem Textfeld funktionieren auch Eingabe- oder Leertaste. Beim Öffnen
erscheint das aktuelle Bulletin; die Combobox bietet archivierte Dateinamen mit
Empfangsdatum, neueste zuerst. Ohne aktuellen Empfang wird der neueste Archiveintrag
geladen. Ein eintreffendes Bulletin ergänzt die Auswahl, ohne den gerade gelesenen
Text zu ersetzen. **Schließen**, Escape oder der Fensterschließknopf schließen das
Lesefenster. Textmarkierung durch Ziehen öffnet kein zusätzliches Fenster.

Zum Stationstest ein Bulletin empfangen, das Lesefenster öffnen und die Auswahl
älterer Dateien prüfen. Danach HS-STV neu starten und vor dem nächsten vollständigen
Empfang das Archiv öffnen: Die gespeicherten Texte müssen weiterhin verfügbar sein.
`mm_beacon_bulletin_archive` prüft Speichern, Neustart, vollständige lange Texte,
Duplikate, Versionen, Fehlerbehandlung und Bedienung. `gui_controls` prüft den
gesamten Weg vom synthetischen Empfangsaudio bis zum Archiv und Lesefenster.

## Stufe 4: Abschluss und Python-freie Tests

Die vorhandenen sieben Python-Test-/Hilfsdateien in hs-stv durch native
C++-/QtTest-Prüfungen ersetzen, ohne die geprüften Signal- und Fehlerfälle zu
verlieren. Danach Python/NumPy aus CMake, prepare.sh und Dokumentation entfernen.
Die neuen Bakenprüfungen sind bereits C++. Die Fortsetzung unvollständiger
Bulletins über Programmneustarts bleibt ein eigener Umfang.
Qt-Dateipfade, PortAudio und passende liquid-dsp-/ZIP-Builds bilden
die Grundlage des späteren Windows-Ports; aktuell wird kein Windows-Build als
getestet behauptet.
