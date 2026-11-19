# Validierung – physikalischer Testsender (historischer Stand)

Aktueller Bildversand und gültige Frames: siehe [BILDVERSAND.md](BILDVERSAND.md).

Stand vor der Bilderweiterung: 19.11.2026. Entwicklungsrechner: Linux x86-64, GCC 13.3, Qt 5.15.13,
libpulse 16.1, liquid-dsp 1.8.3. Es wurden keine Pakete global installiert.

## Implementierter Umfang

Kontinuierliche Zufallssymbole mit 8APSK bei 7200 bit/s, RRC-Roll-off 0,2,
48 kHz und 20 Samples je Symbol. Die Datenmitte liegt bei 1500 Hz, der
mitgesendete Pilot bei 100 Hz. Keine Bild-/Dateiframes und kein Empfänger.
Der Hauptbildschirm enthält START/STOP; Geräteauswahl und Pegel liegen im Setup.
Default ist `toQO100`, Pegel 35 %. Einstellungen werden mit QSettings gespeichert.

## Durchgeführte Prüfungen

| Prüfung | Ergebnis |
|---|---|
| Vergleich mit unabhängiger Transkription des Original-HS-Modem-TX-Pfads | Nach Ausgleich der festen neuen Pegelskalierung: maximale Sampleabweichung 5,96 × 10⁻⁸ bei identischer Symbolfolge |
| Unterschiedliche Audioblockgrößen, auch mitten im Symbol | Identische Samples, kein Phasen-/Filterreset an Blockgrenzen |
| Symboltakt | 48000 ausgegebene Samples benötigen exakt 2400 Symbole; 3 Bit pro Symbol ergeben 7200 bit/s |
| Gleichverteilung der acht Testzustände | Erfolgreich, 80000 Symbole ausgewertet |
| Isolierter Pilot bei Mittelpunkt-Symbolen | RMS 0,056120 bei 100 %, passend zu ungefähr 0,07934 Spitzenamplitude |
| Exportiertes PCM16-Audio | 48 kHz, Mono, korrekte Dateilänge und Pegel, keine Begrenzung einzelner Samples |
| Spektrum des exportierten Signals | Pilot 100,0 Hz, gemessene Spitzenamplitude 0,07913 im Datenstrom; gemessener Daten-Schwerpunkt 1494,0 Hz |
| Leistung außerhalb des erwarteten Datenbandes, Pilotumfeld ausgenommen | Anteil ungefähr 0,000003 in der verwendeten FFT-Messung |
| Vollaussteuerung | Aus den Filterkoeffizienten und Konstellationspunkten berechnete feste Skalierung; gezielt maximierte Symbolfolge erreicht 0,995592, ohne Clipping |
| Pegel 50 % | Daten und Pilot gemeinsam mit halber Amplitude gegenüber 100 % |
| Pegel 0 % | Auch der Pilot verschwindet vollständig |
| GUI mit simuliertem Audioadapter | START/STOP, Fenster schließen, Defaultgerät, fehlendes Gerät, Auswahl/Aktualisieren/Speichern und Fehlererholung erfolgreich |
| Echter Linux-Audioadapter auf privatem PulseAudio-Server | Signal am ausdrücklich gewählten Monitor empfangen, 100-Hz-Pilot nachgewiesen, keine Audioaussetzer |
| Dreimaliges Starten und Stoppen | Erfolgreich; Stream jeweils geschlossen |
| STOP | Im isolierten Test etwa 6 ms für Abbruch/Flush/Schließen; Aufnahme bestätigt nachfolgende Stille |
| Ausgewähltes Gerät entfernt | Ausgabe endet, kein Wechsel auf den anderen verfügbaren Ausgang |
| Audioserver beendet | Fehler erkannt und Ausgabe beendet |
| Alternative PortAudio-Konfiguration | Unter Linux inklusive GUI erfolgreich kompiliert und gelinkt |
| Lieferumfang | liquid-dsp statisch gelinkt; keine Abhängigkeit von den ursprünglichen absoluten Quellprojektpfaden |

Reproduzierbar mit `ctest --test-dir build --output-on-failure`. Die vier CTest-
Gruppen heißen `phy_reference`, `signal_spectrum`, `gui_controls` und
`private_audio`. Das jeweilige vollständige Protokoll liegt nach dem Testlauf
unter `build/Testing/Temporary/LastTest.log`.

Der Audiointegrationstest setzt ausdrücklich einen privaten `PULSE_SERVER` und
zwei eigene Null-Sinks. Er verbindet sich nicht mit `toQO100` der laufenden
Station. Das Einschwingen des PulseAudio-Null-Sink-Takts wird vor der Messung
abgewartet. Die GUI wurde zusätzlich als Offscreen-Bild visuell geprüft.

