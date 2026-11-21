# HS-STV – Bildübertragung mit HS-Modem und SSTV

Bilder über auswählbare Soundkarten mit HS-Modem senden und empfangen.
**8APSK · 6000 bit/s oder QPSK · 4410 bit/s.**
Datenmitte 1500 Hz · Referenzträger 100 Hz · HS-Modem-kompatibel.
Zusätzlich **SSTV Martin 1/2 und Scottie 1/2 · 320 × 256 Pixel** für Senden und Empfang.

## Start

```bash
git clone https://github.com/dj0abr/HS-STV.git
cd HS-STV
./prepare.sh
./start.sh
```

Das Programm beginnt ohne Aussendung. Ein- und Ausgang verwenden ohne gespeicherte
Geräteauswahl den **System-Standard**. Eine gespeicherte Geräte- und Pegeleinstellung
wird wiederverwendet.
Unter **Setup → Soundkarten…** werden Audioeingang und Audioausgang ausgewählt.
Der **Pegelregler direkt neben dem TX-Bild** bleibt auch während einer Sendung bedienbar.
**Bei 8APSK/QPSK entsprechen 100 % digitaler Vollaussteuerung.** Bei SSTV
entsprechen **100 % dem angepassten Maximalpegel (−12,5 dBFS Spitze)**; der gesamte
Reglerweg von 0 bis 100 % ist linear nutzbar. Daten und Pilot werden
gemeinsam geregelt, ohne AGC oder Abschneiden von Spitzen.

Das Fenster startet mit höchstens **900 × 560 Pixeln**, angepasst an den verfügbaren
Bildschirm, und lässt sich bis **640 × 440 Pixel** verkleinern. Formatwahl und
**Senden/STOP** liegen direkt über dem Sendebild; Die flachen Fortschrittsbalken stehen
unter dem jeweiligen Bild: **TX rot**, **RX grün**. Die Signalanzeige ist kompakt darunter angeordnet.
Links neben der kompakteren Signalanzeige regeln zwei senkrechte Schieber die
Anzeigegrenzen für Spektrum und Wasserfall: digital **Min −100 bis −60 dBFS**,
**Max −50 bis 0 dBFS**, Standard **−90 / −20 dBFS**.
Bei SSTV läuft die Pegelautomatik immer: Der Rauschteppich bleibt dunkel,
schwache Spuren bleiben sichtbar. Dazu wird die automatisch ermittelte Untergrenze
um 9 dB abgesenkt; die obere Grenze behält ihre Helligkeitsreserve. Die beiden
Regler justieren **Min** und **Max** unabhängig um **−6 bis +6 dB** in 1-dB-Schritten.
**0 dB** entspricht der automatischen Abstimmung. Min negativ macht den Hintergrund
heller, Max positiv gibt starken Signalen mehr Reserve. Die Regler zeigen die
Korrekturen an; die tatsächlich verwendeten Grenzen stehen an der Spektrumskala.
Korrekturen werden getrennt von den digitalen Anzeigegrenzen gespeichert.
Lange Dateinamen und Statusmeldungen werden abgekürzt und sind per Tooltip
vollständig lesbar. Die Vorschauen passen sich dem Fenster an; die gewählte
Größe des übertragenen Bildes bleibt davon unabhängig.

## Empfangen

Der Empfang startet automatisch mit dem **System-Standardeingang** oder dem
gespeicherten Gerät. Im Setup lassen sich Audioeingang und Audioausgang getrennt
auswählen. Bei Verwendung des QO100_TRX2-Audioadapters kann **fromQO100** gewählt
werden; dafür die gewünschte Frequenz im **RX2**-Kanal einstellen.

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

