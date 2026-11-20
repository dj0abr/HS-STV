# SSTV Martin 1/2 und Scottie 1/2

Im Modusfeld unten rechts stehen **SSTV Martin 1**, **Martin 2**, **Scottie 1** und
**Scottie 2** für Senden und Empfang bereit. Alle vier Modi verwenden für die
Bildaufbereitung und Darstellung fest **320 × 256 Pixel**.
Die Bildgrößenauswahl zeigt dieses Format und ist gesperrt. Beim Wechsel zurück zu
8APSK/QPSK wird die vorherige digitale Bildgröße wiederhergestellt.

Geladene Bilder werden proportional eingepasst und mit weißen Rändern ergänzt.
Für SSTV werden die RGB-Pixel direkt übertragen, ohne JPEG-Kompression. Rufzeichen
und der ausgewählte Freitext aus **Text…** sind Bestandteil dieser Bildpixel.
Die schnelleren Modi verwenden kürzere Farbscans; damit nimmt die horizontale
Detailauflösung ab, auch wenn die Vorschau weiterhin 320 × 256 Pixel zeigt.

| Modus | VIS ohne Paritätsbit | Sendedauer einschließlich VIS |
| --- | --- | --- |
| Martin 1 | 44 | 115,2 s |
| Martin 2 | 40 | 59,0 s |
| Scottie 1 | 60 | 110,5 s |
| Scottie 2 | 56 | 72,0 s |

Am Ende folgen 5 ms Ausblendung und 300 ms Stille. Senden/STOP, Soundkartenauswahl und
Pegeleinstellung funktionieren wie bisher. Der SSTV-Regler arbeitet über den
vollen Bereich **0–100 % linear**. Bei 100 % beträgt der Spitzenpegel rund
23,66 % der digitalen Vollaussteuerung (**−12,5 dBFS Spitze**). Dieser Maximalpegel
ist an die vorhandene Station angepasst. Bei 8APSK/QPSK gelten weiterhin 100 %
als digitale Vollaussteuerung. Der Pegelregler neben dem TX-Bild ist auch während
einer Sendung bedienbar. Pegeländerungen und Sendestart werden geglättet.
SSTV verwendet seine normalen VIS-/Zeilensignale und keinen 100-Hz-Hilfsträger.

Der immer aktive Empfänger erwartet den ausgewählten SSTV-Modus. Die Vorschau
wächst zeilenweise; der grüne Balken zählt tatsächlich empfangene Zeilen.
Auch Teilempfang ohne VIS ist möglich. Ein Teilempfang oder unterbrochenes Bild
wird nicht als vollständig gemeldet. Nach vollständigem Empfang steht unter dem Bild
**Empfang abgeschlossen**. Die RX-Zeile zeigt den AFC-Versatz und die Zahl erkannter
bzw. nicht erkannter Zeilensyncs, einschließlich übersprungener Zeilen. Die Werte
gelten für das aktuelle/letzte Bild; bei einem neuen Bild beginnen sie erneut.
 Bei Empfangsende werden die Bilder automatisch
als PNG in **rxpic** gespeichert. Auch Teilempfang wird beim Empfangsende, Wechseln
des Eingangs/Modus oder Schließen gesichert; noch fehlende Zeilen bleiben schwarz.
**Bild → Empfangsordner öffnen** zeigt den Ordner im Standard-Dateimanager. Anders
als beim digitalen JPEG-Verfahren gibt es bei analogem SSTV keine CRC-gesicherte
Bilddatei und keine Ergänzung fehlender Zeilen aus einer Wiederholung.

Die Diagnoseanzeige nutzt für SSTV die ganze Breite als **Spektrum und Wasserfall
von 0–4000 Hz**. Der Wasserfall zeigt sechs Sekunden Verlauf, die neuesten Werte
oben. Er analysiert das unveränderte Audio in 43-ms-Fenstern (2048 Samples bei
48 kHz) alle 10 ms. Die Frequenzauflösung beträgt **23,44 Hz** und ist damit
doppelt so fein wie zuvor.
Dadurch werden auch die kurzen Zeilensync-Impulse sichtbar. Beim Zusammenfassen
mehrerer Zeitpunkte für eine Bildschirmzeile bleiben die stärksten Werte erhalten.
Zwei senkrechte Regler links neben der Anzeige bestimmen die gemeinsame
dBFS-Skala für Spektrum und Wasserfall. **Min** reicht von **−100 bis −60 dBFS**,
**Max** von **−50 bis 0 dBFS**. Standard ist **−90 / −20 dBFS**; Änderungen werden
gespeichert und gelten für alle Modi. Niedrige Werte unterhalb 100 Hz verändern
die gewählten Grenzen nicht.
Eine grüne Hilfslinie markiert **Sync 1200 Hz**, gelbe Linien begrenzen das
schattierte **Bildsignal 1500–2300 Hz**. Die Hilfslinien in Spektrum und Wasserfall zeigen feste Sollfrequenzen
im unveränderten Empfangsaudio vor der AFC. So kann der Operator am Funkgerät
fein abstimmen. Der angezeigte AFC-Wert entspricht dem geschätzten Frequenzversatz.
Ein Konstellationsdiagramm wird bei SSTV nicht benötigt.

