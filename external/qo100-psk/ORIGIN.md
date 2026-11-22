# Nativer QO-100-PSK-Bakendekoder

Übernommen aus dem lokalen Projekt QO100_TRX2, Revision
`7de6dbd001d22d54b1346b32f43e1bd146b59cc6`:
`decoder/beacon_dsp.hpp`, `decoder/beacon_dsp.cpp`, `decoder/beacon_fec.cpp`.
Diese Dateien stehen unter **GPL-3.0-only**; die SPDX-Hinweise bleiben erhalten.
Die vollständige Lizenz steht in `COPYING`.

Der QO100_TRX2-Kern portiert die Verfahren von Levent, TA2LDV,
https://github.com/ta2ldv/qo100-beacon-decoder, Revision
`e5aeb3e47d0dc169f00d774f5de60cf4a11a5b80`. Die FEC-Referenz folgt den
Konventionen von Daniel Estevez/gr-satellites und Phil Karn/libfec.

HS-STV ersetzt die FFTW-Aufrufe durch die bereits eingebundene liquid-dsp-FFT
und die GCC-spezifische Paritätsfunktion durch portables C++17. Framing,
Manchester-/Differenzialdemodulation, Träger- und Taktsuche, CRC, Viterbi,
Descrambler und Reed-Solomon bleiben fachlich unverändert. Es werden keine
Python-Dateien und keine UDP-/Linux-Prozesssteuerung übernommen.
`tools/psk_rx.cpp` stellt den neuen portablen Soundkarten-/Prozessadapter bereit.

Die unabhängigen FEC-Vektoren, Aufnahme und erwarteten Ergebnisse stehen in
`tests/data/psk-beacon`; Herkunft und Prüfsumme sind dort dokumentiert.

Prüfsummen der übernommenen Quelldateien vor der HS-STV-Anpassung:

| Datei | SHA-256 |
|---|---|
| beacon_dsp.cpp | 345a342caf38c7c6b484b9c2732187503bb5eee3984803c8d380ed7b013e7919 |
| beacon_dsp.hpp | ba325efc86d2dc8d1dd65c718bf79030ccaccecc49403e378cb1183c47ae9c8a |
| beacon_fec.cpp | 3a63781ac9cf811f9411c0424cbfab696af5670756d00b3e40acb006bcb0bdad |
