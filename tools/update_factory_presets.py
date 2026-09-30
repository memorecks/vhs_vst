#!/usr/bin/env python3
"""Copies factory presets you edited in the plug-in into plugin/Source/Presets.h.

    python3 update_factory_presets.py [--from PRESET_DIR] [--presets Presets.h] [--keep]

In the plug-in, load a factory preset, change it and use "..." > Save (keeping the name). That
writes <name>.vhspreset to the user presets folder and marks the preset with "*". This script puts
the values of every such file into the matching Presets.h entry, then moves the files into an
"Applied to factory presets" subfolder (so they no longer override the new built-in values) unless
--keep is given. Other user presets are left alone. Rebuild the plug-in afterwards.

The first entry ("90s VHS Tape") also supplies the parameter defaults (see createLayout()).
"""
import argparse, os, re, sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
if sys.platform == 'darwin':
    DEFAULT_DIR = os.path.expanduser('~/Library/Application Support/Memorecks/VHS/Presets')
else:
    DEFAULT_DIR = os.path.join(os.environ.get('APPDATA', ''), 'Memorecks', 'VHS', 'Presets')

# Dry/Wet is a mixing control rather than part of a sound, so factory presets leave it alone.
NOT_IN_PRESETS = {'mix'}
APPLIED_DIR = 'Applied to factory presets'

ENTRY = re.compile(r'^    \{ "((?:[^"\\]|\\.)*)", \{ (.*) \}, \d+ \},$')


def fmt(v):
    txt = f'{v:.6g}'
    return txt if re.search(r'[.en]', txt) else txt + '.0'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--from', dest='src', default=DEFAULT_DIR)
    ap.add_argument('--presets', default=os.path.join(HERE, '..', 'plugin', 'Source', 'Presets.h'))
    ap.add_argument('--keep', action='store_true', help='leave the .vhspreset files where they are')
    a = ap.parse_args()

    lines = open(a.presets).read().split('\n')
    factory = {}   # name -> line index
    for i, line in enumerate(lines):
        m = ENTRY.match(line)
        if m:
            factory[m.group(1).replace('\\"', '"')] = i
    if not factory:
        sys.exit(f'{a.presets}: no presets found')

    applied = []
    for fn in sorted(os.listdir(a.src)):
        if not fn.endswith('.vhspreset'):
            continue
        root = ET.parse(os.path.join(a.src, fn)).getroot()
        name = root.get('name', fn[:-len('.vhspreset')]).strip()
        if name not in factory:
            continue
        if root.get('noiseDb') != '1':
            print(f'skipped "{name}": saved by an old version (noise levels not in dB), re-save it in the plug-in')
            continue
        items = [(p.get('id'), float(p.get('value'))) for p in root.iter('PARAM') if p.get('id') not in NOT_IN_PRESETS]
        body = ', '.join(f'{{ "{pid}", {fmt(v)}f }}' for pid, v in items)
        esc = name.replace('"', '\\"')
        lines[factory[name]] = f'    {{ "{esc}", {{ {body} }}, {len(items)} }},'
        applied.append(fn)
        print(f'updated "{name}"')

    if not applied:
        print(f'no edited factory presets in {a.src}')
        return
    open(a.presets, 'w').write('\n'.join(lines))
    if not a.keep:
        dest = os.path.join(a.src, APPLIED_DIR)
        os.makedirs(dest, exist_ok=True)
        for fn in applied:
            os.replace(os.path.join(a.src, fn), os.path.join(dest, fn))
        print(f'moved {len(applied)} file(s) to {dest}')
    print('Rebuild the plug-in to use the new factory presets.')


if __name__ == '__main__':
    main()
