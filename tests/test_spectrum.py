# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure exported audio, independently of DSP implementation details."""
import pathlib
import subprocess
import sys
import tempfile
import wave
import numpy as np

with tempfile.TemporaryDirectory(prefix='mm-sstv-spectrum-') as folder:
    path = pathlib.Path(folder) / 'signal.wav'
    subprocess.run([sys.argv[1], str(path), '8', '100', '42'], check=True)
    with wave.open(str(path)) as wav:
        assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()) == (48000, 1, 2, 384000)
        audio = np.frombuffer(wav.readframes(wav.getnframes()), dtype='<i2').astype(float) / 32767
    assert .9 < np.max(np.abs(audio)) < 1, 'Full-scale output missing or clipped'
    # Exact two-second window removes startup; coherent 100-Hz bin separates pilot.
    x = audio[48000:144000]
    spectrum = np.abs(np.fft.rfft(x)) ** 2
    frequency = np.fft.rfftfreq(len(x), 1 / 48000)
    pilot = np.argmax(spectrum[(frequency >= 80) & (frequency <= 120)])
    pilot_hz = frequency[(frequency >= 80) & (frequency <= 120)][pilot]
    assert abs(pilot_hz - 100) <= .5, pilot_hz
    pilot_amplitude = 2 * np.sqrt(spectrum[np.argmin(abs(frequency - 100))]) / len(x)
    assert .078 < pilot_amplitude < .081, pilot_amplitude
    # Windowed measurement of the data band, excluding pilot and its neighborhood.
    power = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    inside = (frequency >= 280) & (frequency <= 2720)
    outside = (frequency > 20) & ~inside & (abs(frequency - 100) > 30)
    leakage = float(power[outside].sum() / power[inside].sum())
    assert leakage < .002, leakage
    data = (frequency >= 300) & (frequency <= 2700) & (abs(frequency - 100) > 30)
    centroid = float((power[data] * frequency[data]).sum() / power[data].sum())
    assert 1430 < centroid < 1570, centroid
    # 6000 bit/s must no longer occupy the wider 7200-mode edges.
    low_edge = (frequency >= 100) & (frequency <= 240) & (abs(frequency - 100) > 30)
    high_edge = (frequency >= 2760) & (frequency <= 2900)
    assert power[low_edge].sum() / power[data].sum() < .0001, 'Data spills into the low 7200-mode edge'
    assert power[high_edge].sum() / power[data].sum() < .0001, 'Data spills into the high 7200-mode edge'
    print(f'Pilot {pilot_hz:.1f} Hz, amplitude {pilot_amplitude:.5f}; data centroid {centroid:.1f} Hz; out-of-band ratio {leakage:.6f}')
    subprocess.run([sys.argv[1], str(path), '1', '0'], check=True)
    with wave.open(str(path)) as wav:
        assert not np.frombuffer(wav.readframes(48000), dtype='<i2').any(), '0% output is not silence'
