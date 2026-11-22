# Independent AO-40 test vectors

Copyright 2020 Daniel Estevez <daniel@destevez.net>, GPL-3.0-or-later.
From https://github.com/daniestevez/gr-satellites at
`0ff8b614c3ed57110c3497c287bced9a01bbc238`:

- `ao40_frame.f32`: unchanged `python/components/deframers/qa_ao40_fec_deframer_frame.f32`.
- `post_viterbi.bin`, `payload.bin`: byte arrays extracted without GNU Radio
  from `post_viterbi_reference` / `frame_reference` in
  `python/components/deframers/qa_ao40_fec_deframer.py`.

Full license: [GPL v3](../../../external/qo100-psk/COPYING).

## Historische Audioaufnahme und Vergleichsergebnis

`sample.wav` stammt unverändert aus TA2LDV/qo100-beacon-decoder, Revision
`e5aeb3e47d0dc169f00d774f5de60cf4a11a5b80`, GPL-3.0.
SHA-256: `8788f0385db5ca201e8e113a6f5bb0acb1fb2fe763f9c96ddf7533e6a6a79b6f`.
Die historische Aufnahme enthält Ankündigungen aus 2020, keine aktuellen Texte.
Der RIFF-/Datenkopf enthält eine bekannte übergroße Längenangabe; der Test liest
nur tatsächlich vorhandene vollständige PCM-Samples.

`recording.json` wurde mit dem unveränderten nativen QO100_TRX2-Dekoder
(Revision `7de6dbd001d22d54b1346b32f43e1bd146b59cc6`) erzeugt:
`qo100_beacon_decoder --wav sample.wav --rx-hz 10489748500 --cpu-percent 100 --json`.
Es enthält alle sechs CRC- und fünf FEC-Nutzlasten der Aufnahme sowie den Träger.
Der HS-STV-Test verlangt bytegleiche Nutzlasten; die FFT wird unabhängig davon
mit der vorhandenen liquid-dsp-Bibliothek berechnet.
