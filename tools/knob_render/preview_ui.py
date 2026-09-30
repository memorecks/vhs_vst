"""Composites rendered strips over the background at the plug-in's layout (2x) for a quick look.

    Blender -b -P preview_ui.py -- STRIP_DIR BACKGROUND.png OUT.png [prefix]
"""
import bpy, os, sys
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, BG, OUT = argv[:3]
PREFIX = argv[3] if len(argv) > 3 else ""
S = 2.0
KNOB_EXPAND = 1.6
SWITCH_EXPAND = (56.0 / 46.0)


def load(path):
    img = bpy.data.images.load(path)
    img.colorspace_settings.name = "Non-Color"
    w, h = img.size
    a = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1].copy()
    bpy.data.images.remove(img)
    return a


def resize(a, w, h):
    img = bpy.data.images.new("tmp", a.shape[1], a.shape[0], alpha=True, float_buffer=True)
    img.pixels[:] = a[::-1].ravel()
    img.scale(w, h)
    out = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)[::-1].copy()
    bpy.data.images.remove(img)
    return out


bg = resize(load(BG), int(1000 * S), int(500 * S))
bg[..., 3] = 1.0
strips = {n: load(os.path.join(SRC, PREFIX + n + ".png")) for n in ("knob_black", "knob_green", "knob_white", "switch")}


def frame(strip, idx, fh):
    n = strip.shape[0] // fh
    idx = min(idx, n - 1)
    return strip[idx * fh:(idx + 1) * fh]


def paste(tile, cx, cy, w, h):
    t = resize(tile, int(round(w)), int(round(h)))
    x0, y0 = int(round(cx - t.shape[1] / 2)), int(round(cy - t.shape[0] / 2))
    reg = bg[y0:y0 + t.shape[0], x0:x0 + t.shape[1]]
    a = t[..., 3:4]
    reg[..., :3] = t[..., :3] * a + reg[..., :3] * (1 - a)


knobs = [("knob_white", 136, 65, 36), ("knob_white", 952, 69, 36), ("knob_white", 233, 456, 35)]
knobs += [("knob_white", x, 77, 35) for x in (384, 428, 472, 516, 560, 604)]
cols = [319.0, 363.0, 409.0, 451.5, 496.0, 539.5, 583.5, 628.0, 671.5]
rows = [215.0, 274.0, 334.0]
green1 = [True, False, False, True, False, True, False, True, False]
knobs += [("knob_black", c, rows[0], 35) for c in cols]
knobs += [("knob_green" if g else "knob_black", c, rows[1], 35) for c, g in zip(cols, green1)]
knobs += [("knob_black", cols[i], rows[2], 35) for i in (0, 2, 3, 4, 5, 7, 8)]
for k, (name, cx, cy, d) in enumerate(knobs):
    st = strips[name]
    fh = st.shape[1]
    paste(frame(st, k % (st.shape[0] // fh), fh), cx * S, cy * S, d * KNOB_EXPAND * S, d * KNOB_EXPAND * S)

sw = [("switch", 299, 63, 48, 22, 0), ("switch", 639, 63, 48, 22, 1), ("switch", 876, 59, 48, 22, 1),
      ("switch", 914, 267, 48, 22, 1), ("switch", 892, 315, 48, 22, 0), ("switch", 859, 363, 48, 22, 1),
      ("switch", 32, 451, 48, 22, 0)]
for name, x, y, w, h, on in sw:
    st = strips[name]
    fh = st.shape[0] // 2
    paste(frame(st, on, fh), (x + w / 2) * S, (y + h / 2) * S, w * SWITCH_EXPAND * S, w * SWITCH_EXPAND * S / 2)

img = bpy.data.images.new("out", bg.shape[1], bg.shape[0], alpha=True, float_buffer=False)
img.colorspace_settings.name = "Non-Color"
img.pixels[:] = np.clip(bg[::-1], 0, 1).ravel()
img.filepath_raw = OUT
img.file_format = "PNG"
img.save()
print("wrote", OUT)
