# PSB-Bake: QO-100-PSK-Empfang

## Bedienung und Stationstest

1. HS-STV neu starten und unten rechts **PSB-Bake** wählen.
2. Unter **Setup → Soundkarten…** den Audioeingang mit dem USB-Empfangssignal
   auswählen. HS-STV stimmt keinen Funkempfänger ab. Bei QO100_TRX2 enthält
   `fromQO100` den normalen RX2-Ton, nicht automatisch den getrennten RX4-Kanal.
3. Den externen Empfänger auf die PSK-Bake (10489,750 MHz) einstellen. Ihre
   Signalmitte soll im Audio etwa **1500 Hz** betragen; die automatische
   Trägersuche umfasst ungefähr **1000–2000 Hz**. Das ganze Manchester-Signal
   muss im Audioband liegen; ein üblicher USB-Durchlass bis etwa 3 kHz reicht.
   Es gibt keinen gesonderten MM-Baken-Pilotton.
4. Mindestens **20–40 Sekunden** durchgehend empfangen. Das Textfeld zeigt
   ausschließlich vollständig CRC-/RS-geprüfte Rahmen. **RX · Sync** bestätigt
   gültige Daten; Spektrum oder Signalpegel allein bestätigen keine Dekodierung.
5. Zeilen, Sonderzeichen, fortlaufenden Empfang und die Umschaltung zur MM-Bake
   und zurück prüfen. Empfangene PSK-Texte bleiben beim Moduswechsel erhalten.
   Zum Test von Signalverlust den Ton ausreichend lange unterbrechen: Ein
   bereits begonnenes Empfangsfenster kann zunächst noch gültige Daten liefern.

Die Ausgabe verwendet die übertragenen 64-Zeichen-Zeilen. Der Rahmenkopf zeigt
CRC beziehungsweise FEC/RS, Zahl korrigierter Bytes und den Audioträger.
Zwei zeitlich getrennte, tatsächlich ausgesendete Wiederholungen bleiben sichtbar;
doppelte Ergebnisse desselben Rahmens aus überlappenden Fenstern werden entfernt.
Die Ansicht enthält höchstens 2000 Zeilen und folgt neuen Texten nur, wenn man
bereits unten liest und keinen Text markiert. Es gibt aktuell kein dauerhaftes
PSK-Textarchiv. Senden, Bildladen und Waterfall-ID sind im Bakenmodus deaktiviert.

## Signalweg und Portabilität

Der vorhandene C++-Kern aus QO100_TRX2 wird unter Beibehaltung seiner Lizenz
übernommen. Er unterstützt BPSK mit differentieller/Manchester-Kodierung,
400 Bit/s / 800 Chips/s, 512-Byte-CRC-Rahmen sowie AO-40-FEC mit Viterbi,
CCSDS-Descrambler und zwei verschachtelten RS(160,128)-Wörtern.
[Herkunft und Anpassungen](../external/qo100-psk/ORIGIN.md).

Der gewählte Soundkarteneingang liefert 48-kHz-Mono-Float-Audio an einen eigenen
Empfangsprozess `hs-stv-psk-rx-core`. Dort laufen das Audiospektrum und eine
gefilterte Dezimation auf 12 kHz. Ein eigener C++-Worker bearbeitet nach zunächst
20 Sekunden alle 10 Sekunden ein überlappendes Fenster von höchstens 40 Sekunden.
Es gibt einen laufenden und höchstens einen ersetzbaren neuesten Auftrag.
Damit blockieren Träger-/Taktsuche und FEC weder Audioannahme noch GUI. Die
Fenster überbrücken beliebige Stream-Anfänge und Rahmen an Fenstergrenzen.

Die FFT verwendet die bereits vorhandene liquid-dsp-Bibliothek. Es entstehen
keine neuen Bibliotheksabhängigkeiten und keine Python-, UDP- oder Linux-
Scheduler-Abhängigkeiten. C++17-Threads, Qt-Prozesssteuerung und der vorhandene
PortAudio-Adapter bilden die Grundlage eines späteren Windows-Ports.
Ein nativer Windows-Build ist damit noch nicht geprüft.

## Automatische Prüfungen

```sh
ctest --test-dir build -R 'psk_beacon_|gui_controls' --output-on-failure
```

`psk_beacon_fec` prüft unabhängige gr-satellites-Vektoren, Viterbi/Descrambler,
CRC und RS-Korrektur mit 0/1/8/16 fehlerhaften Bytes je Wort.
`psk_beacon_recording` vergleicht alle sechs CRC- und fünf FEC-Nutzlasten einer
historischen Aufnahme byteweise mit QO100_TRX2. Es prüft außerdem den neuen
48-kHz-Empfangsprozess samt Überlappungsunterdrückung, Spektrum und Synchronisation
sowie Stille und einen reinen Ton ohne Nutzdaten.
`gui_controls` prüft den vollständigen Audioempfang bis ins PSK-Textfeld,
die Moduswahl und Wiederherstellung, unveränderte andere Modi und Sendesperren.
Alle neuen Tests sind C++ und funktionieren ohne physische Soundkarte.
