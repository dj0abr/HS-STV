# SPDX-License-Identifier: GPL-3.0-or-later
"""Native HS-Modem QPSK4410 waveform, acquisition and robustness checks."""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import wave
import numpy as np
probe, fixture, signal = sys.argv[1:4]
def audio(offset=0, phase=0, mode='clean', frames=65):
    return subprocess.check_output([fixture, str(offset), mode, str(frames), 'qpsk4410', str(phase)])
def receive(samples, rate=44100):
    r = subprocess.run([probe, '--modulation', 'qpsk4410', '--input-rate', str(rate)], input=samples, capture_output=True, check=True, timeout=15)
    return [json.loads(line) for line in r.stdout.splitlines()]
def check(events, minimum=45):
    assert events[-1]['locked'] and events[-1]['valid_frames'] >= minimum, events[-1]
    assert events[-1]['modulation'] == 'qpsk4410' and events[-1]['bit_rate'] == 4410
    for e in events:
        if e['event'] == 'frame':
            assert e['frame_type'] == 8 and e['frame_info'] == 3
            assert bytes.fromhex(e['payload_hex'])[1:].rstrip(b'\0') == b'TEST CW-SKIMMER RX VALIDATION'
for offset in (-75, 0, 25, 81, 180, 400, 750):
    for phase in (0, math.pi/2, math.pi, 3*math.pi/2, .7): check(receive(audio(offset, phase)))
for mode in ('drift', 'jump', 'dropout', 'interferer'): check(receive(audio(25, mode=mode, frames=120)), 70)
clean = np.frombuffer(audio(81), dtype='<f4')
for rate in (48000, 12000):
    positions = np.arange(int(len(clean)*rate/44100))*44100/rate
    converted = np.interp(positions, np.arange(len(clean)), clean).astype('<f4')
    check(receive(converted.tobytes(), rate))
# Fixed noise density and low signal power, rather than scaling signal and noise together.
weak = np.frombuffer(audio(frames=90), dtype='<f4').astype(float)
weak *= .02 / np.sqrt(np.mean(weak*weak))
weak += np.random.default_rng(90).normal(0, .025*np.sqrt(44100/48000), len(weak))
check(receive(weak.astype('<f4').tobytes()), 70)
noise = np.random.default_rng(45).normal(0,.01,len(clean))
check(receive((clean*.1 + noise*.1).astype('<f4').tobytes()))
for samples in (audio(81, mode='corrupt'), np.zeros(44100*5, dtype='<f4').tobytes(),
                np.random.default_rng(7).normal(0,.01,44100*5).astype('<f4').tobytes()):
    e = receive(samples); assert not any(x.get('locked') for x in e) and not any(x['event']=='frame' for x in e)
with tempfile.TemporaryDirectory(prefix='mm-sstv-qpsk-') as folder:
    path=Path(folder)/'signal.wav'
    subprocess.run([signal, str(path), '5', '100', '42', 'qpsk4410'],check=True)
    with wave.open(str(path)) as wav:
        assert (wav.getframerate(),wav.getnframes())==(44100,220500)
        samples=np.frombuffer(wav.readframes(wav.getnframes()),dtype='<i2')/32768.
    assert .8<abs(samples).max()<1
    x=samples[44100:132300];freq=np.fft.rfftfreq(len(x),1/44100)
    power=abs(np.fft.rfft(x*np.hanning(len(x))))**2
    inside=(freq>=157)&(freq<=2843);outside=(freq>20)&~inside&(abs(freq-100)>30)
    assert power[outside].sum()/power[inside].sum()<.002
    region=(freq>80)&(freq<120)
    assert abs(freq[region][np.argmax(power[region])]-100)<.5
print('QPSK4410: original framing/Gray mapping, 35 phase/frequency combinations, drift/jump/dropout, low level, 12/44.1/48-kHz inputs, CRC/noise gates and 2646-Hz band/pilot passed')
