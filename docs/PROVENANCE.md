# Quellen und Lizenzen

Der neue Anwendungscode steht unter GPL-3.0-or-later, siehe `LICENSE` und die
SPDX-Hinweise in den Quelldateien.

Die Modulationsparameter und die Audio-Synthese wurden anhand des lokalen
HS-Modem von DJ0ABR / Kurt Moraw bestimmt. Maßgeblich sind `liquid_if.cpp`,
`tuning.cpp`, `hsmodem.cpp` und `constellation.cpp` unter
`/Daten/funk/hsmodem/hsmodem`. Diese Dateien enthalten GPL-2.0-or-later-Hinweise.
Die genaue Bestandsidentität steht im Quellenmanifest `QUELLEN.json` der Analyse.
Originalprojekt: https://github.com/amsat-dl/QO-100-Modem
Entwicklerdokumentation: https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:ota

Der Referenztest enthält eine eigenständige Transkription der relevanten
Modulatorschritte zum Vergleich mit dem Originalverfahren. Im Kern bleiben der
Original-RRC-Offset-Rechenweg, NCO-Schrittreihenfolge, Real-plus-Imaginär-Mischung
und der Pilotfaktor erhalten. Ein neues Sendesignal wird am Anfang innerhalb
von 5 ms eingeblendet; Pegeländerungen werden ebenfalls geglättet.
Die ursprüngliche feste Ausgangsskalierung 0,1 wurde durch eine aus RRC-Taps,
Konstellation, Audio-Projektion und Pilotamplitude bestimmte Vollaussteuerungs-
normalisierung ersetzt (Faktor ungefähr 0,317397 im aktuellen 6000-bit/s-Modus). Das erhöht Daten und Pilot
linear um denselben Faktor und lässt die übrige Wellenform unverändert.

