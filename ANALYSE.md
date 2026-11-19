# mm-sstv – Analyse vor der Implementierung

Stand: 19.11.2026 · Zielverzeichnis: `/Daten/funk/sat/mm-sstv`

## Ergebnis und Abgrenzung

Die technische Grundlage für Schritt 2 (8APSK-Testsender mit 100-Hz-Pilot) und Schritt 3 (Bildversand an HS-Modem) ist vorhanden. Die Implementierung kann auf Basis der lokalen Originalquellen erfolgen. Die Wiki-Dokumentation allein reicht für eine exakte Umsetzung nicht aus: Sie enthält widersprüchliche Angaben zu Dateikopf und Bytepositionen sowie eine unvollständige Beschreibung der Sendekette. Zusätzlich widersprechen einzelne Quellcodekommentare zur Statusbelegung dem ausgeführten Code.

Es wurde ausschließlich analysiert. Es gibt noch keinen mm-sstv-Programmcode, kein GUI und keine Audio- oder HF-Aussendung. Beide bestehenden Projekte wurden nicht verändert. Ein Nachweis der geforderten vollständigen Kompatibilität ist erst mit dem implementierten Sender und der vorgesehenen Prüfung gegen HS-Modem möglich.

Für den ersten Test wird **8APSK mit 6000 bit/s, 2000 Symbolen/s und intern 48 kHz** vorgeschlagen. Das entspricht einem existierenden HS-Modem-Modus und passt einschließlich Pilot in ungefähr 2,6 kHz Frequenzspanne. 7200 bit/s wird vorerst nicht als Voreinstellung vorgesehen.

## 1. Untersuchte Grundlagen

### HS-Modem

Lokaler Bestand: `/Daten/funk/hsmodem`. Dort ist kein Git-Repository vorhanden; eine Commit-ID dieses Bestands lässt sich deshalb nicht verlässlich nennen. Die tatsächlich untersuchten Dateien werden in `QUELLEN.json` mit SHA-256 festgehalten.

Wesentliche Dateien:

| Bereich | Fundstellen unter `/Daten/funk/hsmodem/` |
|---|---|
| Modulation und Filter | `hsmodem/liquid_if.cpp` |
| Betriebsarten, Frameversand und Wiederholungen | `hsmodem/hsmodem.cpp` |
| Pilot | `hsmodem/tuning.cpp` |
| Bit-/Symbolreihenfolge | `hsmodem/constellation.cpp` |
| Frameaufbau | `hsmodem/frameformat.h`, `hsmodem/frame_packer.cpp` |
| Fehlerkorrektur und Prüfsumme | `hsmodem/fec.cpp`, `hsmodem/fec/`, `hsmodem/crc16.cpp` |
| Scrambler | `hsmodem/scrambler.cpp` |
| Audioausgabe | `hsmodem/libkmaudio/`, insbesondere `libkmaudio_interface.cpp`, `libkmaudio_playback_linux.cpp` |
| Dateikopf und Fragmentierung | `oscardata/oscardata/ArraySend.cs` |
| Bildaufbereitung | `oscardata/oscardata/imagehandler.cs`, `oscardata/oscardata/Form1.cs` |
| Gegenprüfung am Originalempfänger | `oscardata/oscardata/receivefile.cs` |
| Alternative Dateisendefunktion | `hsmodem/filesend.cpp` |
| Tatsächliche 8APSK-Konstellation | `3rdParty/liquid-dsp/src/modem/src/modem_apsk_const.c`, `modem_apsk.c` |

### QO100_TRX2

Untersuchter Git-Stand: `7af11b830ba83b7d497a89ca3f1492008b212cd9`.

Der Arbeitsbaum hatte zwei unversionierte Dateien (`nano.19161.save`, `other_vhosts_access.log`); diese wurden nicht verändert oder benötigt.

Besonders relevant sind:

