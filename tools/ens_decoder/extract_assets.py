#!/usr/bin/env python3
"""Regenerates the VHS plug-in assets directly from VHS_1.12.ens.

    python3 extract_assets.py [--out ../../plugin/assets] [--presets OUT.h]

Produces:
  background.png        instrument panel bitmap (1000x500 BGRA, stored bottom-up in the ensemble)
  hum1/hum2/hum3/hiss/crackle.flac
                        the five Sampler Loop samples, trimmed to their loop length and
                        peak-normalised (16-bit); original peak levels are printed/used in code
  mic_irs.bin           40 x 512 float32 impulse responses (Reaktor 6 table list, first 512 taps
                        are what the Core "+512units" convolution reads)
  Presets.h             the original snapshot bank as a C++ table, only with --presets. The plug-in's
                        Source/Presets.h has since been edited (tools/update_factory_presets.py), so
                        writing over it discards those changes.

Requires: numpy, soundfile, pillow.
"""
import argparse, os, re, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from prim import D, fulltree              # noqa: E402  (parses the Primary structure)
from rk import parse                      # noqa: E402
from knobs import knobinfo, lastlabel     # noqa: E402


def extract_background(out):
    from PIL import Image, ImageOps
    i = D.find(b'\xe8\x03\xf4\x01\x20\x00', 0x990000)          # 1000 x 500 x 32 bpp
    raw = D[i + 6:i + 6 + 1000 * 500 * 4]
    im = Image.frombytes('RGBA', (1000, 500), raw, 'raw', 'BGRA')
    ImageOps.flip(im).save(os.path.join(out, 'background.png'))
    print('background.png')


SAMPLES = [  # map-file hash, loop length (ms), output name
    ('191100A2D0BEF017716511EF84C15BB61978D140', 9300, 'hum1'),
    ('C014BEF811BA167681398EC8187F312612613713', 7700, 'hum2'),
    ('A11BC711A67DF23C127B790E2DE999589C3', 18100, 'hum3'),
    ('12B18B100A690DD1E3F99048F9110936357151', 17650, 'hiss'),
    ('B91DB16F13E104D418DF2132DEE2DDACE69B80', 141100, 'crackle'),
]


def extract_samples(out):
    import soundfile as sf
    for m in re.finditer(b'NIMapFile', D):
        i = m.start()
        j = min(x for x in (D.find(b'/Users', i, i + 300), D.find(b'/Volumes', i, i + 300)) if x > 0)
        h = re.search(rb'([0-9A-F]{30,40})', D[i:j]).group(1).decode()
        plen = struct.unpack_from('<I', D, j - 4)[0]
        k = j + plen
        size, frames, ch, sr, _, bits, fmt = struct.unpack_from('<7I', D, k)
        data = np.frombuffer(D, dtype='<f4', count=frames * ch, offset=k + 28).reshape(-1, ch)
        for hh, loop_ms, name in SAMPLES:
            if hh == h:
                n = min(len(data), int(round(loop_ms / 1000 * sr)))
                x = data[:n]
                pk = float(np.abs(x).max())
                sf.write(os.path.join(out, name + '.flac'), x / pk, sr, subtype='PCM_16', format='FLAC')
                print(f'{name}.flac  {sr} Hz  {ch} ch  {n} frames  original peak {pk!r}')


def extract_mic_irs(out):
    i = D.find(b'RktX')                                         # raw Core-format table list
    root = D.find(b'\xba\x49\x93\x02', i)
    tag, ln = struct.unpack_from('<II', D, root)
    tables = []

    def grab(n):
        for tg, v in n:
            if isinstance(v, list):
                grab(v)
            elif tg == 0x03976ca1:
                tables.append(np.frombuffer(v, dtype='<f4'))
    grab(parse(D, root, root + 8 + ln))
    assert len(tables) == 40, len(tables)
    blob = b''.join(np.asarray(t[:512], dtype='<f4').tobytes() for t in tables)
    open(os.path.join(out, 'mic_irs.bin'), 'wb').write(blob)
    print('mic_irs.bin  40 x 512')


# snapshot id (module record 0x27xx - 0x2710) -> plug-in parameter id
SNAP_TO_PARAM = {7: 'rpm', 8: 'warp', 9: 'hz', 10: 'shape', 11: 'flutter', 26: 'on', 57: 'hum1', 62: 'hum2', 69: 'hum3',
                 76: 'hiss', 83: 'crackle', 145: 'noise', 90: 'hiCut', 94: 'tone', 95: 'drive', 99: 'wRate', 100: 'wow',
                 102: 'tapeHiCut', 103: 'tapeLoCut', 104: 'flutter2', 105: 'fRate', 106: 'level', 122: 'highs',
                 123: 'lows', 124: 'loSat', 127: 'saturate', 128: 'split', 132: 'hiSat', 135: 'comp', 136: 'input',
                 147: 'wear', 149: 'micOn', 150: 'micMix', 163: 'chorusSpeed', 164: 'chorusDelay',
                 165: 'chorusDepth', 166: 'chorusWidth', 168: 'mono'}