Die Wasserfallfarben sind auf die FFT-Binbreite des Spektrums bezogen: Die
breiteren Bins der kurzen Analysefenster lassen den Rauschboden dadurch nicht
künstlich heller erscheinen. Werte an oder unter **Min** werden schwarz.
Schmale Träger und kurze Impulse können wegen der unterschiedlichen Zeit- und
Frequenzauflösung weiterhin andere Spitzenwerte als im Spektrum zeigen.

## Übernommener Kern und Portierung

Encoder, Decoder, Timing und ursprüngliche Regressionstests stammen aus dem
lokalen QO100_TRX2-Projekt. Originalpfade, Basisrevision und SHA-256 stehen in
`external/qo100-sstv/SOURCE.json`. Der Decoder enthält VIS-Erkennung, AFC,
Zeilensynchronisation, Nachführung und Wiedereinstieg. Für Martin 2 / Scottie 2
wurden die Modustabelle und die modusspezifischen Akquisitionszustände erweitert;
Filterung und Nachführung bleiben erhalten.

Die einzige Encoder-Erweiterung ist eine optionale native 48-kHz-Ausgabe mit
Phasenzugriff für den Ausklang. Die ursprüngliche 12-kHz-Ausgabe bleibt der
Standard und wird mit den unveränderten Originaltests geprüft.
`SstvAudio` umgibt den Encoder mit endlicher Dauer, Pegelregelung und Fortschritt.
Beide Komponenten sind C++17 ohne Qt oder Betriebssystemzugriffe.

Die vorhandenen PulseAudio-/PortAudio-Adapter arbeiten für SSTV mit 48-kHz-Mono.
`hs-stv-sstv-rx-core` filtert und dezimiert dieses Audio auf 12 kHz für den
Originaldecoder. Die GUI empfängt RGB-Zeilen und Spektren über die vorhandene
Qt-Prozessanbindung. Es werden weder die Transceiversteuerung noch Netzwerk-,
FFTW- oder Audioabhängigkeiten aus QO100_TRX2 übernommen.

## Prüfung und Offline-Export

```bash
./build.sh -DPython3_EXECUTABLE=/usr/bin/python3
ctest --test-dir build --output-on-failure

# Keine Soundkartenausgabe; WAV und Referenzbild als bild.wav.png:
./build/bin/hs-stv-sstv-signal /pfad/bild.png /tmp/bild.wav martin1 100
./build/bin/hs-stv-sstv-signal /pfad/bild.png /tmp/bild.wav scottie1 100
```

Die unveränderten Originaltests prüfen Martin 1 und Scottie 1 mit unabhängigen Signalen, schwachem
Pegel, Rauschen, Frequenzversatz, Timingfehlern, fehlendem VIS und falschen oder
fehlenden Syncpulsen. Zusätzliche Prüfungen führen native 48-kHz-Sendedaten durch
den echten RX-Prozess und prüfen alle 256 Zeilen, Farbinhalte, Spektrum,
kalibrierten Sendepegel und Teilbildstatus. GUI-Tests prüfen alle vier SSTV-Modi einschließlich
Senden/STOP, laufendem Empfang, PNG-Speicherung und Wiederherstellung der digitalen
Bildgröße. Eine Prüfung über den Satelliten erfolgt anschließend an der Station.

Für Martin 2 und Scottie 2 gibt es zusätzlich unabhängige 12-kHz-Referenzsignale
mit VIS/Teileinstieg, Frequenzversatz und fehlenden Syncs. Die 48-kHz-Audioexport-
Tests prüfen für alle vier Modi Dauer, RGB-Zeilen, Diagnose und Teilbildstatus.
Der Parameterabgleich erfolgte mit den Primärquellen
[QSSTV](https://raw.githubusercontent.com/ON4QZ/QSSTV/master/src/sstv/sstvparam.cpp)
und [PySSTV](https://raw.githubusercontent.com/dnet/pySSTV/master/pysstv/color.py).