- `external/mmdecoder/`: bereits isolierter HS-Modem-Empfangskern mit Frame-, Reed-Solomon- und CRC-Prüfung sowie synthetischem Testsignalerzeuger.
- `external/mmdecoder/UPSTREAM.md`: dokumentiert die Herkunft der übernommenen HS-Modem-RX-Bausteine aus Commit `cbd30059c18607654c1530b7606c18e375b7f74c` vom 24.10.2022. Diese Herkunftsangabe gilt für diesen RX-Bestand, nicht automatisch für `/Daten/funk/hsmodem`.
- `external/liquid-dsp-1.8.3/`: gepinnte Header und Linux-x86-64-Bibliotheken; die Implementierungsquellen sind dort nicht enthalten.
- `audio/digital_audio.cpp`, `docs/digital-audio.md`: virtuelle Soundgeräte `toQO100` und `fromQO100` als Anschluss für externe Digitalprogramme.
- `src/tx_channel.cpp`: TX2-Digitalprofil, USB-Filter von 100 bis 4000 Hz, ohne Sprachkompressor.
- `docs/mm-beacon.md`, `decoder/mm_beacon.py`: vorhandene 8APSK-Empfangsdiagnose und Pilotnachführung.

### Dokumentation

Gelesen wurden die HS-Modem-Startseite und die von ihrer Navigation direkt verlinkten Fachseiten: Grundlagen, Installation, Audioanschluss, Bedienung, Bild-/Dateitransfer, TRX-/SDR-Einstellungen, Streaming sowie alle acht Entwicklerkapitel. Die Navigation ergab 49 eindeutige HS-Modem-Seiten-IDs. Zwei davon sind leere Impressums-/Datenschutzseiten; 47 lieferten Fach-/Übersichtsinhalte, einschließlich der englischen Startseite.

Das vollständige URL-Verzeichnis steht in `QUELLEN.json`. Besonders maßgeblich:

- [Physical parameters](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:ota)
- [Frame Coding/Decoding](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:scrambler)
- [Frame Format](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:packer)
- [GUI Interface](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:guiinterf)
- [App → Modem](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:guidata)
- [File Format](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:filetx)
- [Modem → App](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:guirx)
- [External Data Interface](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:extdata)

Die Seiten mit historischen Betriebsfrequenzen und Installationshinweisen wurden als Bestandsdokumentation behandelt. Daraus wird keine aktuelle Betriebsfrequenz oder aktuelle Satellitenregel abgeleitet.

## 2. Physikalischer Sendepfad

Die folgende Beschreibung folgt dem lokalen Originalcode.

```text
Zufallsbits (Schritt 2) / vollständige Frames (Schritt 3)
    → je 3 Bit zu einem Symbol 0…7, MSB zuerst
    → liquid-dsp LIQUID_MODEM_APSK8
    → RRC-Interpolation
    → komplexes Hochmischen auf 1500 Hz
    → Audiosignal aus Realteil + Imaginärteil
    → 100-Hz-Pilot addieren
    → gemeinsame digitale Pegeleinstellung
    → Audiogeräte-Adapter
```

### Konstellation und Symbolzuordnung

`getMod()` in `liquid_if.cpp` wählt `LIQUID_MODEM_APSK8`, nicht einen 8PSK-Modulator. Es wird normale, nicht differenzielle Modulation verwendet. Die für QPSK im Original vorhandene zusätzliche Gray-Umsetzung wird bei 8APSK nicht ausgeführt.

Die Konstellation besteht aus **einem Punkt bei 0 und sieben Punkten auf einem äußeren Kreis**. Der äußere Radius ist ungefähr `1.06904495`, sodass die mittlere Symbolenergie bei gleichverteilten Symbolen normiert ist. Die symbolabhängige Abbildung ist Bestandteil des Kompatibilitätsformats.

| Symbol | Realteil | Imaginärteil |
|---:|---:|---:|
| 0 | 0 | 0 |
| 1 | 0,666539 | 0,835813 |
| 2 | −0,963176 | 0,463841 |
| 3 | −0,237885 | 1,042242 |
| 4 | 1,069045 | 0 |
| 5 | 0,666538 | −0,835813 |
| 6 | −0,963176 | −0,463841 |
| 7 | −0,237885 | −1,042242 |