Nach vollständigem Bildempfang steht **Empfang abgeschlossen** unter dem Bild.
Eine kompakte RX-Zeile zeigt AFC sowie bei digitalem Empfang gültige Frames,
CRC-Fehler und FEC-korrigierte Frames. Der Tooltip enthält zusätzlich die Zahl
korrigierter RS-Bytes und nicht dekodierbarer FEC-Kandidaten und erklärt die Zähler.
Bei SSTV erscheinen erkannte und nicht erkannte Zeilensyncs des aktuellen bzw.
letzten Bildes. Bei fehlenden Syncs läuft die Bildausgabe mit dem internen
Zeilentakt weiter; die Anzeige meldet **Bildempfang · Freilauf**.
Digitale Zähler laufen seit dem Start des Empfangs und werden
beim Eingangs-/Moduswechsel zurückgesetzt.

Empfangene Bilder werden automatisch im Unterordner **rxpic** des Arbeitsverzeichnisses
abgelegt: vollständige digitale Bilder als originales JPEG, SSTV-Bilder als PNG.
Mit **Bild → Empfangsordner öffnen** öffnet der Standard-Dateimanager diesen Ordner.
Dateien werden atomar mit eindeutigen Namen geschrieben; vorhandene Bilder bleiben erhalten. Nicht vollständige Blöcke bleiben für Wiederholungen
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

## QO100 MM-Bake – erste Teststufe

Die neue Auswahl **QO100 MM-Bake** ist ein reiner Empfangsmodus mit **8APSK ·
7200 bit/s**. Die erste Stufe zeigt Pilot/AFC, Spektrum, Konstellation und gültige
Frames; die Textdekodierung folgt nach dem Stationstest.
[Testanleitung und weitere Stufen](docs/MM-BAKE.md).

## SSTV Martin 1/2 und Scottie 1/2

Alle vier SSTV-Modi sind im selben Modusfeld auswählbar und verwenden fest
**320 × 256 Pixel**. Bilder werden proportional eingepasst. Der Empfang erscheint
zeilenweise und lässt sich auch bei Teilempfang als PNG speichern.
Die Sendedauer einschließlich VIS beträgt rund 115,2 s für Martin 1, 59,0 s für
Martin 2, 110,5 s für Scottie 1 und 72,0 s für Scottie 2.

Der SSTV-Monitor zeigt Spektrum und einen Wasserfall mit sechs Sekunden Verlauf
mit **23,44 Hz Frequenzauflösung** und kurzen, überlappenden Analysefenstern für
die Sync-Impulse. Feste Hilfslinien für **Sync bei 1200 Hz**
und **Bildsignal von 1500 bis 2300 Hz** helfen beim Feinabstimmen der Empfangsfrequenz.
Daneben steht der ermittelte AFC-Versatz. SSTV sendet keinen 100-Hz-Referenzträger.
[Bedienung, Technik und Tests](docs/SSTV.md).

## Optionale Waterfall-ID

**Waterfall-ID** neben der Betriebsart sendet vor jedem Bild eine im Wasserfall
lesbare Kennung, z. B. **HS-STV 8APSK**, **HS-STV QPSK** oder **HS-STV MARTIN1**.
Der Text folgt automatisch der Betriebsart. Die Auswahl wird gespeichert;
standardmäßig ist sie aus. Ein 500-ms-Punkt mit 100 ms Abstand gibt einer audiogesteuerten PTT Zeit zum
Anziehen, bevor der erste Buchstabe beginnt. Der Vorspann dauert einschließlich
Pausen rund 10–13 s.
Der Pegelregler wirkt auf Kennung und Bild. Bei SSTV wird die Kennung mit
**10 dB mehr Pegel** gegenüber dem eingestellten SSTV-Bildpegel gesendet;
bei 8APSK/QPSK erhält sie keine Anhebung. STOP bricht Kennung und Bild ab.
[Details](docs/WATERFALL-ID.md).

## Rufzeichen und Textüberlagerung

**Text…** über dem TX-Bild oder **Bild → Textüberlagerung…** öffnet den Editor.
Dort lassen sich eigenes Rufzeichen, Gegenstation und fünf Freitexte eingeben.
Die Auswahl **1–5** neben **Text…** bestimmt den eingeblendeten Freitext; beide
Rufzeichen werden unabhängig davon angezeigt. Leere Felder erscheinen nicht.

