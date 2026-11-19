# mm-sstv – digitale Bildübertragung mit HS-Modem

Bilder über auswählbare Soundkarten mit HS-Modem senden und empfangen.
**8APSK · 6000 bit/s oder QPSK · 4410 bit/s.**
Datenmitte 1500 Hz · Referenzträger 100 Hz · HS-Modem-kompatibel.

## Start

```bash
git clone https://github.com/dj0abr/mm-modem.git
cd mm-modem
./start.sh
```

Das Programm beginnt ohne Aussendung. Der Audioausgang ist standardmäßig
`toQO100`; eine gespeicherte Geräte- und Pegeleinstellung wird wiederverwendet.
Unter **Setup → Audioausgang und Pegel…** sind beide einstellbar.
**100 % entspricht weiterhin digitaler Vollaussteuerung.** Daten und Pilot werden
gemeinsam geregelt, ohne AGC oder Abschneiden von Spitzen.

Das Fenster startet mit höchstens **900 × 560 Pixeln**, angepasst an den verfügbaren
Bildschirm, und lässt sich bis **640 × 440 Pixel** verkleinern. Formatwahl und
**Senden/STOP** liegen direkt über dem Sendebild; Die flachen Fortschrittsbalken stehen
unter dem jeweiligen Bild: **TX rot**, **RX grün**. Die Signalanzeige ist kompakt darunter angeordnet.
Lange Dateinamen und Statusmeldungen werden abgekürzt und sind per Tooltip
vollständig lesbar. Die Vorschauen passen sich dem Fenster an; die gewählte
Größe des übertragenen Bildes bleibt davon unabhängig.

## Empfangen

Der Empfang startet automatisch mit dem Eingang **fromQO100**. Im Setup lassen
sich Audioeingang und Audioausgang getrennt auswählen. Der Eingang muss vom
laufenden QO100_TRX2-Audioadapter bereitgestellt werden; für `fromQO100` die
gewünschte Frequenz im **RX2**-Kanal einstellen.

Rechts im Fenster erscheinen Pilot-/Daten-Synchronisation und das empfangene Bild.
Die Anzeige wächst während der Übertragung blockweise. Fehlende Blöcke bleiben
im Fortschritt sichtbar und können bei einer erneuten Aussendung ergänzt werden.
Der Empfang ist immer aktiviert. Nach einem Ausfall des gewählten Eingangs wird
alle drei Sekunden erneut verbunden. Senden und Empfangen können gleichzeitig
laufen; **STOP** betrifft ausschließlich die Sendung.

Unter den Bildern zeigt ein Live-Monitor das **Empfangsspektrum von 0–4000 Hz**
und die **Konstellation der gewählten Modulation**. Der Pilotsuchbereich ist schattiert, der erkannte
Pilot grün markiert, die Grenzen des aktuellen Datenbands gelb gestrichelt.
Die Konstellationspunkte erscheinen bei bestätigter Datensynchronisation grün.

Ein vollständiges Bild lässt sich mit **Bild → Empfangsbild speichern…** als
originales JPEG ablegen. Nicht vollständige Blöcke bleiben für Wiederholungen
innerhalb der laufenden Sitzung im Speicher; sie werden nicht über einen
Programmneustart hinweg gespeichert.

Der RX-Kern mit Pilotsuche/AFC stammt direkt aus QO100_TRX2. Er wird mitgebaut und
benötigt keine getrennte Installation des Transceiverprogramms oder des alten
HS-Modems. Geräte auflisten, ohne Aufnahme oder Sendung zu starten:

```bash
./start.sh --list-inputs
```

Unten rechts zwischen **8APSK · 6000 bit/s** und **QPSK · 4410 bit/s** wählen.
Die Auswahl gilt für Sender und Empfänger und wird gespeichert. Während einer
Sendung ist sie gesperrt. **Beide Gegenstellen gleich einstellen.** 8APSK bleibt
der Standard, solange keine andere Wahl gespeichert ist.

