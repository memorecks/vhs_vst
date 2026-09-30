#!/usr/bin/env python3
"""Compare the plug-in against captures of the Reaktor original.

resources/test_tones holds a 440 Hz, -12 dBFS sine (reference_tone_440hz_-12db.wav) and the same tone
recorded through each of the 22 snapshots in Reaktor (preset1.wav ... preset22.wav). This renders the
tone through the plug-in's factory presets with the offline harness and prints, per preset, the level of
the fundamental and the 2nd/3rd harmonics (relative to the fundamental) for both, plus the differences.

    pip install numpy scipy soundfile
    cd plugin && cmake -S . -B build -DVHS_BUILD_TESTS=ON && cmake --build build --target VHSTest
    python3 tools/compare_reaktor.py

Bands are +-12 % around each harmonic, so wow and flutter smearing stays inside the measurement.
Preset 22 ("Be Kind; Rewind") modulates so heavily that its numbers are only indicative.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfiltfilt

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TONES = os.path.join(ROOT, 'resources', 'test_tones')
HARNESS = os.path.join(ROOT, 'plugin', 'build', 'VHSTest_artefacts', 'Release', 'VHSTest')


def harmonics(path):
    x, sr = sf.read(path, always_2d=True)
    m = x[len(x) // 4:, 0]   # skip the start-up
    out = []
    for k in (1, 2, 3):
        sos = butter(4, [440 * k * 0.88, 440 * k * 1.12], 'bandpass', fs=sr, output='sos')
        out.append(20 * np.log10(np.sqrt(np.mean(sosfiltfilt(sos, m) ** 2)) + 1e-12))
    return np.array(out)


def main():
    if not os.path.exists(HARNESS):
        sys.exit(f'build the offline harness first ({HARNESS} not found)')
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([HARNESS, '--render', os.path.join(TONES, 'reference_tone_440hz_-12db.wav'), tmp],
                       check=True, stdout=subprocess.DEVNULL)
        rows = []
        print(' #  |  Reaktor H1  H2r  H3r |  plug-in H1  H2r  H3r |  dH1   dH3r')
        for i in range(1, 23):
            r = harmonics(os.path.join(TONES, f'preset{i}.wav'))
            v = harmonics(os.path.join(tmp, f'preset{i}.wav'))
            d1, d3 = v[0] - r[0], (v[2] - v[0]) - (r[2] - r[0])
            rows.append((d1, d3))
            print(f'{i:2d}  |  {r[0]:9.1f} {r[1] - r[0]:4.0f} {r[2] - r[0]:4.0f} |  {v[0]:9.1f} {v[1] - v[0]:4.0f} '
                  f'{v[2] - v[0]:4.0f} | {d1:+5.1f}  {d3:+5.1f}')
    d = np.array(rows[:21])   # 22 is dominated by modulation
    print(f'\npresets 1-21: fundamental error {np.sqrt(np.mean(d[:, 0] ** 2)):.2f} dB rms '
          f'(mean {np.mean(d[:, 0]):+.2f}), 3rd harmonic error {np.sqrt(np.mean(d[:, 1] ** 2)):.2f} dB rms')


if __name__ == '__main__':
    main()
