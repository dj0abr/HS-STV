# Bildversand und gültige Testframes – Version 0.2

Historischer Prüfbericht des 7200-bit/s-Stands. Aktuell senden und empfangen wir
auf Benutzerwunsch mit 6000 bit/s; siehe [EMPFANG.md](EMPFANG.md) und
[PROVENANCE.md](PROVENANCE.md).

Stand des Bildsenders: 19.11.2026. Der spätere Empfang ist in [EMPFANG.md](EMPFANG.md) dokumentiert. Physikalischer Modus unverändert: 8APSK 7200 bit/s,
2400 Symbole/s, 48 kHz. 100 % bleibt digitale Vollaussteuerung. Der Screenshot
des Benutzers zeigte die erwartete 1+7-Konstellation und einen Pilotversatz von
etwa +81 Hz, aber noch keine durch gültige Frames bestätigte Synchronisation.
Die frühere Zufallsaussendung enthielt noch kein Frameprotokoll.

## Implementierung

Der neue Encoder erzeugt 258-Byte-Funkframes:

```text
53 E1 A6 | Zähler-LSB | Status/Typ/Zähler-MSB | 219 Byte Payload | CRC16 | 32 Byte RS
           <------------------- 255 Byte, gescrambelt ------------------------>
```

CRC: Start FFFF, reflektiertes Polynom 8408, kein finales XOR, MSB zuerst.
RS(255,223): GF(256), primitives Polynom 187, Generatorwurzeln alpha^120 bis
alpha^151. Der Encoder ist eigenständig implementiert. Der Scrambler verwendet
die unveränderte feste Originaltabelle und beginnt bei jedem Frame wieder bei 0.
Die gesamten Framebytes werden MSB zuerst in Dreiergruppen zu 8APSK-Symbolen.

Für JPEG-Dateien ist der Typ 2. Der 55-Byte-Dateikopf enthält den ASCII-Namen
(50 Byte), die Inhalts-CRC als Datei-ID (2 Byte) und die JPEG-Länge (3 Byte),
beides MSB zuerst. Im ersten Frame bleiben 164 JPEG-Bytes, danach je 219 Byte.
Der Rest des letzten Frames wird mit Nullbytes gefüllt. Dateiname und JPEG werden
nicht zusätzlich gezippt. Zähler sind auf 10 Bit begrenzt; die Dateigrenze von
200000 Byte bleibt darunter. Die Statuswerte sind 0/1/2/3 für erster/Folge-/letzter/
einziger Frame. Auch der Einzelframe beginnt eindeutig bei Zähler 0.

Die erste Bildframe-Ausgabe wird mit 21 weiteren Kopien wiederholt (insgesamt
22 Frames, ungefähr 6,3 s). Der letzte Frame erscheint sechsmal. Einzelframes
werden insgesamt 22-mal gesendet. Filter- und Oszillatorzustände bleiben über alle
Frames erhalten. 30 abschließende Mittelpunkt-Symbole lassen das RRC auslaufen;
die letzten 5 ms blenden die gemeinsame Amplitude auf Null aus.

Die Audioport-Schnittstelle unterstützt jetzt endliche Quellen: kurze
Rückgabeblöcke signalisieren das Ende. PulseAudio entleert dann regulär den
Wiedergabepuffer; PortAudio verwendet `paComplete`. Erst nach dem vollständigen
Abspielen meldet der Adapter `finished`. STOP verwendet weiterhin Abbruch und
Pufferverwerfen. Dadurch werden am normalen Ende keine letzten Bildframes
abgeschnitten.

Unter Test gibt es kontinuierlich gültige Typ-1-Frames mit aufsteigendem Zähler
und dem originalen druckbaren Testmuster A bis y. Die frühere ungeframte
Zufallsaussendung bleibt separat erhalten.

Die GUI erlaubt Dateiablage oder Dateiauswahl, 320×240/640×480/800×600/1024×768,
proportionales Einpassen mit weißen Rändern, automatische JPEG-Qualitätswahl,
Vorschau des tatsächlich kodierten JPEG, Senden/STOP und Fortschritt. Empfang
ist bewusst noch nicht implementiert.

## Prüfung

Alle sieben auf dem Entwicklungsrechner konfigurierten CTest-Gruppen bestehen:

- `protocol_original_vectors`: 64 vollständige Frames bytegenau gleich dem
  ursprünglichen Pack()-Pfad einschließlich dessen Schifra-FEC, CRC und Scrambler.
  Die Vektoren wurden durch die unveränderten Originalfunktionen in einem
  temporären Hilfsprogramm erzeugt; Quelle und Prüfsummen stehen unter
  `tests/data/original_frames.json`. Nur die Vektoren, nicht die Hilfsbibliothek,
  gehören zum neuen Projekt.
- Protokoll-Randfälle: Dateilängen 4, 164, 165, 383, 384, 56010 und 200000 Byte;
  Einzelframe, exakt volle Nutzlast, Zählerbits über 255, Endpadding, Status,
  Wiederholungen und Wiederherstellung aller Datei-Bytes. Zusätzlich die
  Original-BER-Nutzlast und MSB-Symbolreihenfolge.
- `phy_reference` und `signal_spectrum`: bestehende Prüfungen für Originalwellenform,
  7200-bit/s-Symboltakt, Pilot, Spektrum und Vollaussteuerung weiterhin erfolgreich.
- `gui_controls`: neun Funktionsprüfungen plus Setup/Abbau; Geräteauswahl,
  Senden/STOP, Fehlererholung, normaler Abschluss, JPEG-Budget bei Rauschbildern,
  Seitenverhältnis/Ränder, Drag-and-drop, Größenwechsel und Testframes ohne Bild.
- `image_export`: gültiges JPEG und endliche PCM16-WAV-Datei mit korrektem
  Ausblenden, ohne Audioausgang.
- `original_rx_image`: 26047-Byte-JPEG aus 120 Datei-Frames durch den unabhängigen
  HS-Modem-RX-Kern von QO100_TRX2 vollständig und byteidentisch zurückgewonnen;
  gültige Frame-Synchronisation sowohl ohne Versatz als auch mit automatisch
  korrigierten +81 Hz. Datei-ID stimmt mit der Inhalts-CRC überein.
- `private_audio`: keine Aussetzer; dreifaches START/STOP; Fehler bei Geräte- und
  Serververlust; zusätzlich endlicher Strom mit 48000 Samples und Endmarker bis
  zum letzten Sample vollständig abgespielt. Kein offener Stream nach Abschluss.

Der zusätzliche RX-Test ist optional und wird nur mit `MM_SSTV_RX_PROBE` aktiviert.
Das ausgelieferte Projekt baut und prüft den Frameencoder ohne Zugriff auf ein
Nachbarprojekt. `build/Testing/Temporary/LastTest.log` enthält das aktuelle Protokoll.
Die Oberfläche wurde außerdem als Offscreen-Bild visuell geprüft.

Der aktualisierte PortAudio-Build mit GUI und Bildexport wird unter Linux gebaut;
Windows/macOS sind weiterhin nicht als getestet ausgewiesen. Sämtliche Prüfungen
verwenden Dateien, simulierte Adapter oder einen privaten PulseAudio-Server.
Die praktische Abnahme über QO100 und die Anzeige im originalen HS-Modem bleiben
beim Benutzer; der neue hs-stv-Prozess wurde dazu nicht automatisch auf Sendung
geschaltet.