Diese Werte wurden zusätzlich direkt aus der vorhandenen liquid-dsp-1.8.3-Bibliothek abgefragt und passen zur Abbildung im alten liquid-Quellcode. Ein in sich gedrehtes Konstellationsbild ist wegen der unbekannten Empfangsphase möglich; entscheidend sind sieben äußere Wolken plus Mittelpunkt.

`convertBytesToSyms_8PSK()` trägt einen historischen Namen, führt aber nur die MSB-zuerst-Gruppierung durch. Je drei Bytes liefern acht Symbole. Für `53 E1 A6` entstehen die Symbole `2 4 7 6 0 6 4 6`.

### Betriebsarten

Aus der Tabelle `sr[]` in `hsmodem.cpp` ergeben sich:

| HS-Modem-Anzeige | Audio-Samplerate | Samples/Symbol | Tatsächliche Symbolrate | Tatsächliche Bruttobitrate | RRC-Bandbreite, rechnerisch |
|---|---:|---:|---:|---:|---:|
| 5500 | 44100 Hz | 24 | 1837,5 Bd | 5512,5 bit/s | 2205 Hz |
| 6000 | 48000 Hz | 24 | 2000 Bd | 6000 bit/s | 2400 Hz |
| 6600 | 44100 Hz | 20 | 2205 Bd | 6615 bit/s | 2646 Hz |
| 7200 | 48000 Hz | 20 | 2400 Bd | 7200 bit/s | 2880 Hz |

Die Bandbreitenspalte ist aus `Symbolrate × (1 + 0,2)` berechnet. Sie beschreibt den idealen RRC-Datenkanal; endliche Filter und die tatsächlich gemessene belegte Bandbreite sind davon zu unterscheiden. Der Pilot ist darin noch nicht gesondert berücksichtigt.

Bei 6000 bit/s liegt das Datensignal nominell von 300 bis 2700 Hz. Zusammen mit dem 100-Hz-Pilot entsteht eine Spanne von etwa 2600 Hz. Bei 7200 bit/s liegt das Datensignal rechnerisch von 60 bis 2940 Hz. Die Wiki-Bezeichnung dieses Modus als 2700-Hz-Modus ist deshalb keine ausreichende Grundlage für eine feste 2,7-kHz-Auslegung. Der vorhandene QO100_TRX2-Bakenempfänger ist auf 7200 bit/s zugeschnitten und wäre ohne Anpassung kein Referenzempfänger für unseren vorgeschlagenen 6000-bit/s-Test.

### RRC-Filter

Die Originalparameter lauten:

- Typ: `LIQUID_FIRFILT_RRC`.
- `m_filterDelay_Symbols = 15`.
- Roll-off `beta_excessBW = 0.2`.
- Koeffizientenanzahl `2 × Samples/Symbol × 15 + 1`; bei 6000 bit/s sind es 721.
- `tau_FracSymbOffset` beginnt bei `−0.2`, wird vor dem Filterentwurf aber auf `0.8` normalisiert. Aus `k × tau` wird nur der gebrochene Sampleanteil `dt` an `liquid_firdes_prototype()` übergeben; der ebenfalls berechnete ganzzahlige Anteil `ds` wird im Original nicht verwendet. Bei `k=24` ergibt sich ungefähr `dt=+0.2`, bei `k=20` ungefähr `dt=0`.

Für eine originalnahe Nachbildung ist die tatsächlich ausgeführte Berechnung maßgeblich. Nur den Anfangswert `−0.2` als Filteroffset zu übernehmen, bildet den Originalpfad nicht exakt nach.

### Audiomischer, Pilot und Pegel

Der Daten-NCO läuft mit 1500 Hz bei der jeweiligen internen Audio-Samplerate. Nach dem Hochmischen wird `c.real + c.imag` als reelles Audio verwendet. Der Originalcode ruft pro Sample zuerst `nco_crcf_step()` und dann die Mischung auf.

