# SPDX-License-Identifier: GPL-3.0-or-later
"""Real playback on a private PulseAudio server, never on the user's station."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import numpy as np

for tool in ('pulseaudio', 'pactl', 'parec'):
    if not shutil.which(tool):
        print(f'SKIP: {tool} unavailable'); sys.exit(77)
driver = sys.argv[1]
with tempfile.TemporaryDirectory(prefix='mm-sstv-audio-') as folder:
    root = Path(folder); runtime = root/'runtime'; runtime.mkdir(mode=0o700)
    socket = root/'native'; client = root/'client.conf'
    client.write_text('autospawn = no\nenable-shm = no\n')
    env = dict(os.environ, PULSE_SERVER=f'unix:{socket}', PULSE_CLIENTCONFIG=str(client), XDG_RUNTIME_DIR=str(runtime))
    log = (root/'server.log').open('wb')
    server = subprocess.Popen(['pulseaudio', '-n', '--daemonize=no', '--use-pid-file=no', '--exit-idle-time=-1',
                               '--load', f'module-native-protocol-unix socket={socket} auth-anonymous=1',
                               '--load', 'module-null-sink sink_name=mm_sstv_test rate=48000 channels=1 norewinds=1',
                               '--load', 'module-null-sink sink_name=other rate=48000 channels=1 norewinds=1'],
                              env=env, stdout=log, stderr=log)
    def pactl(*args):
        return subprocess.check_output(['pactl', *args], env=env, stderr=subprocess.STDOUT, timeout=4).decode()
    children = []
    try:
        for _ in range(100):
            if socket.exists():
                try: pactl('info'); break
                except subprocess.SubprocessError: pass
            if server.poll() is not None: raise RuntimeError((root/'server.log').read_text())
            time.sleep(.03)
        else: raise RuntimeError('Private audio server startup timed out')
        assert set(subprocess.check_output([driver, 'list'], env=env, text=True, timeout=5).split()) == {'mm_sstv_test', 'other'}
        pactl('set-default-sink', 'other')
        capture = root/'capture.raw'
        with capture.open('wb') as audio:
            recorder = subprocess.Popen(['parec', '--device=mm_sstv_test.monitor', '--format=float32le', '--rate=48000', '--channels=1', '--latency-msec=20'],
                                        env=env, stdout=audio, stderr=subprocess.PIPE); children.append(recorder)
            # PulseAudio null sinks initially advance their clock by two seconds.
            # Let the isolated monitor settle before measuring the transmitter.
            time.sleep(2.2)
            result = subprocess.run([driver, 'run', 'mm_sstv_test', '1800'], env=env, capture_output=True, text=True, timeout=7)
            assert result.returncode == 0, result.stderr
            print(result.stdout.strip())
            time.sleep(.3); recorder.terminate(); recorder.wait(timeout=3)
        samples = np.fromfile(capture, dtype='<f4')
        active = np.flatnonzero(abs(samples) > .001)
        print(f'Captured {len(samples)} samples, {len(active)} active, peak {np.max(abs(samples)) if len(samples) else 0:.4f}')
        assert len(active) > 48000, 'No continuous audio reached the selected sink'
        assert len(samples) - active[-1] > 5000, 'Audio did not stop after STOP'
        x = samples[active[0]+10000:active[0]+58000]
        assert len(x) == 48000
        power = abs(np.fft.rfft(x*np.hanning(len(x))))**2
        hz = np.fft.rfftfreq(len(x), 1/48000)
        assert hz[(hz > 80) & (hz < 120)][np.argmax(power[(hz > 80) & (hz < 120)])] == 100
        assert np.max(abs(samples)) < 1, 'Clipped output'
        assert 'underruns 0' in result.stdout, result.stdout
        finite_capture = root/'finite.raw'
        with finite_capture.open('wb') as audio:
            recorder = subprocess.Popen(['parec', '--device=mm_sstv_test.monitor', '--format=float32le', '--rate=48000', '--channels=1', '--latency-msec=20'], env=env, stdout=audio, stderr=subprocess.PIPE)
            children.append(recorder); time.sleep(.2)
            finite = subprocess.run([driver, 'finite', 'mm_sstv_test', '1000'], env=env, capture_output=True, text=True, timeout=6)
            assert finite.returncode == 0, finite.stderr
            time.sleep(.2); recorder.terminate(); recorder.wait(timeout=3)
        captured = np.fromfile(finite_capture, dtype='<f4')
        assert np.count_nonzero(captured > .1) >= 47990, 'Final samples discarded instead of drained'
        assert np.count_nonzero(captured > .3) >= 470, 'Final marker truncated'
        assert not pactl('list', 'short', 'sink-inputs').strip(), 'Finite stream leaked'
        print('Finite playback drained all 48000 samples including the final marker')
        capture_file = root/'native-input.raw'
        capture_process = subprocess.Popen([driver, 'capture', 'mm_sstv_test.monitor', '1800', str(capture_file)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        children.append(capture_process); assert capture_process.stdout.readline().strip() == 'capturing'
        finite = subprocess.run([driver, 'finite', 'mm_sstv_test', '1000'], env=env, capture_output=True, text=True, timeout=6)
        assert finite.returncode == 0, finite.stderr
        _, error = capture_process.communicate(timeout=4)
        assert capture_process.returncode == 0, error
        native = np.fromfile(capture_file, dtype='<f4')
        assert np.count_nonzero(native > .1) >= 47000 and np.count_nonzero(native > .3) >= 470, 'Native input lost stream or final marker'
        missing = subprocess.run([driver, 'capture', 'missing-input', '100', str(root/'missing.raw')], env=env, capture_output=True, text=True, timeout=5)
        assert missing.returncode != 0, 'Capture silently fell back to a different source'
        print('Native input: explicit source, float48k, concurrent playback/capture, final marker and missing-source handling pass')
        result = subprocess.run([driver, 'cycles', 'mm_sstv_test', '200'], env=env, capture_output=True, text=True, timeout=8)
        assert result.returncode == 0 and result.stdout.count('stopped') == 3, result
        print('Three START/STOP cycles passed')
        result = subprocess.run([driver, 'run', 'missing', '200'], env=env, capture_output=True, text=True, timeout=5)
        assert result.returncode != 0 and 'started' not in result.stdout, 'Missing sink silently fell back'
        assert not pactl('list', 'short', 'sink-inputs').strip(), 'Playback stream leaked'
        # Removing the selected sink must end playback, not move it to "other".
        running = subprocess.Popen([driver, 'run', 'mm_sstv_test', '5000'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        children.append(running); assert running.stdout.readline().strip() == 'started'
        source_loss = subprocess.Popen([driver, 'capture', 'mm_sstv_test.monitor', '5000', str(root/'removed.raw')], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        children.append(source_loss); assert source_loss.stdout.readline().strip() == 'capturing'
        module = next(line.split()[0] for line in pactl('list', 'short', 'modules').splitlines() if 'sink_name=mm_sstv_test ' in line)
        pactl('unload-module', module)
        _, error = running.communicate(timeout=4)
        assert running.returncode == 3, (running.returncode, error)
        assert not pactl('list', 'short', 'sink-inputs').strip(), 'Stream migrated to a different sink'
        source_loss.communicate(timeout=4)
        assert source_loss.returncode == 3, 'Capture did not stop when selected source vanished'
        print('Device removal stopped playback and capture without fallback')
        running = subprocess.Popen([driver, 'run', 'other', '5000'], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        children.append(running); assert running.stdout.readline().strip() == 'started'
        server.terminate(); server.wait(timeout=4)
        _, error = running.communicate(timeout=4)
        assert running.returncode == 3, (running.returncode, error)
        print('Server failure stopped playback promptly')
    finally:
        for child in children:
            if child.poll() is None: child.kill(); child.wait(timeout=3)
        if server.poll() is None: server.terminate(); server.wait(timeout=4)
        log.close()