BOOLS = {'on', 'saturate', 'micOn', 'mono'}
# Switches Reaktor excludes from the snapshot bank (Noise A/B insert switches 140-143, Chorus list 161).
# Reaktor leaves them untouched on snapshot recall; we pin them to the ensemble's saved state instead so
# every preset recalls a complete, predictable sound.
UNSNAPPED = {'noisePre': 1.0, 'noisePost': 0.0, 'chorusOn': 0.0, 'micModel': 32.0}
# Noise levels are stored in dB like the plug-in parameters: +12 dB is the Reaktor maximum.
NOISE_MAX = {'hum1': 6.0, 'hum2': 6.0, 'hum3': 6.0, 'hiss': 6.0, 'crackle': 6.0, 'noise': 0.05}


def extract_presets(path):
    top, mods = fulltree()
    snap = {}
    for m in mods:
        if m.type in (0x14, 0x16, 0x17, 0x18):
            for off in (0xc6, 0xca, 0xc2, 0xce):
                v = struct.unpack_from('<I', m.body, off)[0]
                if 0x2700 <= v < 0x2900:
                    snap.setdefault(v - 0x2710, m)
                    break
    # snapshot names follow the "DSIN" chunk headers at the end of the file
    names = []
    for mm in re.finditer(rb'([\x05-\x50])\x00\x00\x00([\x20-\x7e]{5,80})', D[0x5838a00:]):
        n = mm.group(1)[0]
        s = mm.group(2)[:n]
        if len(s) == n and not re.search(rb'DSIN|RTKR|hsin', s):
            names.append((mm.start() + 0x5838a00, s.decode()))
    presets = []
    for k, (off, name) in enumerate(names):
        end = names[k + 1][0] if k + 1 < len(names) else len(D)
        vals = {}
        for r in re.finditer(rb'(.)\x00\x00\x00\x28\x00\x00\x00\x09\x00\x00\x00\x02\x00\x00\x00\x02\x00\x00\x00', D[off:end], re.S):
            vals[r.group(1)[0]] = struct.unpack_from('<f', D, off + r.start() + 8 + 32)[0]
        presets.append((name, vals))
    presets = presets[1:]   # the first entry is the instrument name, not a snapshot

    lines = ['// Factory presets decoded from the snapshot bank stored in VHS_1.12.ens.',
             '// Generated by tools/ens_decoder/extract_assets.py - values are in parameter units (noise levels in dB).',
             '#pragma once', '', 'namespace vhs', '{',
             'struct PresetValue { const char* id; float value; };',
             'struct FactoryPreset { const char* name; PresetValue values[48]; int count; };', '',
             'static const FactoryPreset kFactoryPresets[] =', '{']
    for name, vals in presets:
        items = []
        for sid, pid in SNAP_TO_PARAM.items():
            if sid not in vals or sid not in snap:
                continue
            m = snap[sid]
            if m.type == 0x14:
                mn, mx = knobinfo(m)[:2]
                v = mn + vals[sid] * (mx - mn)
            else:
                v = 1.0 if vals[sid] > 0.25 else 0.0   # buttons: 0.5 = on
            if pid in BOOLS:
                v = 1.0 if vals[sid] > 0.25 else 0.0
            if pid in NOISE_MAX:
                v = -60.0 if v <= 0 else max(-60.0, min(12.0, 20 * np.log10(v / NOISE_MAX[pid]) + 12.0))
            txt = f'{v:.6g}'
            if not re.search(r'[.e]', txt):
                txt += '.0'
            items.append(f'{{ "{pid}", {txt}f }}')
        for pid, v in UNSNAPPED.items():
            items.append(f'{{ "{pid}", {v:.1f}f }}')
        esc = name.replace('"', '\\"')
        lines.append(f'    {{ "{esc}", {{ {", ".join(items)} }}, {len(items)} }},')
    lines += ['};', '', 'static constexpr int kNumFactoryPresets = (int) (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));',
              '} // namespace vhs', '']
    open(path, 'w').write('\n'.join(lines))
    print(f'{path}: {len(presets)} presets')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(HERE, '..', '..', 'plugin', 'assets'))
    ap.add_argument('--presets', help='also write the original snapshot bank to this file')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    extract_background(a.out)
    extract_samples(a.out)
    extract_mic_irs(a.out)
    if a.presets:
        extract_presets(a.presets)
