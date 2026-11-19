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
normalisierung ersetzt (Faktor ungefähr 0,317353). Das erhöht Daten und Pilot
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