Texte im Editor mit der Maus verschieben oder nach Anklicken mit den Pfeiltasten
um einen Bildpixel bewegen; **Umschalt** bewegt um fünf Pixel. Pro Textebene sind
serifenlose Schrift, Serifenschrift oder Monospace, Größe und Fettdruck einstellbar.
Die fünf Freitexte teilen sich eine Position und Formatierung. Weiße Schrift mit
schwarzer Kontur bleibt auf hellen und dunklen Bildern sichtbar. Lange Texte
werden auf die Bildbreite eingepasst; die Vorschau zeigt das Ergebnis.

**Übernehmen** speichert Texte, Formatierung, Positionen und Auswahl für den
nächsten Start. **Abbrechen** verwirft die Bearbeitung. Die Überlagerung wird bei
jeder Bildaufbereitung neu auf eine Kopie des skalierten Bildes gezeichnet;
Positionen und Schriftgröße skalieren mit dem gewählten Bildformat. Das geladene
Original bleibt erhalten. Die Funktion gilt für 8APSK, QPSK und alle SSTV-Modi.
Während einer Sendung sind Texteditor und Vorlagenwahl gesperrt; der Pegelregler
bleibt bedienbar.

## Bild senden (8APSK/QPSK)

1. Ein Bild in die Vorschau ziehen, die Vorschau anklicken oder **Bild → Bild öffnen…** wählen.
2. **Bildgröße** auswählen: 320×240, 640×480, 800×600 oder 1024×768.
3. Vorschau, JPEG-Dateigröße und ungefähre Sendedauer prüfen, dann **Senden** drücken.

Das Bild wird proportional in das gewählte Format eingepasst; freie Ränder werden
weiß aufgefüllt. Die Vorschau zeigt das tatsächlich kodierte JPEG einschließlich
gewählter Textüberlagerungen. Die JPEG-Qualität wird
bei Bedarf automatisch reduziert, damit die Datei höchstens 200000 Byte groß ist.

Während des Sendens zeigt der Button **STOP** und der Fortschrittsbalken läuft.
Der Anfang enthält ungefähr 7,6 Sekunden bei 8APSK bzw. 10,3 Sekunden bei QPSK zur Akquisition, der letzte Frame wird
mehrfach wiederholt. Das erklärt einen Teil der angezeigten Sendedauer.
Am normalen Ende werden die letzten Audiosamples vollständig abgespielt und der
Audiostream automatisch geschlossen. **STOP** verwirft den Puffer sofort; ein
Fensterschließen beendet ebenfalls die Ausgabe.

„Sendung beendet“ bestätigt die lokale Audioausgabe, keine Empfangsbestätigung.
Den korrekten Bildempfang bitte mit der vorhandenen HS-Modem-Installation prüfen.
Der Bildempfang in hs-stv läuft unabhängig davon weiter.

Die Auswahl von Sendeausgang und Sende-Bildformat wird während einer laufenden Aussendung
gesperrt; der Sendepegel kann direkt neben dem TX-Bild weiterhin verändert werden. Bei fehlendem
gewähltem Gerät findet kein automatischer Wechsel auf eine andere Soundkarte statt.
Nach erneutem Anschluss im Setup **Aktualisieren** drücken.

## Anschluss

```text
hs-stv → toQO100 → QO100_TRX2-Audioadapter → TX2 → Pluto
hs-stv ← fromQO100 ← QO100_TRX2-Audioadapter ← RX2 ← Pluto
```

Die virtuelle Soundkarte wird vom laufenden QO100_TRX2-Stack bereitgestellt,
nicht von hs-stv. Beide Programme im selben normalen Benutzerkonto starten.
Frequenz und PTT-/Stationssteuerung werden außerhalb von hs-stv eingestellt.