## Abgleich mit der Multimedia-Bake

Der auf Benutzerwunsch gewählte Modus entspricht dem 7200-bit/s-Eintrag der
[Multimedia-Bakenquellen](https://github.com/amsat-dl/multimedia_beacon/blob/ce444ba3620174a02d79806555343369bb623692/hsmodem/hsmodem.cpp):
48 kHz, Interpolation 20 und 3 Bit je 8APSK-Symbol. RRC: 601 Koeffizienten,
15 Symbole Filterverzögerung, Roll-off 0,2, effektiver Original-Offset `dt=0`.
Die Datenmitte bleibt bei 1500 Hz, der Pilot bei 100 Hz. Die rechnerische
RRC-Datenbandbreite beträgt 2880 Hz, entsprechend ungefähr 60 bis 2940 Hz.
Der Spektrumtest prüft ausdrücklich auch die Bandanteile außerhalb des früheren
6000-bit/s-Datenbandes.

Zusätzliche Offline-Gegenprüfung mit `QO100_TRX2/build/bin/qo100_mm_probe --manual`:
12 Sekunden aus dem neuen WAV-Generator wurden als Mono-Float32 eingespeist.
Nach fünf Sekunden Einschwingzeit wurden 4200 Konstellationspunkte ausgewertet:
alle acht Gruppen belegt (523 im Mittelpunkt, 501 bis 548 in den äußeren Gruppen),
normierter RMS-Abstand zum angepassten 1+7-Konstellationsraster ungefähr 4,15 %,
verbleibender PLL-Frequenzfehler am Ende ungefähr 0,006 Hz. Es gibt erwartungsgemäß
keine gültigen Frames, da der Testsender weiterhin reine Zufallssymbole ausgibt.
Dieser Referenzempfänger ist keine Build-Abhängigkeit von mm-sstv.

Die genaue Quellenidentität steht in `BEACON-REFERENCE.json`. Die Dokumente von
Schritt 1 beschreiben weiterhin den damaligen Analyse- und Planungsstand.

## Pegelskala nach der Anpassung

100 % bezieht sich jetzt auf den maximal möglichen digitalen Spitzenpegel des
kompletten Signals einschließlich Filterüberschwingern und Pilot. Dazu wird für
jede RRC-Polyphase der größte Betrag der gewichteten Konstellationspunkte bestimmt.
Die Projektion Realteil + Imaginärteil und die Pilot-Spitzenamplitude werden
mitberücksichtigt. Ein fester Faktor von ungefähr 0,317353 ersetzt den bisherigen
Faktor 0,1; das entspricht +10,03 dB. Ein PCM16-LSB und eine kleine numerische
Rundungsreserve liegen unterhalb der Aussteuerungsgrenze.

Die Tests prüfen zusätzlich eine gezielt maximierte Symbolhistorie und lineare
Pegelhalbierung. Die konstante Skalierung erhält die Konstellation und das
Verhältnis von Daten zu Pilot; sie arbeitet ohne AGC, Limiter oder Clipping.

## Grenzen der Prüfung

`toQO100` ist auf dem tatsächlichen PipeWire-Pulse-Server vorhanden und wird vom
neuen Programm aufgelistet. Die automatischen Prüfungen verwenden ausschließlich
den privaten Audioserver und Dateien. Die vom Benutzer vorgenommenen praktischen
Pegeltests ersetzen keinen automatischen Nachweis der HF-Strecke; TX2-Filter und
Satellitenweg wurden in diesem Prüflauf nicht vermessen. Die Abbruchzeit des echten Geräts kann wegen Betriebssystem-, Geräte-
und Stationspuffern von der isolierten Messung abweichen.

Der Signalvergleich prüft den digitalen Sendepfad, nicht das gesamte System aus
Soundkarte, SDR und Satelliten. Die vereinbarte Abnahme bleibt der Empfang über
QO100 in der vorhandenen HS-Modem-Installation bei **8APSK 7200 bit/s** mit
sauberer Konstellation und 100-Hz-Referenz.

Der Windows-/macOS-PortAudio-Adapter und die CMake-Auswahl sind vorbereitet;
auf diesen Betriebssystemen wurde noch kein Build oder Laufzeittest durchgeführt.
Die PortAudio-Geräteauswahl verwendet Host-API und Gerätename und verweigert bei
doppeldeutigen Namen den Start. Der Linux-Adapter adressiert Sinks direkt nach ID.
