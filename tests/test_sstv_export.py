#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise native 48-kHz TX, actual SSTV subprocess RX and spectrum on both modes."""
import json
import pathlib
import subprocess
import sys
import tempfile
import wave
import numpy as np

exporter, receiver = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    y, x, color = np.indices((256, 320, 3))
    pixels = (25 + (x // 80 * 44 + y // 16 * 19 + color * 71) % 202).astype(np.uint8)
    image = directory / 'colors.ppm'
    image.write_bytes(b'P6\n320 256\n255\n' + pixels.tobytes())
    # Independent published timings, in seconds, including the VIS header.
    profiles = [('martin1', 115.200176, .91, .446446, 0., .004862),
                ('scottie1', 110.54332, .919, .428220, .279480, .009),
                ('martin2', 58.970288, .91, .226798, 0., .004862),
                ('scottie2', 72.008152, .919, .277692, .179128, .009)]
    for mode, duration, start, period, sync_start, sync_length in profiles:
        audio = directory / (mode + '.wav')
        subprocess.run([exporter, str(image), str(audio), mode, '100'], check=True, capture_output=True)
        assert pathlib.Path(str(audio) + '.png').is_file()
        with wave.open(str(audio), 'rb') as f:
            assert (f.getframerate(), f.getnchannels(), f.getsampwidth()) == (48000, 1, 2)
            samples = np.frombuffer(f.readframes(f.getnframes()), dtype='<i2').astype(np.float32) / 32768
        assert len(samples) == round(duration * 48000) + 240 + 14400, (mode, len(samples))
        assert abs(np.max(np.abs(samples)) - (6 / 35 * 1.15 * 1.20)) < 2 / 32768, '100% must reach the calibrated SSTV maximum'
        assert np.all(samples[-14400:] == 0), 'Final silence must let decoder finish its last row'
        # The entire 0..100% slider must scale linearly, including above 35%.
        for percent, factor in [(0, 0.), (35, .35), (70, .7)]:
            level_audio = directory / f'{mode}-{percent}.wav'
            subprocess.run([exporter, str(image), str(level_audio), mode, str(percent)], check=True, capture_output=True)
            with wave.open(str(level_audio), 'rb') as f:
                level_samples = np.frombuffer(f.readframes(48000), dtype='<i2').astype(np.float32) / 32768
            assert np.max(np.abs(level_samples)) < 1, 'SSTV output clipped'
            error = np.max(np.abs(level_samples[4800:] - samples[4800:48000] * factor))
            assert error <= 2 / 32768, (mode, percent, error)
        # Native 1900-Hz VIS leader; there must be no digital modem pilot.
        leader = samples[4800:14400]
        spectrum = np.abs(np.fft.rfft(leader * np.hanning(len(leader))))
        assert spectrum[20] / spectrum[380] < 1e-4, 'Unexpected 100-Hz pilot in SSTV'
        result = subprocess.run([receiver, '--modulation', mode, '--input-rate', '48000', '--monitor'],
                                input=samples.astype('<f4').tobytes(), capture_output=True, check=True, timeout=30)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        begin = [e for e in events if e['event'] == 'sstv_begin']
        lines = [e for e in events if e['event'] == 'sstv_line']
        endings = [e for e in events if e['event'] == 'sstv_end']
        assert len(begin) == 1 and not begin[0]['partial'], (mode, begin)
        assert len(lines) == 256 and [e['index'] for e in lines] == list(range(256)), (mode, len(lines))
        assert len(endings) == 1 and endings[0]['complete'], (mode, endings)
        assert sum(e['synchronized'] for e in lines) >= 254
        decoded = np.array([list(bytes.fromhex(e['rgb_hex'])) for e in lines], dtype=np.int16).reshape(256, 320, 3)
        # Avoid analog color transitions when checking plateau amplitude accuracy.
        error = np.abs(decoded[:, [35, 115, 195, 275], :] - pixels[:, [35, 115, 195, 275], :]).mean()
        assert error < 6, (mode, error)
        status = [e for e in events if e['event'] == 'status']
        assert any(e['locked'] and len(e['spectrum_dbfs']) > 2700 for e in status)
        assert not status[-1]['locked'], 'Stale line sync after frame end'
        assert status[-1]['sync_detected'] == sum(line['synchronized'] for line in lines)
        assert status[-1]['sync_detected'] + status[-1]['sync_missed'] == 256
        waterfall_rows = [row for event in status for row in event['waterfall_dbfs']]
        assert len(waterfall_rows) == len(samples) // 480 - 2, 'Waterfall skipped 10-ms audio intervals'
        assert all(len(row) == 86 for row in waterfall_rows)
        assert all(event['waterfall_bin_hz'] == 46.875 for event in status)
        assert max(row[26] for row in waterfall_rows[100:600]) > -40, 'Sync missing from short-window waterfall'
        assert any(e['received_lines'] > 10 and e['received_lines'] < 50 for e in status), 'No progressive rows'
        print(f'{mode}: full VIS, 256 ordered RGB rows, mean color error {error:.2f}, live spectrum, calibrated TX level, no 100-Hz pilot')
        # Late entry must remain a partial image even when the decoder reaches its predicted end.
        late = subprocess.run([receiver, '--modulation', mode], input=samples[3 * 48000:].astype('<f4').tobytes(),
                              capture_output=True, check=True, timeout=30)
        late_events = [json.loads(line) for line in late.stdout.splitlines()]
        late_begin = [e for e in late_events if e['event'] == 'sstv_begin']
        late_lines = [e for e in late_events if e['event'] == 'sstv_line']
        late_end = [e for e in late_events if e['event'] == 'sstv_end']
        assert len(late_begin) == 1 and late_begin[0]['partial']
        assert 230 < len(late_lines) < 256, (mode, len(late_lines))
        assert len(late_end) == 1 and not late_end[0]['complete'], (mode, late_end)
        interrupted = subprocess.run([receiver, '--modulation', mode], input=samples[:15 * 48000].astype('<f4').tobytes(),
                                     capture_output=True, check=True, timeout=30)
        interrupted_events = [json.loads(line) for line in interrupted.stdout.splitlines()]
        assert any(e['event'] == 'sstv_line' for e in interrupted_events)
        assert [e['complete'] for e in interrupted_events if e['event'] == 'sstv_end'] == [False]
        print(f'{mode}: missing VIS and interrupted stream preserve partial status')
        # Remove eight real sync pulses. Skipped rows must count too, rather than
        # counting only unsynchronized lines that the decoder chose to render.
        damaged = samples.copy()
        for row in range(50, 58):
            first = round((start + row * period + sync_start) * 48000)
            last = round((start + row * period + sync_start + sync_length) * 48000)
            damaged[first:last] = 0
        result = subprocess.run([receiver, '--modulation', mode], input=damaged.astype('<f4').tobytes(),
                                capture_output=True, check=True, timeout=30)
        damaged_events = [json.loads(line) for line in result.stdout.splitlines()]
        damaged_status = [e for e in damaged_events if e['event'] == 'status'][-1]
        damaged_lines = [e for e in damaged_events if e['event'] == 'sstv_line']
        assert damaged_status['sync_missed'] >= 8, (mode, damaged_status['sync_missed'])
        assert damaged_status['sync_detected'] == sum(line['synchronized'] for line in damaged_lines)
        assert damaged_status['sync_detected'] + damaged_status['sync_missed'] == 256
        print(f"{mode}: {damaged_status['sync_detected']} detected and {damaged_status['sync_missed']} missing syncs")