QPSK 4410 verwendet wie HS-Modem 2205 Symbole/s und 44,1-kHz-Audio. Es ist für
schwächere Signale vorgesehen und besitzt etwa 2646 Hz Datenbandbreite; 8APSK 6000
verwendet 48 kHz, 2000 Symbole/s und etwa 2400 Hz Datenbandbreite. Die Software
stellt die Audioadapter automatisch passend ein. Der 100-Hz-Referenzträger und
100 % als digitale Vollaussteuerung bleiben erhalten.
[Technik und Prüfungen von QPSK 4410](docs/QPSK4410.md).

## Bild senden

1. Ein Bild in die Vorschau ziehen, die Vorschau anklicken oder **Bild → Bild öffnen…** wählen.
2. **Bildgröße** auswählen: 320×240, 640×480, 800×600 oder 1024×768.
3. Vorschau, JPEG-Dateigröße und ungefähre Sendedauer prüfen, dann **Senden** drücken.

Das Bild wird proportional in das gewählte Format eingepasst; freie Ränder werden
weiß aufgefüllt. Die Vorschau zeigt das tatsächlich kodierte JPEG. Es werden keine
Rufzeichen oder sonstigen Texte in das Bild eingefügt. Die JPEG-Qualität wird
bei Bedarf automatisch reduziert, damit die Datei höchstens 200000 Byte groß ist.

Während des Sendens zeigt der Button **STOP** und der Fortschrittsbalken läuft.
Der Anfang enthält ungefähr 7,6 Sekunden bei 8APSK bzw. 10,3 Sekunden bei QPSK zur Akquisition, der letzte Frame wird
mehrfach wiederholt. Das erklärt einen Teil der angezeigten Sendedauer.
Am normalen Ende werden die letzten Audiosamples vollständig abgespielt und der
Audiostream automatisch geschlossen. **STOP** verwirft den Puffer sofort; ein
Fensterschließen beendet ebenfalls die Ausgabe.

„Sendung beendet“ bestätigt die lokale Audioausgabe, keine Empfangsbestätigung.
Den korrekten Bildempfang bitte mit der vorhandenen HS-Modem-Installation prüfen.
Der Bildempfang in mm-sstv läuft unabhängig davon weiter.

Die Auswahl von Sendeausgang und Sende-Bildformat wird während einer laufenden Aussendung
gesperrt; der Sendepegel kann im Setup weiterhin verändert werden. Bei fehlendem
gewähltem Gerät findet kein automatischer Wechsel auf eine andere Soundkarte statt.
Nach erneutem Anschluss im Setup **Aktualisieren** drücken.

## Anschluss

```text
mm-sstv → toQO100 → QO100_TRX2-Audioadapter → TX2 → Pluto
mm-sstv ← fromQO100 ← QO100_TRX2-Audioadapter ← RX2 ← Pluto
```

Die virtuelle Soundkarte wird vom laufenden QO100_TRX2-Stack bereitgestellt,
nicht von mm-sstv. Beide Programme im selben normalen Benutzerkonto starten.
Frequenz und PTT-/Stationssteuerung werden außerhalb von mm-sstv eingestellt.

```bash
./start.sh --list-audio
```

## Bauen und testen

Die bereits vorhandenen Entwicklungsabhängigkeiten wurden verwendet. Für
Debian/Ubuntu-basierte Linux-Systeme entsprechen sie etwa:

```bash
sudo apt install build-essential cmake pkg-config qtbase5-dev libpulse-dev python3-numpy pulseaudio-utils
./build.sh -DPython3_EXECUTABLE=/usr/bin/python3
ctest --test-dir build --output-on-failure
```

Der normale Build braucht keine Downloads und verwendet die beigelegte statische
liquid-dsp-1.8.3-Bibliothek für Linux x86-64. Es gibt keine Build-Abhängigkeit von
den bestehenden HS-Modem- oder QO100_TRX2-Verzeichnissen.

Für den isolierten Audiotest muss zusätzlich das Programm `pulseaudio` vorhanden
sein. Er startet einen eigenen Server an einem privaten Unix-Socket mit eigenen
virtuellen Ausgängen; die laufende Station und physische Soundkarten werden nicht
verwendet. Fehlen seine Hilfsprogramme, wird nur dieser Test übersprungen.
Die GUI-Prüfungen laufen mit simulierten Audioadaptern ohne Bildschirm.
Die 16 übernommenen RX-Regressionstests sind ebenfalls enthalten.

