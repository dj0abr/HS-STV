# Empfang – Version 0.3

Stand: 19.11.2026. Der Benutzer hat den fehlerfreien Bildversand über QO100 an
HS-Modem bestätigt. Auch der Bildempfang wurde inzwischen über QO100 erfolgreich bestätigt.

## Herkunft und Aufbau

Der DSP-Empfangskern wurde aus
`QO100_TRX2/external/mmdecoder` übernommen, Projektstand
`7af11b830ba83b7d497a89ca3f1492008b212cd9`.
`external/qo100-rx/SOURCE.json` hält die Originalprüfsummen fest.
Enthalten sind Pilotsuche/AFC (`pilot_afc.*` und Empfangsschleife aus `rx_probe.cpp`),
Downmixer, Vorfilter, Symboltracking, Header-/Rotationssuche, Descrambler,
Reed-Solomon und Frame-CRC. Originalhinweise und die Schifra-Header bleiben erhalten.

Die Such- und Nachführregeln bleiben unverändert: 20–900 Hz Pilotsuche mit
überlappenden FFTs, stabile Kandidaten, Verwerfen unbestätigter Töne,
Bestätigung durch gültige Frames, kontinuierliche Übernahme langsamer PLL-
Korrekturen sowie Wiederaufnahme nach Aussetzern. Auch der 500-Hz-Pilot wird
unterstützt. Die Datenmitte liegt stets 1400 Hz über dem Piloten.

Der Kern läuft als mitgeliefertes Programm `mm-sstv-rx-core`. Das isoliert die
ursprünglichen statischen RX-Zustände vom Sender. Der Qt-Adapter `Receiver`
übergibt Mono-Float-Audio bei 48 kHz und verarbeitet die JSON-Frame-/Statusausgabe.
Die Verbindung benutzt ausschließlich lokale Prozess-Pipes, keine UDP-Ports und
keinen Netzwerkserver. Der Build braucht kein bestehendes Nachbarprojekt.

Die Anpassungen an der übernommenen Empfangsschleife betreffen den Windows-
Binärmodus von stdin, das zusätzliche Anzeige-Feld `pilot_hz` und die optionale
Monitor-FFT (siehe unten). Die Pilotenanzeige
folgt bei bestätigter Synchronisation dem Empfangskanal statt einem stärkeren
fremden Ton. Die eigentliche Akquisition und AFC wurden nicht ersetzt.

Der Audioeingang liegt hinter einer eigenen Schnittstelle `AudioInput`:
PulseAudio/PipeWire unter Linux, separater PortAudio-Adapter für spätere
Windows-/macOS-Builds. Die bestehenden Sendeparameter, der Pilot und die
Vollaussteuerungsnormierung bleiben unverändert.

## Live-Spektrum und Konstellation

Unter den Bildern liegen ein natives Qt-Spektrum und das 8APSK-Konstellationsdiagramm.
Die Prozessbrücke aktiviert dafür `mm-sstv-rx-core --monitor`. Ohne diese Option
bleibt die bisherige Statusausgabe unverändert. Bereits vorhandene `points` werden
jetzt als reale, entzerrte I/Q-Symbole dargestellt; bis zu 600 Punkte bilden eine
kurze Spur. Bestätigte Datensynchronisation färbt die Punkte grün, ansonsten cyan.
Ein Träger allein wird weiterhin nicht als Datensynchronisation angezeigt.

Die zusätzliche portable Klasse `AudioSpectrum` berechnet unabhängig vom Decoder
und dessen Pilotsuche ein Hann-Fenster mit 32768 Samples bei 48 kHz. Sie benutzt
liquid-dsp, keine zusätzliche Bibliothek und keine Betriebssystem-API.
Das Eingangsaudio wird vor Downmixer, AFC und AGC analysiert. Die Anzeige wird mit
den vorhandenen 100-ms-Statusmeldungen aktualisiert, erstmals nach etwa 0,7 Sekunden.
Die Auflösung beträgt 1,465 Hz; dargestellt werden 0–4000 Hz und 0 bis −140 dBFS/Bin.
Ein Sinus mit Spitzenamplitude 1 genau auf einem FFT-Bin entspricht 0 dBFS/Bin;
Fensterverluste bei Zwischenfrequenzen sind möglich. Der Pegel oben rechts ist
der RMS-Pegel des Eingangs. Die FFT erhält keine automatische Pegelnormierung.