Der Pilot stammt aus `singleFrequency()` in `tuning.cpp`: NCO bei **100 Hz**, Sinus, ein Schritt je ausgegebenem Sample. `marker` ist im Original initial eingeschaltet. Während der Datenübertragung gilt sinngemäß:

```text
audio[n] = pbvol × 0.1 × (Re(c[n]) + Im(c[n]) + 0.25 × sin(pilotphase[n]))
```

Damit beträgt die nominelle Pilot-Spitzenamplitude `0.025 × pbvol`, vor eventueller Sampleratenumsetzung und weiterer Verstärkung im Betriebssystem. Der Faktor 0,25 beschreibt die Addition zum Datenaudio und ist kein pauschales Verhältnis der gemessenen Leistungen beider Signalanteile.

`getMarker()` in derselben Modulatordatei erzeugt einen anderen, im untersuchten TX-Pfad nicht verwendeten Oszillator. Diese Funktion darf nicht mit dem tatsächlich zugemischten 100-Hz-Pilot verwechselt werden.

Pilot und Daten müssen beim gemeinsamen Pegelregler zusammen skaliert werden. Bei STOP muss beides enden. DSP-Zustände müssen innerhalb einer Sendung über alle Audioblöcke und Frames erhalten bleiben. Begrenzung einzelner Samples, Sprachkompression und automatische Pegelregelung würden die Konstellation verändern.

### Audioanbindung

Das aktuelle Original benutzt unter Linux libsoundio über `libkmaudio`; unter Windows liegt dort ein separater PortAudio-Pfad. Das Modem liefert intern Mono-Float-Samples. Der Linux-Ausgabeadapter schreibt sie auf die Kanäle des geöffneten Geräts und setzt bei Bedarf auf dessen tatsächliche Samplerate um.

Für mm-sstv ist die konkrete Original-Audiobibliothek keine Protokollvorgabe. Entscheidend sind korrekte Frequenzen, Symboltakt, ein lückenloser Strom und lineare Ausgabe. Der neue Kern sollte fest mit 48 kHz rechnen; erforderliche Geräteumsetzung gehört an die Audiogrenze.

## 3. Funkframes für den späteren Bildversand

### Frameformat

Ein vollständiger Funkframe hat 258 Byte bzw. 688 8APSK-Symbole. Nachfolgende Offsets sind nullbasiert und beziehen sich auf den gesamten Funkframe:

| Byteposition | Länge | Bedeutung vor Scrambling |
|---|---:|---|
| 0–2 | 3 | Fester Synchronkopf `53 E1 A6` |
| 3 | 1 | Framezähler, Bits 0–7 |
| 4 | 1 | Typ, Status und Zähler-Bits 8–9 |
| 5–223 | 219 | Nutzlast |
| 224–225 | 2 | CRC16, höchstwertiges Byte zuerst |
| 226–257 | 32 | Reed-Solomon-Prüfbytes |

Das Managementbyte an Position 4 wird **nach Code** so gebildet:

```text
((framecounter >> 8) & 3) << 6 | (status & 3) << 4 | (type & 15)
```

Für Bilder ist `type=2`. Der Status ist ein Zweibitwert:

| Wert | Bedeutung |
|---:|---|
| 0 | Erster Frame |
| 1 | Folgeframe |
| 2 | Letzter Frame |
| 3 | Einziger Frame |

Die Kommentare in `frameformat.h`, welche einzelne First-/Last-Flags beschreiben, stimmen mit `Pack()` und dem Empfangscode nicht überein. Der Zweibitwert ist maßgeblich.

### Verarbeitungsreihenfolge

1. Managementbytes und 219-Byte-Nutzlast ergeben 221 Byte.
2. CRC16 über diese 221 Byte berechnen und zwei Byte MSB zuerst anhängen: 223 Byte.
3. Reed-Solomon ergänzen: 255 Byte.
4. Alle 255 Byte mit der festen Originaltabelle XOR-verknüpfen; Tabellenindex bei jedem Frame wieder bei 0 beginnen.
5. Den ungescrambelten Synchronkopf voranstellen: 258 Byte.
6. MSB zuerst in Dreiergruppen zerlegen und modulieren.

