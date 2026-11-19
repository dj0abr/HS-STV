# Test helper extracted from QO100_TRX2 rx_monitor.py, GPL-3.0-or-later.
import numpy as np
import math
RATE = 48000
def spectrum(samples):
    """Hann-windowed amplitude spectrum and a tentative 20..900 Hz pilot."""
    n = min(32768, len(samples))
    if n < 4096:
        return np.array([]), np.array([]), None
    x = np.asarray(samples[-96000:], dtype=float)
    window = np.hanning(n)
    power = np.mean([np.abs(np.fft.rfft(x[start:start+n] * window)) ** 2
                     for start in range(0, len(x)-n+1, n//2)], axis=0)
    db = 10 * np.log10(np.maximum(power * (2 / window.sum()) ** 2, 1e-20))
    freq = np.fft.rfftfreq(n, 1 / RATE)
    indices = np.flatnonzero((freq >= 20) & (freq <= 900))
    # Rank narrow peaks by prominence over their local floor. The stronger
    # broadband data at 200..300 Hz must not hide a weak low-frequency pilot.
    gap = max(2, math.ceil(4*n/RATE))
    radius = max(gap+1, math.ceil(25*n/RATE))
    scores = []
    for i in indices:
        neighbors = np.r_[power[max(0, i-radius):max(0, i-gap)],
                          power[i+gap:min(len(power), i+radius+1)]]
        floor = max(float(np.median(neighbors)), 1e-30)
        scores.append(10 * math.log10(max(float(power[i]), 1e-30) / floor))
    best = int(np.argmax(scores))
    idx, prominence = indices[best], scores[best]
    pilot = None
    if prominence >= 10 and db[idx] > -110:
        logp = np.log(np.maximum(power[idx - 1:idx + 2], 1e-30))
        denominator = logp[0] - 2 * logp[1] + logp[2]
        delta = 0.5 * (logp[0] - logp[2]) / denominator if denominator else 0
        pilot = (float((idx + np.clip(delta, -0.5, 0.5)) * RATE / n), prominence)
    visible = freq <= 4000
    return freq[visible], db[visible], pilot