Der Bereich 20–900 Hz markiert die Pilotsuche. Die grüne Linie folgt dem vom
Decoder gewählten Piloten, die gelben Linien markieren die aktuelle Datenmitte
±1440 Hz (2400 Symbole/s, RRC-Rolloff 0,2). Beim Verkleinern bleiben schmale
Träger durch das Maximum der FFT-Bins pro Pixel sichtbar. Die feste I/Q-Skalierung
verhindert eine scheinbar saubere Konstellation durch automatische Größenanpassung.

Pause, Eingangswechsel und Empfangsfehler leeren die Anzeige. Digitale Stille
ersetzt nach Ablauf des FFT-Fensters das alte Spektrum; die Punktspur wird beim
ausbleibenden Symbolstrom geleert. Bei fehlenden aktuellen Statusdaten wird nach
zwei Sekunden auch die Anzeige als veraltet verworfen. Bildblöcke bleiben erhalten.

## Bildrekonstruktion und Teilanzeige

`ImageAssembler` ist eine C++-Anpassung von `receivefile.cs` und der Dateikopf-
Auswertung aus `ArraySend.cs` des Original-HS-Modems. Er verarbeitet nur Typ 2,
ordnet die 219-Byte-Blöcke über die Zähler zu, entfernt den 55-Byte-Dateikopf und
beachtet die tatsächliche JPEG-Länge. Die maximale Empfangslänge ist durch
1024 Blöcke begrenzt (224201 JPEG-Bytes). Der Sender bleibt bei 200000 Byte.

Doppelte erste Frames löschen vorhandene Blöcke nicht. Fehlende Teile können durch
eine weitere Aussendung ergänzt werden. Bis zu acht Bildidentitäten werden anhand
von Dateiname, ID und Größe im Arbeitsspeicher gehalten. Dieser Zwischenspeicher
bleibt beim Anhalten oder Wechseln des Audioeingangs erhalten, jedoch nicht nach
einem Programmneustart. Nach einem Stream-Neustart ist ein gültiger Dateikopf nötig;
kopflose Daten verändern kein zuvor vollständig empfangenes Bild.

Die Anzeige dekodiert laufend den zusammenhängenden JPEG-Anfang bis zum ersten
fehlenden Block. Für diese Vorschau wird nur vorübergehend ein JPEG-Endmarker
angehängt; empfangene Datei-Bytes werden nicht geändert. Sobald ausreichend JPEG-
Header und Bilddaten vorliegen, wächst das sichtbare Bild. Die ersten kurzen
Blöcke können noch kein darstellbares Bild enthalten. Fehlende Blöcke bleiben im
Fortschritt erkennbar; bereits vorhandene spätere Blöcke gehen nicht verloren.

Die Frame-CRC ist Voraussetzung für die Übernahme eines Blocks. Eine Datei-ID darf
laut Protokoll auch eine beliebige Kennung sein; ältere Sender werden deshalb
nicht wegen einer abweichenden Inhalts-CRC abgewiesen. Vollständigkeit bedeutet,
dass sämtliche Blöcke und die angegebene JPEG-Länge vorliegen. Erst ein erfolgreich
dekodiertes vollständiges Bild kann über „Bild → Empfangsbild speichern…“ gespeichert
werden. Die originale JPEG-Datei wird mit QSaveFile atomar abgelegt.

## Bedienung

- Links: zu sendendes Bild. Rechts: Empfangsbild, Pilot-/Sync-Anzeige, gültige
  Frames und Bildblock-Fortschritt.
- Audioeingang standardmäßig `fromQO100`, Audioausgang weiterhin `toQO100`.
  Beide sind im Setup auswählbar; es gibt keinen stillen Wechsel auf ein anderes Gerät.
