# Waterfall-ID vor der Bildsendung

Die Checkbox **Waterfall-ID** unten neben der Betriebsart schaltet eine automatisch
passende Kennung vor jeder Bildsendung ein. Die Auswahl wird gespeichert; bei einer
neuen Installation ist sie ausgeschaltet. Die Kennungen lauten:

| Betriebsart | Text | Zusätzliche Dauer einschließlich Pause |
|---|---|---|
| 8APSK | HS-STV 8APSK | 10,4 s |
| QPSK | HS-STV QPSK | 9,6 s |
| Martin 1 | HS-STV MARTIN1 | 12,0 s |
| Martin 2 | HS-STV MARTIN2 | 12,0 s |
| Scottie 1 | HS-STV SCOTTIE1 | 12,8 s |
| Scottie 2 | HS-STV SCOTTIE2 | 12,8 s |

Vor dem ersten Buchstaben wird ein Punktmarker **500 ms** lang gesendet,
gefolgt von **100 ms** Abstand. Der Marker beginnt direkt mit Audio, damit eine
audiogesteuerte PTT rechtzeitig anzieht. Ein gewöhnliches Punktzeichen mit
führenden Leerspalten und anschließender langer Pause wird dafür nicht verwendet.

Der Text erscheint seitlich gedreht im Wasserfall. Bei einem nach unten scrollenden
Wasserfall steht das erste Zeichen unten. Die Kennung benötigt keinen Decoder.
Nach dem Text folgen 300 ms Stille und automatisch die normale Bildsendung.
Die Audioverbindung wird dabei nicht geschlossen oder neu geöffnet.
Die angegebene Sendedauer und der TX-Fortschritt berücksichtigen Punktvorlauf und Kennung.
Der Status wechselt von **Sendet Waterfall-ID** auf **Sendet Bild**.

Der Pegelregler wirkt auch während der Kennung. Bei SSTV gilt der gleiche kalibrierte
Maximalpegel wie für das Bildsignal. **STOP** bricht Kennung und Bild gemeinsam ab.
Während der Sendung ist die Checkbox gesperrt. Ohne Häkchen beginnt die bisherige
Bildsendung direkt; Bildprotokoll, VIS und Bilddaten bleiben unverändert.

## Portabler Encoder

Das Verfahren und der 7×9-Zeichensatz stammen aus QO100_TRX2, siehe
[Quellenmanifest](WATERFALL-REFERENCE.json) und [Herkunft](PROVENANCE.md).
Neun Frequenzbänder bestehen jeweils aus sieben Tönen im 25-Hz-Raster; zwischen
den Bändern liegen jeweils zwei freie Tonpositionen. Die Töne reichen von 525
bis 2475 Hz. Schriftspalten werden nacheinander mit 100 ms Dauer gesendet.
Dies liegt im bereits getesteten Bereich des ursprünglichen Encoders (100–400 ms).
Jede Rasterzeile wird über 10 ms weich ein- und ausgeblendet. Verteilt gewählte
Startphasen verringern die Überlagerungsspitzen; ein gemeinsamer Normierungsfaktor
für die ganze Kennung erhält die relative Helligkeit der Schriftstriche.

Der Kern `WaterfallTextAudio` erzeugt Mono-Float-PCM in C++17 ohne Qt-, Browser-,
JSON-, Schrift- oder Betriebssystem-Abhängigkeit. QPSK nutzt nativ 44100 Hz,
die übrigen Modi 48000 Hz. Periodentabellen und PCM werden vor dem Start der
Soundkarte aufgebaut; der Audio-Callback übernimmt nur Ausgabe und Pegelrampe.
Die vorhandenen PulseAudio-/PortAudio-Adapter bleiben unverändert.

## Prüfungen

`waterfall_id_audio` prüft beide Sampleraten mit einer unabhängigen Tonkorrelation,
dem sofort aktiven Punktvorlauf, der vollständigen Referenzrasterfolge R_F,
Pausen, Flanken, Pegeländerung und Clipping.
Für alle sechs Modi werden Kennung, Kalibrierung, Samplezahl, Fortschritt und
Abbruch geprüft. Die ersten Bildsamples hinter der Kennung müssen exakt dem
bisherigen Sender entsprechen, auch bei einem Wechsel mitten im Audio-Callback.
Bei digitaler Übertragung werden zusätzlich Ende und vollständiger Ablauf geprüft.
Der GUI-Test prüft gespeicherte Auswahl, Moduswechsel und STOP während der Kennung
sowie das kompakte Layout bei 640×440 Pixeln.