Das Scrambling ist im TX-Ablauf des Wiki-Framekapitels ausgelassen, obwohl es beim RX beschrieben wird. `Pack()` zeigt die notwendige Position hinter FEC eindeutig.

CRC16 ist vollständig spezifiziert: Startwert `0xFFFF`, reflektiertes Polynom `0x8408`, Verarbeitung LSB zuerst innerhalb eines Bytes, keine abschließende XOR-Verknüpfung; Übertragung des Ergebnisworts MSB zuerst.

FEC: Reed-Solomon `(255,223)` über GF(2^8), 32 Prüfbytes, Generatorwurzelindex 120 und 32 aufeinanderfolgende Wurzeln, primitives Polynom `x^8 + x^7 + x^2 + x + 1` (`0x187`, Schifra `primitive_polynomial06`). Ein beliebiger RS-(255,223)-Encoder mit anderen Feld-/Generatorparametern wäre nicht austauschbar.

Der Scrambler ist keine frei wählbare PRBS. Die ersten 255 Byte der Originaltabelle müssen unverändert verwendet werden.

### Zähler und Wiederholungen

Der Zähler hat 10 Bit. Bei Status 0 setzt das Original ihn auf 0, normale Folgeframes erhöhen ihn, Wiederholungen behalten ihn bei. Das Original setzt den Zähler bei Status 3 nicht ausdrücklich zurück; der Originalempfänger besitzt dazu einen Kompatibilitätspfad. Für den neuen Sender ist Frame 0 auch beim Einzelframe die klare Vorgabe; dieser Randfall muss später separat gegen HS-Modem geprüft werden.

Erster bzw. einzelner und letzter Frame werden vom Original wiederholt. Der Anfang dient zugleich der Akquisition. `toGR_sendData(...,5)` bedeutet im untersuchten Code einen ersten Versand plus fünf Wiederholungen; bei zuvor ruhender Audioausgabe wird die Zahl anhand einer ungefähr sechssekündigen Akquisitionsphase gegebenenfalls erhöht. Es existiert hier keine gesonderte, fest codierte Bitpräambel.

Das Original fügt auch Stationsinformationen mit Typ 7 ein. Deren Zählerbehandlung darf den Bildzähler nicht verschieben. Sprachansage und Stationsinformation sind vom Bildformat zu unterscheiden und müssen nicht als zusätzliche GUI-Bereiche übernommen werden. Für Schritt 3 ist eine ausreichende Akquisitionsphase nötig, damit der Empfänger den Dateikopf zuverlässig erfasst.

## 4. Bildcontainer und Fragmentierung