Der Modus wurde anschließend auf Benutzerwunsch auf 7200 bit/s umgestellt.
Zusätzlicher Parameterabgleich mit
[amsat-dl/multimedia_beacon](https://github.com/amsat-dl/multimedia_beacon),
Commit `ce444ba3620174a02d79806555343369bb623692` (18.01.2023):
`hsmodem/hsmodem.cpp`, `hsmodem/liquid_if.cpp` und `hsmodem/tuning.cpp`.
7200 bit/s entstehen aus 48 kHz / 20 Samples je Symbol × 3 Bit je Symbol.
Die Dateien wurden nur gelesen; es wurden daraus keine weiteren Komponenten
übernommen. SHA-256 und Quelllinks stehen in `BEACON-REFERENCE.json`.

liquid-dsp 1.8.3, Copyright Joseph Gaeddert und Mitwirkende, MIT-Lizenz:
`external/liquid-dsp-1.8.3/LICENSE`. Header, statische und dynamische
Linux-x86-64-Bibliotheken sowie das Build-/SHA-256-Manifest wurden unverändert aus
`QO100_TRX2/external/liquid-dsp-1.8.3` übernommen. Die Prüfsummen sind im
mitgelieferten `manifest-linux-x86_64.json` dokumentiert. Der normale Build nutzt
die statische Bibliothek. Upstream-Commit:
`10041f70cebbe3b97887e75bb41e48b73dda1b23`.
Quellarchiv: https://codeload.github.com/jgaeddert/liquid-dsp/tar.gz/refs/tags/v1.8.3

Qt, libpulse und optional PortAudio werden als externe Abhängigkeiten eingebunden.
Es wurden keine Schifra-, Codec2-, Opus-, FFTW- oder HS-Modem-Empfangsbausteine
in den Testsender übernommen. Beide vorhandenen Quellprojekte bleiben unverändert.

## Erweiterung Bildversand (Version 0.2)

Frameformat, Dateiheader und Wiederholungsablauf wurden mit `frame_packer.cpp`,
`frameformat.h`, `crc16.cpp`, `scrambler.cpp`, `hsmodem.cpp` und
`oscardata/oscardata/ArraySend.cs` des lokalen Originals abgeglichen.
Die feste Scrambler-Tabelle wurde aus dem GPL-2.0-or-later-Original von DJ0ABR
übernommen; der entsprechende Hinweis steht in `scrambler_table.inc`.
CRC, Serialisierung, Ablauf und GF(256)/RS-Encoder sind im neuen Kern eigenständig
implementiert. Es wurde keine Schifra-Implementierung in den Sender übernommen.

64 binäre Referenzvektoren wurden mit den Originalfunktionen Pack(), CRC16,
Schifra-FEC und TX_Scramble erzeugt. Das temporäre Oracle-Programm gehört nicht
zum Projekt und ist keine Build-Abhängigkeit. Herkunft und SHA-256 der verwendeten
Originaldateien stehen in `tests/data/original_frames.json`.

JPEG-Laden, Skalieren und Kodieren erfolgen im Qt-Adapter; der portable Protokoll-
und Audiokern erhält nur die fertigen Bytes. Der optionale RX-Kompatibilitätstest
verwendet den bereits vorhandenen Decoder, ohne dessen Code zu übernehmen.

## Empfang (Version 0.3)

Pilotsuche, AFC und RX-Kette wurden aus QO100_TRX2 übernommen, Stand
`7af11b830ba83b7d497a89ca3f1492008b212cd9`. Der lokale Bestand liegt vollständig
unter `external/qo100-rx/`, einschließlich Lizenz-/Herkunftshinweisen und
Schifra-Decoderheadern. `SOURCE.json` enthält die Original-Prüfsummen.
Die frühere Aussage zu nicht übernommenen RX-Bausteinen bezieht sich auf die
Sender-Versionen 0.1/0.2; ab 0.3 ist der RX-Kern ausdrücklich enthalten.

`rx_probe.cpp` erhält Windows-Binärmodus für stdin, ein zusätzliches
Piloten-Anzeigefeld und die optionale Ausgabe einer unabhängigen Monitor-FFT. Suchstrategie, Filter, Symboltracking, FEC und CRC bleiben
wie im QO100_TRX2-Bestand. Der Kern läuft als eigenes Programm; Qt-Prozess- und
Audioadapter umgeben die portable DSP-Implementierung.

Blockpuffer, Dateikopfauswertung und schrittweiser Bitmap-Aufbau wurden aus
`oscardata/oscardata/receivefile.cs` und `ArraySend.cs` des Original-HS-Modems in
`ImageAssembler` übertragen. Qt übernimmt die Darstellung teilweise empfangener
JPEGs. Der Empfangskern und der Bildassembler sind vom unveränderten Sendepfad
getrennt. Das Verhalten ist in `EMPFANG.md` beschrieben.

Die 16 ursprünglichen RX-Tests und der synthetische Signalerzeuger sind mit
übernommen. Aus `rx_monitor.py` ist ausschließlich die reine Spektrumfunktion als
Testhilfe extrahiert; Tk-/Web-/Streaming-Programme werden nicht eingebunden.

Die native Qt-Monitoranzeige und `AudioSpectrum` sind neue mm-sstv-Bausteine.
Sie nutzen die ohnehin enthaltene liquid-dsp-FFT; die Monitoroption verändert
keine Samples, Pilotentscheidung oder Decoderparameter. Die Konstellationsdaten
kommen unverändert aus der bestehenden `points`-Ausgabe von QO100_TRX2.

## Rückkehr zu 6000 bit/s und dauerhafter Empfang

Auf Benutzerwunsch wieder **6000 bit/s**, damit das Signal durch schmalere
Funkgerätefilter passt. Referenz ist der ursprüngliche HS-Modem-Moduseintrag
`{48000, 24, 6, 3, 6000, 4800}` in `hsmodem.cpp`: 24 TX-Samples pro Symbol,
RX-Vordezimation 6 und vier Samples pro Symbol im Tracker. Die interne PLL
läuft mit zwei Samples pro Symbol, daher beträgt ihre Umrechnung 4000 Hz.
Pilotsuche, AFC-Entscheidungen, Symboltracker, FEC und Frame-CRC bleiben erhalten.
Der Testgenerator ist entsprechend auf 24 Samples pro Symbol angepasst.
Die aktuellen Tests verwenden diese 6000-bit/s-Parameter. Ältere 7200-bit/s-
Prüfberichte und das Beacon-Manifest dokumentieren den historischen Projektstand.

Die GUI bietet keine Testaussendungen und keinen Empfangsschalter mehr.
Der Empfang ist dauerhaft aktiviert, mit Wiederverbindung nach Gerätefehlern.
Das Testsender-API für automatisierte Audiotests und die Offline-WAV-Werkzeuge
bleiben intern bzw. separat verfügbar.

## QPSK 4410

Der neue wählbare Modus entspricht dem ursprünglichen HS-Modem-Eintrag
`{44100, 20, 5, 2, 4410, 3600}`. Modulation mit `LIQUID_MODEM_QPSK` und beidseitige
Zwei-Bit-Gray-Umwandlung stammen als Verfahrensreferenz aus `liquid_if.cpp`.
Die bereits übernommenen QPSK-Rotationstabellen, Bytekonvertierung, FEC und CRC
werden unverändert benutzt; es wurde kein eigener QPSK-Framer eingeführt.

Die Soundkartenadapter und die portable DSP-/Monitor-Schicht unterstützen
zusätzlich native 44100 Hz. Pilotsuche und AFC bleiben gleich, erhalten aber
die passende Frequenz- und Zeitkalibrierung. Ein- und Ausgabeadapter sowie
CLI-Rundlauf werden mit den originalen 4410-bit/s-Parametern geprüft.
Die 8APSK-Wellenform bleibt im unabhängigen Vergleich unverändert.
[QPSK4410.md](QPSK4410.md) beschreibt Details und Referenztests.
Die benachbarten Originalprojekte wurden nur gelesen und nicht verändert.

## SSTV Martin 1 und Scottie 1

Auf ausdrücklichen Wunsch des Projektinhabers wurden Encoder, Decoder, Timing und
Regressionstests aus seinem lokalen QO100_TRX2-Projekt übernommen:
`decoder/sstv/{sstv.hpp,timing.hpp,encoder.cpp,decoder.cpp}` und
`tests/test_sstv.cpp`. Der Quelltext wurde ohne Entfernen vorhandener Hinweise
übernommen. `external/qo100-sstv/SOURCE.json` nennt Basisrevision, Originalpfade
und Original-Prüfsummen. Die beiden Encoder-Dateien erhalten einen optionalen
48-kHz-Ausgabepfad und Phasenzugriff; Decoder, Timing und Originaltests bleiben
unverändert. Die neuen Audio-, Prozess- und GUI-Adapter stehen wie der übrige
Anwendungscode unter GPL-3.0-or-later. Weitere Einzelheiten: `SSTV.md`.

## Martin 2 und Scottie 2

Die vorhandenen SSTV-Profile wurden um Martin 2 (VIS 40, Farbscan 73,216 ms) und
Scottie 2 (VIS 56, Farbscan 88,064 ms) ergänzt. Zeitprofile und VIS-Werte wurden
mit QSSTV (`src/sstv/sstvparam.cpp`) und PySSTV (`pysstv/color.py`) abgeglichen;
die Quelllinks stehen in `SSTV.md` und im SSTV-Manifest. Daraus wurde kein weiterer
Programmbaustein übernommen. Die Bilder werden wie bei QSSTV mit 320 × 256 Pixeln
aufbereitet und dargestellt. Die verkürzten Scans begrenzen die horizontale
Detailauflösung der analogen Übertragung.

Für die Moduserweiterung wurden im übernommenen Decoder die Liste möglicher Modi, die Größe der
Akquisitionszustände und die anfängliche Martin-Sync-Zuordnung erweitert. Die
ursprünglichen 22 Prüfungen für M1/S1 bleiben unverändert enthalten; neue
unabhängige Referenzsignale prüfen M2/S2 zusätzlich.

## SSTV-Freilauf und Wiedereinrasten

Die lokale Zeilennachführung wurde anschließend für längere Syncausfälle
überarbeitet: durchgehende RGB-Ausgabe, Regression über bis zu 16 Syncpunkte,
getrennte Phasen- und Periodenkorrektur, erweiterte Suche und Bestätigung auch
über ausgelassene Syncimpulse hinweg. Beim Teilempfang hängt der Abbruch nun
zusätzlich vom Ausbleiben des Bildsignals ab. Filter und Tonimpulserkennung
bleiben erhalten. Die Architektur wurde mit QSSTV (`syncprocessor.cpp`,
`modes/modebase.cpp`) verglichen; es wurde kein QSSTV-Programmcode übernommen.
Die bisherigen Regressionstests wurden für die gewünschte durchgehende Ausgabe
angepasst; `tests/test_sstv_sync.cpp` ergänzt unabhängige Signale für alle vier Modi.
Die Prüfsummen im SSTV-Manifest dokumentieren weiterhin die übernommenen Originale.

## Bildtexte

Bedienung, Ebenenmodell und Voreinstellungen wurden mit
`QO100_TRX2/web/sstv-tx.js` und dem Editor in `web/index.html` abgeglichen.
HS-STV verwendet drei Ebenen: eigenes Rufzeichen, Gegenstation und einen von fünf
Freitexten. `text_overlay.*` und `overlay_editor.*` sind native Qt-Neuimplementierungen
unter GPL-3.0-or-later. Die Qt-Version verwendet QPainterPath für weiße Schrift mit
schwarzer Kontur, QSettings für die dauerhafte Ablage sowie Qt-Maus-/Tastaturereignisse.

Bildaufbereitung und Modulationskern bleiben getrennt. Die Texte werden nach dem
Skalieren und vor JPEG-Kodierung bzw. SSTV-Rasterbildung eingezeichnet. Der Editor
arbeitet auf einer Kopie; Abbrechen und Bild-/Formatwechsel verändern das Original
nicht. Ohne ausgefüllte Texte bleibt die bisherige Bildkodierung erhalten.

Die Erweiterung bindet keine Browser-Laufzeit, Linux-Schriften oder nativen
Betriebssystem-APIs ein. Font-Familien werden über Qt-Style-Hints bzw. die
System-Monospace-Schrift gewählt; Unterschiede der installierten Schriftarten sind
möglich. Mauskoordinaten verwenden den jeweiligen Qt-5-/Qt-6-Zugriff. Die Anwendung
wurde auch mit dem PortAudio-Adapter unter Linux gebaut; ein nativer Windows-Build
ist damit nicht ersetzt.

## Waterfall-ID

Auf ausdrücklichen Benutzerwunsch wurden das Rasterverfahren, die Tonkämme mit
Phasenverteilung und die 7×9-Zeichen aus dem lokalen QO100_TRX2 übernommen:
`webbridge/waterfall_tx.cpp` und `web/waterfall-font.json`, Revision
`7af11b830ba83b7d497a89ca3f1492008b212cd9`. Prüfsummen stehen in
`WATERFALL-REFERENCE.json`. `tests/test_waterfall_tx.py` dient als unabhängige
Referenz für Schriftorientierung und Frequenzbänder. Das Original bleibt unverändert.

`waterfall_text.cpp` und `waterfall_font.inc` bilden die portable C++17-Anpassung
unter GPL-3.0-or-later. Anstelle der Bridge-/JSON-Anbindung und des 12-kHz-TX2-
Transportes werden die Zeichen als feste Tabelle eingebunden und Mono-Float-PCM
mit 44,1 oder 48 kHz erzeugt. Das Tonraster und die Schriftorientierung bleiben
erhalten. Für kurze Kennungen werden 100 ms pro Rasterzeile verwendet. Die
Normierung wird an die vorhandene digitale Vollaussteuerung beziehungsweise den
kalibrierten SSTV-Maximalpegel angepasst. Ein gemeinsamer Audio-Stream sendet
Kennung, 300 ms Pause und das unveränderte Bildsignal. Neue Betriebssystem-
Abhängigkeiten entstehen nicht. Bedienung und Prüfungen: `WATERFALL-ID.md`.

Vor der Waterfall-Kennung ergänzt HS-STV einen gehaltenen Punktmarker aus den
beiden oberen Frequenzbändern: fünf aktive Rasterzeilen und eine Leerzeile
(500 ms + 100 ms bei der Standard-Rasterdauer). Anders als ein normales Punkt-
Glyph beginnt dieser Vorlauf sofort mit Audio, damit eine audiogesteuerte PTT
vor dem ersten Buchstaben anziehen kann. Die Samplezählung umfasst den Vorlauf.

## MM-Bake: C++-Textsammler

`src/receive/beacon_messages.cpp` überträgt die CW-/DX-Auswertung aus
`QO100_TRX2/external/mmdecoder/data_collector.py` nach C++/Qt: Typ 8, Untertyp 0
mit Längenbyte für DX-Cluster, Untertyp 2 ohne Längenbyte für CW-Skimmer,
UTF-8/Windows-1252 und Duplikatfenster von 120 Sekunden. Keine Python-Laufzeit,
Tkinter-, HTTP- oder SQLite-Schicht wird übernommen. Die Oberfläche verwendet
Qt-Klartextfenster; Bulletin-Dateidekodierung ist noch nicht enthalten. Native
Testframes prüfen den Signalweg vom 7200-bit/s-Empfang bis zur Textausgabe.

## MM-Bake: native Bulletin-Dekodierung

`src/receive/bulletin_receiver.cpp` überträgt Dateikopf, Blocksammlung, Datei-CRC,
Metadaten und Duplikatregeln aus dem vorhandenen Python-Collector nach C++/Qt.
`beacon_text.cpp` enthält den gemeinsamen UTF-8-/Windows-1252-Decoder. ZIP-Daten
werden ausschließlich im Speicher gelesen; Deflate und CRC32 verwenden zlib.
Der eingeschränkte ZIP-Leser in `bulletin_zip.cpp` folgt der
[PKWARE APPNOTE](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT),
Abschnitte 4.3.7/4.3.9/4.3.12/4.3.16, und verwendet die dokumentierten
[zlib-Schnittstellen](https://zlib.net/manual.html). zlib bleibt eine externe
Bibliothek, eingebunden über CMake `ZLIB::ZLIB`. Die ursprünglichen Python-,
HTTP- und SQLite-Laufzeitkomponenten werden nicht eingebunden.

## PSK-Bake: nativer Dekoder

`external/qo100-psk` übernimmt den vorhandenen C++-Dekoder aus QO100_TRX2.
Herkunft, Revisionen und Änderungen stehen in
[ORIGIN.md](../external/qo100-psk/ORIGIN.md). Die übernommenen Komponenten sind
GPL-3.0-only; ihre SPDX-Hinweise und vollständige Lizenz bleiben erhalten.
Der damit verknüpfte Dekoder wird unter GPL v3 vertrieben.
Die neue Soundkarten-, Prozess- und Qt-Anbindung ist GPL-3.0-or-later.

Es wird keine Python-Laufzeit für die PSK-Bake eingebunden. FFTW wird durch die
vorhandene liquid-dsp-Bibliothek ersetzt. Die Prüfung vergleicht alle Nutzlasten
mit QO100_TRX2 sowie unabhängigen GNU-Radio/gr-satellites-FEC-Vektoren.
Details: [PSK-BAKE.md](PSK-BAKE.md).