- Empfang startet beim Programmstart automatisch, wenn aktiviert und der Eingang
  verfügbar ist. Das Kästchen „Empfang“ hält ausschließlich den Empfang an bzw.
  startet ihn neu. Gespeicherte Geräteeinstellungen werden wiederverwendet.
- RX und TX laufen unabhängig. STOP beendet ausschließlich die Sendung. Die Wahl
  eines neuen Eingangs unterbricht nicht die Sendung.
- Beim virtuellen Audioweg `fromQO100` liefert QO100_TRX2 den RX2-Kanal. Dessen
  Frequenz muss im Transceiverprogramm auf die gewünschte Aussendung eingestellt sein.
- Vollständige Bilder werden auf ausdrücklichen Dateidialog-Aufruf gespeichert,
  nicht automatisch in einen Ordner geschrieben.

## Prüfung

Die elf eigenständigen CTest-Gruppen enthalten zusätzlich zu den bisherigen
Sendetests:

- Alle 16 übernommenen QO100_RX-Regressionstests: Frequenzversatz bis +750 Hz,
  negative Versätze, 500-Hz-Pilot, Drift, Frequenzsprünge, Signalverlust, erneute
  Akquisition, stärkerer fremder Ton sowie keine falsche Daten-Synchronisation
  durch Rauschen, Stille, reine Träger oder CRC-fehlerhafte Frames.
- Eigener vollständiger RX-Kern gegen den Bild-WAV-Generator: gültige Frames und
  byteidentisches JPEG ohne Versatz und mit automatisch korrigierten +81 Hz.
- Qt-Prozessbrücke mit simuliertem Eingang: Synchronisation, 300 erfolgreich
  dekodierte Teilansichten vor Abschluss, endgültiges JPEG byteidentisch.
- Monitor: kalibrierte FFT bei Vollaussteuerung und −20 dB, DC, wechselnde
  Blockgrößen, Ringpuffer und Stille/nichtendliche Samples; echte Spektren und
  Konstellationspunkte durch die Qt-Prozessbrücke, Auslaufen alter Daten bei Stille.
- Blockrekonstruktion: fehlende Blöcke, Wiederholung erster Frames, Ergänzung bei
  erneuter Aussendung, Wechsel zwischen Bildern, alte Einzelframe-Zähler und
  beliebige Datei-IDs, ungültige Größen und kopflose Daten.
- GUI: vollständiges Empfangsbild während gleichzeitig laufender Sendung,
  Speicheraktion erst bei vollständigem Bild, STOP lässt RX aktiv, RX-Pause
  beendet die Aufnahme. Zusätzlich bleiben die früheren Bedienprüfungen grün.
- Privater PulseAudio-Server: echter Eingangsadapter mit expliziter Quellwahl,
  48-kHz-Float-Samples, gleichzeitige Wiedergabe/Aufnahme, keine automatische
  Ersatzquelle sowie Abbruch bei Entfernen des gewählten Eingangs.

Die GUI wurde mit beiden Bildern visuell geprüft. Die erwartete Qt-JPEG-Warnung
„Corrupt JPEG data: premature end of data segment“ wird ausschließlich während
der Dekodierung einer unvollständigen Empfangsvorschau ausgeblendet. Die lokale
Markierung gilt nur für den dekodierenden Thread. Andere Meldungen sowie dieselbe
Warnung beim Lesen vollständiger Bilder werden an den bisherigen Qt-Meldungshandler
weitergegeben. Die Tests prüfen Vorschau, unveränderten vollständigen Bildempfang
und die Weitergabe von Warnungen. Empfangene JPEG-Bytes bleiben unverändert.

PortAudio samt Eingangsadapter und GUI wird unter Linux kompiliert; ein tatsächlicher
Windows-/macOS-Lauf ist weiterhin nicht getestet. Die Prüfungen nutzen Dateien,
simulierte Quellen oder einen privaten Audioserver. Der Benutzer hat auch den praktischen Bildempfang über
QO100 bestätigt; die Monitor-Erweiterung wurde mit simuliertem Eingang geprüft. Das originale HS-Modem- und das QO100_TRX2-
Projekt wurden nicht verändert.