Das Original wandelt Bilder vor dem Versand in JPEG um. Die JPEG-Daten werden **ohne zusätzlichen ZIP-Container** übertragen. Auflösung und JPEG-Qualität sind Eigenschaften der Bilddatei, keine eigenen Funkprotokollfelder. Die [Dateiformat-Dokumentation](https://wiki.amsat-dl.org/doku.php?id=en:hsmodem:filetx) beschreibt JPEG und die Trennung vom ZIP-Dateitransfer.

`ArraySend.AddHeader()` und `GetAndRemoveHeader()` liefern denselben 55-Byte-Dateikopf. Die folgende Tabelle bezieht sich auf die **Nutzlast des ersten Frames**, nicht auf die Funkframe- oder UDP-Position:

| Nutzlastposition | Länge | Inhalt |
|---|---:|---|
| 0–49 | 50 | Dateiname, ASCII, maximal 50 Byte, Rest Nullbytes |
| 50–51 | 2 | Datei-ID: CRC16 über den vollständigen JPEG-Dateiinhalt, MSB zuerst |
| 52–54 | 3 | Länge des JPEG-Inhalts ohne Dateikopf, 24 Bit, MSB zuerst |
| 55–218 | 164 | Erste JPEG-Bytes |

Weitere Frames tragen je 219 Dateibytes. Der letzte Frame wird mit Nullbytes aufgefüllt; die Dateilänge bezieht sich ausschließlich auf die JPEG-Datei. Passt die Datei einschließlich Kopf in 219 Byte, also JPEG-Länge höchstens 164 Byte, ist Status 3 vorgesehen. Die Frameanzahl lautet `ceil((JPEG-Länge + 55) / 219)`.

Damit sind folgende Fehler bzw. Abweichungen im Wiki geklärt:

- Für den ersten Frame verbleiben 164 Dateibytes, nicht 163.
- Die Dateigröße überlappt nicht mit der Datei-ID.
- Bei einem App→Modem-Datagramm mit zwei vorangestellten Verwaltungsbytes belegt die Dateigröße Position 54–56, die erste JPEG-Nutzlast beginnt an Position 57.
- Das komplette App→Modem-Datagramm ist 221 Byte lang; seine Nutzlast liegt an Position 2–220, nicht 2–221.
- Die lokale GUI berechnet die Datei-ID über den Dateiinhalt. Eine bloße CRC über den Namen würde verschiedene Bilder mit gleichem Namen nicht unterscheiden.

Die 10-Bit-Framezähler erlauben nominell 1024 × 219 − 55 = 224201 JPEG-Bytes, wenn die Bildframes allein durchgezählt werden. Das ist keine Empfehlung, die dokumentierte Dateigrenze auszuschöpfen: Für mm-sstv bietet sich zunächst eine eindeutig definierte Obergrenze von **200000 JPEG-Bytes** an, unterhalb der im Wiki genannten 200 kB. Die JPEG-Qualität muss bei großen Bildern entsprechend sinken oder der Versand muss mit einer verständlichen Meldung abgewiesen werden.

640×480 ist keine Grenze des Funkformats. Die Original-GUI verwendet üblicherweise 320×240 und 640×480. Größere auswählbare Formate bleiben grundsätzlich möglich, benötigen aber Prüfung mit der Originalanzeige und unterliegen derselben Dateigrößengrenze.

Die Bildskalierung im Original erhält das Seitenverhältnis und passt in ein Zielrechteck. Für die gewünschte exakte Ausgabegröße von beispielsweise 640×480 wird als Vorgabe empfohlen: proportional einpassen und freie Ränder auffüllen. Damit ist die ausgegebene Datei exakt so groß wie ausgewählt. Beschneiden oder Verzerren ist eine spätere UI-Entscheidung, kein offener Protokollpunkt.

## 5. Nutzbarkeit von QO100_TRX2

### Audioverbindung

Für die praktische Anbindung ist bereits vorhanden:

```text
mm-sstv → Audioausgang toQO100 → QO100_TRX2-Audioadapter → TX2 → Pluto
```

Später ist für Empfang der Gegenweg `fromQO100` nutzbar. Die Namen gelten aus Sicht des externen Digitalprogramms. Der Adapter verwendet virtuelle Geräte im PulseAudio- bzw. PipeWire-Pulse-Server derselben Benutzersitzung; für mm-sstv müssen sie als Geräte auswählbar sein. Eine zusätzliche direkte Pluto- oder QTR2-UDP-Anbindung ist für das vereinbarte Soundkartenkonzept nicht nötig.

Der TX2-Digitalpfad arbeitet ohne Sprachkompressor. Eine praktische Besonderheit bleibt: Seine untere Filtergrenze liegt bei 100 Hz, also gerade auf dem Pilot. Die tatsächliche Pilotdämpfung durch diesen und weitere Stationsfilter muss beim späteren Signaltest gemessen werden. Die Untersuchung hat hier keinen kompletten Durchlasstest der angeschlossenen Station durchgeführt.

Der beim Bakenempfänger dokumentierte Pilot bei 500 Hz ist eine andere Empfangsabstimmung des gesamten Signals. Dort liegen Pilot und Datenmitte weiterhin 1400 Hz auseinander. Das ist keine Aufforderung, in mm-sstv nur den 100-Hz-Pilot auf 500 Hz zu versetzen. Unser Sender soll bei 100/1500 Hz bleiben.

Der vorhandene Audioadapter kann TX2 anhand offener Wiedergabeströme aktiv halten. Das spätere START/STOP-Verhalten soll deshalb auch den Wiedergabestrom passend öffnen und schließen; Nullen in einem dauerhaft offenen Stream haben für diesen Aktivitätsmechanismus eine andere Bedeutung.

### Offline-Referenz

Vorhandene Programme `build/bin/qo100_mm_probe` und `qo100_mm_fixture` ermöglichen Tests ohne Soundkarte oder HF. Ausgeführt wurden die 16 vorhandenen Tests in `external/mmdecoder/tests/test_rx.py`; alle waren erfolgreich (3,627 s). Sie prüfen unter anderem saubere Frames, falsche CRC, Stille, Rauschen, Frequenzversatz, Drift und Wiederakquisition. Das vollständige Ergebnis steht in `OFFLINE-TESTS.log`.

Das bestätigt die aktuelle Verwendbarkeit dieser Testhilfe. Es beweist weder einen noch nicht existierenden mm-sstv-Sender noch die Bildrekonstruktion durch oscardata.

Der synthetische Erzeuger ist eine Testhilfe für den 7200-bit/s-RX-Pfad. Seine RRC-Offsetbehandlung und Pilotamplitude unterscheiden sich vom oben ermittelten Original-TX-Pfad. Er darf daher nicht ungeprüft als exakte Vorlage für unseren Sender übernommen werden.

## 6. Vorgeschlagene portable Struktur

Die vorhandenen Quellen sprechen für **C++ mit CMake**, einen von der GUI unabhängigen Kern und explizite Audio-/Bildadapter. Dies ist ein Entwurf, keine bereits festgelegte oder implementierte Abhängigkeit.

```text
core/phy          Symbolabbildung, RRC, NCO, Pilot, Float-Audioblöcke
core/protocol     Framebytes, CRC, FEC, Scrambler, Bildfragmentierung
core/transfer     Start/Stop-Zustand, Sequenzierung, Wiederholungen
ports             Schnittstellen für Audiogeräte, Bildkodierung, Einstellungen
adapters/audio    Geräteauswahl, Formatumsetzung, Linux/Windows/macOS-Backend
adapters/image    Laden, Skalieren, JPEG-Erzeugung
ui                START/STOP; später Bildvorschau, Größenwahl und Senden
```

Der Kern erhält und liefert Datenblöcke; er kennt weder ALSA/PulseAudio noch Win32/CoreAudio oder GUI-Objekte. Die Protokollbytes werden ausdrücklich serialisiert, ohne Struct-Packing oder Host-Byteordnung vorauszusetzen. Lebensdauer und Abbruchzustand gehören pro Senderinstanz in Objekte statt in globale Register und statische Rückgabepuffer wie im Original.

Als gemeinsame GUI- und Adapterbasis bietet sich Qt Widgets mit Qt Multimedia an: [QMediaDevices](https://doc.qt.io/qt-6/qmediadevices.html) liefert die Geräteauswahl, [QAudioSink](https://doc.qt.io/qt-6/qaudiosink.html) die Ausgabe auf ein ausgewähltes Gerät. Der GUI-unabhängige Audioport bleibt austauschbar. Die Sichtbarkeit von `toQO100`, unterstützte Formate und Abbruchlatenz müssen im konkreten Linux-Backend geprüft werden.

Auf dem untersuchten Rechner sind Qt5Widgets 5.15.13 und PortAudio/libpulse über pkg-config auffindbar; Qt6Widgets und die geprüften Qt-Multimedia-pkg-config-Module sind nicht auffindbar. Der vorhandene liquid-Bestand ist ein Linux-x86-64-Binärpaket. Für andere Betriebssysteme und Architekturen werden passende Builds benötigt. Diese Punkte betreffen die Bauumgebung, nicht das Funkformat; in diesem Schritt wurde nichts installiert.

Für Schritt 2 bleibt das Hauptfenster beim gewünschten einzelnen START/STOP-Button. Soundkartenauswahl und Pegel können in einem Setup-Dialog liegen. Ein fehlendes oder getrenntes Ausgabegerät muss zu einer verständlichen Fehlermeldung führen. Der Audiopuffer muss kurz und begrenzt sein, damit STOP tatsächlich zeitnah wirkt; der Originalpfad mit größeren Warteschleifen sollte dafür nicht unverändert übernommen werden.

## 7. Was noch offen ist

| Punkt | Einordnung / sinnvoller Ausgangspunkt |
|---|---|
| Erster Testmodus | Vorschlag: 6000 bit/s; die vorhandene HS-Modem-Installation muss auf denselben Modus eingestellt sein. |
| Konkreter Audioausgang | Spätestens für den Praxistest wählen: etwa `toQO100` oder eine physische USB-Soundkarte. Keine fehlende Protokollinformation. |
| PTT | Im bisherigen Auftrag ist Audioausgabe beschrieben. Manuelle PTT bzw. die vorhandene Stationslösung genügt für den Test; CAT/GPIO ist nicht Teil der ersten Implementierung. |
| Bildgrößen und Randbehandlung | Für Schritt 3 als einfache Vorgabe z. B. 320×240 und 640×480, proportional mit Rändern. Weitere Größen später nach Wunsch. |
| Abnahme | Schritt 2: saubere Konstellation samt Pilot. Schritt 3: vollständiges JPEG in der bestehenden HS-Modem-Installation. |

Keiner dieser Punkte verhindert die Vorbereitung des Testsenders. Die Bestätigung des Modus und die Gerätewahl können im nächsten Arbeitsschritt erfolgen.

## 8. Herkunft und Abhängigkeiten

Die Original-HS-Modem-Dateien enthalten GPL-2.0-or-later-Hinweise. Teile von libkmaudio tragen zusätzlich einen GPL-3-Hinweis. liquid-dsp liegt mit MIT-Lizenz vor. Schifra trägt eigene Lizenzhinweise; die [Herstellerseite](https://www.schifra.com/license.html) unterscheidet eine GPL-v2-Nutzung für die dort beschriebene Nutzergruppe und eine kommerzielle Lizenz. Bei späterer Quellübernahme gehören die jeweils zutreffenden Hinweise und Lizenztexte mit zum Projekt. Für den reinen Modulationstest ist Schifra noch nicht erforderlich.

## 9. Prüfbasis für die folgenden Schritte

Schritt 2 soll zuerst intern ohne HF überprüfbar sein: gleichverteilte Dreierbitgruppen, korrekte Konstellation, durchgehender Symboltakt, RRC-Spektrum, 1500-Hz-Datenmitte, 100-Hz-Pilot und gemeinsamer linearer Pegel. Wiederholtes START/STOP und Gerätefehler gehören zur Prüfung des Audioadapters. Die Zufallsfolge braucht weder Bildframes noch eine vorgetäuschte BER-Erfolgsmeldung im HS-Modem; die gewünschte Abnahme betrifft zunächst die Konstellation.

Für Schritt 3 folgt ein bytegenauer Vergleich von Frames mit dem Originalpacker einschließlich CRC, FEC und Scrambler. Erst anschließend werden JPEG-Kopf, Fragmentierung, Akquisitionsphase und Wiederholungen ergänzt. Die Randfälle Einzelframe, exakt gefüllter letzter Frame, Zähler-Bitwechsel und Größenbegrenzung verdienen gezielte Prüfungen. Der vorhandene Offline-Decoder kann nach Anpassung an den gewählten Symboltakt als zusätzliche Gegenstelle dienen.

Die eigentlichen QO100-Prüfungen bleiben wie vereinbart beim Benutzer. Die Analyse ist abgeschlossen; Schritt 2 wurde nicht begonnen.