Optional lässt sich ein zusätzlich vorhandener RX-Kern als zweite Gegenstelle
für die JPEG-Rundlaufprüfung eintragen. Dieser externe Decoder muss ebenfalls
für **6000 bit/s** gebaut bzw. eingestellt sein:

```bash
./build.sh -DMM_SSTV_RX_PROBE=/Daten/funk/sat/QO100_TRX2/build/bin/qo100_mm_probe
ctest --test-dir build --output-on-failure
```

Ein Bild-Rundlauf einschließlich AFC bei +81 Hz wird bereits mit dem mitgebauten
Empfangskern geprüft. Der optionale Eintrag ergänzt eine fünfzehnte Prüfgruppe zum
Vergleich mit der bisherigen externen Installation. Ohne ihn stehen vierzehn
eigenständige Prüfgruppen zur Verfügung, einschließlich 64 Original-Framevektoren.

## Offline-Audiodateien

Diese Werkzeuge öffnen keinen Audioausgang:

```bash
# Zufallsdaten: Dauer, Pegel, Seed
./build/bin/mm-sstv-signal /tmp/random.wav 10 100 1

# Bildsignal: Eingabe, Ausgabedatei, Breite, Höhe, Pegel
./build/bin/mm-sstv-image-signal /pfad/bild.png /tmp/bild.wav 640 480 100
```

Der Bildexport erzeugt Mono-PCM16 mit 48 kHz sowie daneben `bild.wav.jpg` mit den
exakt übertragenen JPEG-Bytes zum Vergleichen.

## Struktur und Portierung

- `include/mm_sstv/modem_mode.hpp`: native Parameter beider Modi für TX, RX, Audio und Anzeige.
- `src/core/`: Betriebssystem-unabhängige Modulation und endlicher Bild-Audiostrom.
- `src/protocol/`: Serialisierung, CRC16, RS(255,223), Scrambler, Fragmentierung und Bildrekonstruktion.
- `src/receive/`: Qt-Prozess-/Audioadapter für den Empfang.
- `external/qo100-rx/`: übernommene Pilotsuche/AFC und RX-Dekodierung aus QO100_TRX2.
- `src/app/`: Sendersteuerung und threadsichere Pegel-/Fortschrittsübergabe.
- `src/audio/`: getrennte PulseAudio- und PortAudio-Adapter.
- `src/ui/`: Qt-Widgets-Oberfläche und Qt-Bildadapter für JPEG/Skalierung.

Der Protokollkern braucht weder Qt noch Betriebssystem-APIs. Er enthält einen
kleinen eigenständigen Reed-Solomon-Encoder mit den exakten HS-Modem-Parametern.
Die Senderimplementierung bindet keine Schifra-Bibliothek ein; der übernommene
Empfangskern verwendet den originalen Schifra-Decoder. Bild- und Testdaten
laufen durch denselben Modulationskern mit den Parametern des gewählten Modus.

Die PortAudio-Adapter wurden einschließlich Bildsender und Aufnahme unter Linux kompiliert.
Windows/macOS selbst sind noch nicht getestet. Dort sind passende Qt-,
liquid-dsp-1.8.3- und PortAudio-Builds erforderlich. Beispiel:

```text
cmake -S . -B build -DMM_SSTV_AUDIO_BACKEND=portaudio -DMM_SSTV_LIQUID_ROOT=C:/deps/liquid-1.8.3 -DMM_LIQUID_LIBRARY=C:/deps/liquid-1.8.3/lib/liquid.lib -DCMAKE_PREFIX_PATH=C:/deps
cmake --build build --config Release
```

PortAudio wird über ein CMake-Paket, pkg-config oder `MM_PORTAUDIO_INCLUDE` und
`MM_PORTAUDIO_LIBRARY` eingebunden. Benötigte DLLs müssen auffindbar sein.

[Empfang und Prüfergebnisse](docs/EMPFANG.md) ·
[Prüfergebnisse Bildversand](docs/BILDVERSAND.md) ·
[Quellen und Lizenzen](docs/PROVENANCE.md) ·
[Ursprüngliche Analyse](ANALYSE.md)
