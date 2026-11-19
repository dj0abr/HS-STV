# SPDX-License-Identifier: GPL-3.0-or-later
"""Image preparation and optional round trip through an independent HS-Modem RX."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import wave
import zlib
import numpy as np

def crc16(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x8408 if value & 1 else 0)
    return value

def png(path):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    width, height = 400, 200
    rows = b''.join(b'\0' + bytes(v for x in range(width) for v in (x % 256, y % 256, (x*3+y*2) % 256)) for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

with tempfile.TemporaryDirectory(prefix='mm-sstv-image-') as folder:
    root = Path(folder); source = root/'test-image.png'; png(source); output = root/'image.wav'
    mode = sys.argv[3] if len(sys.argv) > 3 else '8apsk'
    rate = 44100 if mode == 'qpsk4410' else 48000
    subprocess.run([sys.argv[1], str(source), str(output), '640', '480', '100'] + ([mode] if mode == 'qpsk4410' else []), check=True)
    jpeg = Path(str(output)+'.jpg').read_bytes()
    assert jpeg.startswith(b'\xff\xd8') and jpeg.endswith(b'\xff\xd9') and len(jpeg) <= 200000
    with wave.open(str(output)) as wav:
        assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) == (rate, 1, 2)
        samples = np.frombuffer(wav.readframes(wav.getnframes()), dtype='<i2').astype('<f4') / 32768
    assert len(samples) > rate*6 and np.max(abs(samples)) < 1
    assert abs(samples[-1]) < .0001, 'End of audio is not faded to zero'
    if len(sys.argv) >= 3:
        for auto in (False, True):
            args = [sys.argv[2]] + ([] if auto else ['--manual']) + (['--modulation', mode, '--input-rate', str(rate)] if mode == 'qpsk4410' else [])
            # Simulate the +81-Hz offset observed in the station for AFC testing.
            audio = samples
            if auto:
                # Analytic frequency shift, offline, with no effect on sample count.
                spectrum = np.fft.fft(samples)
                spectrum[1:len(samples)//2] *= 2
                spectrum[len(samples)//2+1:] = 0
                analytic = np.fft.ifft(spectrum)
                audio = np.real(analytic * np.exp(2j*np.pi*81*np.arange(len(samples))/rate)).astype('<f4')
            result = subprocess.run(args, input=audio.astype('<f4').tobytes(), capture_output=True, check=True, timeout=20)
            events = [json.loads(line) for line in result.stdout.splitlines()]
            assert any(e.get('locked') for e in events), 'No valid-frame lock'
            frames = [e for e in events if e.get('event') == 'frame' and e['frame_type'] == 2]
            recovered = {e['frame_number']: bytes.fromhex(e['payload_hex']) for e in frames}
            assert 0 in recovered, 'File header lost'
            header = recovered[0]
            size = int.from_bytes(header[52:55], 'big')
            count = (size+55+218)//219
            assert all(n in recovered for n in range(count)), f'Missing frames: {set(range(count))-set(recovered)}'
            content = b''.join(recovered[n] for n in range(count))[55:55+size]
            assert content == jpeg, 'Original RX recovered a different JPEG'
            assert int.from_bytes(header[50:52], 'big') == crc16(content)
            print(f'Original RX ({"AFC +81 Hz" if auto else "manual"}): locked; {count} image frames; {len(content)} JPEG bytes identical')
    print(f'JPEG and finite WAV export passed: {len(jpeg)} bytes, {len(samples)/rate:.3f} seconds')