```bash
./start.sh --list-audio
```

## Bauen und testen

Unter Debian, Ubuntu und Linux Mint installiert `prepare.sh` die Pakete für
Bau und Betrieb sowie Python/NumPy für die Tests. Das Skript verwendet `apt-get`
und fordert bei Bedarf über `sudo` Root-Rechte an:

```bash
./prepare.sh
./build.sh -DPython3_EXECUTABLE=/usr/bin/python3
ctest --test-dir build --output-on-failure
```

Mit `./prepare.sh --dry-run` lässt sich die Installation ohne Änderungen anhand
der vorhandenen Paketlisten simulieren. `./prepare.sh --portaudio` installiert
zusätzlich `portaudio19-dev` für einen Build mit
`./build.sh -DMM_SSTV_AUDIO_BACKEND=portaudio -DPython3_EXECUTABLE=/usr/bin/python3`.

Enthalten sind insbesondere **glibc (`libc6`, `libc6-dev`)**, die C++-Laufzeit
(`libstdc++6`), CMake, Compiler, Qt-5-Entwicklungsdateien samt Bildformat- und
Wayland-Plugins, Schriften und die PulseAudio-Clientbibliothek.
Ein vorhandener PulseAudio- oder PipeWire-Pulse-Server wird weiterverwendet;
`prepare.sh` installiert keinen neuen Audioserver.

Bei Meldungen wie `GLIBC_… not found` oder `GLIBCXX_… not found` kann ein auf
einem neueren System gebautes Programm vorliegen. Nach `prepare.sh` mit
`./build.sh -DPython3_EXECUTABLE=/usr/bin/python3` auf dem Zielsystem neu bauen;
`start.sh` allein baut ein bereits vorhandenes Programm nicht erneut.
Das Skript verwendet ausschließlich die konfigurierten Paketquellen der
Distribution und installiert keine fremde glibc-Version.

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
Empfangskern geprüft. Der optionale Eintrag ergänzt eine Prüfgruppe zum
Vergleich mit der bisherigen externen Installation. Ohne ihn stehen achtzehn
eigenständige Prüfgruppen zur Verfügung, einschließlich SSTV und 64 Original-Framevektoren.

## Offline-Audiodateien

Diese Werkzeuge öffnen keinen Audioausgang:

```bash
# Zufallsdaten: Dauer, Pegel, Seed
./build/bin/hs-stv-signal /tmp/random.wav 10 100 1

# Bildsignal: Eingabe, Ausgabedatei, Breite, Höhe, Pegel
./build/bin/hs-stv-image-signal /pfad/bild.png /tmp/bild.wav 640 480 100
```

Der Bildexport erzeugt Mono-PCM16 mit 48 kHz sowie daneben `bild.wav.jpg` mit den
exakt übertragenen JPEG-Bytes zum Vergleichen.

## Struktur und Portierung

- `include/mm_sstv/modem_mode.hpp`: Parameter der digitalen und analogen Modi für TX, RX, Audio und Anzeige.
- `src/core/`: Betriebssystem-unabhängige Modulation und endlicher Bild-Audiostrom.
- `src/protocol/`: Serialisierung, CRC16, RS(255,223), Scrambler, Fragmentierung und Bildrekonstruktion.
- `src/receive/`: Qt-Prozess-/Audioadapter für den Empfang.
- `external/qo100-sstv/`: portabler Martin-1/2-/Scottie-1/2-Encoder und -Decoder aus QO100_TRX2.
- `external/qo100-rx/`: übernommene Pilotsuche/AFC und RX-Dekodierung aus QO100_TRX2.
- `src/app/`: Sendersteuerung und threadsichere Pegel-/Fortschrittsübergabe.
- `src/audio/`: getrennte PulseAudio- und PortAudio-Adapter.
- `src/ui/`: Qt-Widgets-Oberfläche, portabler Texteditor/Renderer und Qt-Bildadapter für JPEG/Skalierung.

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
