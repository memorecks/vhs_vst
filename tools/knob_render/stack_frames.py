"""Stacks a folder of individually rendered knob frames (0001.png, 0002.png, ...) into a filmstrip.

    python3 stack_frames.py FRAME_DIR OUT.png [--size 200]

Needs Pillow and numpy. Every frame gets the same square crop, centred on the knob's solid disc
in the first frame and sized so the disc spans KNOB_FILL of the frame (matching the plug-in's
knobExpand framing, with a little room for the shadow), then is resized to SIZE px and stacked
top to bottom.
"""
import os, sys
import numpy as np
from PIL import Image

KNOB_FILL = 0.615

args = sys.argv[1:]
size = 200
if "--size" in args:
    i = args.index("--size")
    size = int(args[i + 1])
    del args[i:i + 2]
src, out = args

names = sorted(f for f in os.listdir(src) if f.lower().endswith(".png"))
frames = [Image.open(os.path.join(src, f)).convert("RGBA") for f in names]

alpha = np.asarray(frames[0])[..., 3]
ys, xs = np.where(alpha >= 250)
cx, cy = (xs.min() + xs.max() + 1) / 2, (ys.min() + ys.max() + 1) / 2
half = (xs.max() - xs.min() + 1) / KNOB_FILL / 2
box = (cx - half, cy - half, cx + half, cy + half)

strip = Image.new("RGBA", (size, size * len(frames)))
for i, f in enumerate(frames):
    strip.paste(f.resize((size, size), Image.LANCZOS, box=box), (0, i * size))
strip.save(out, optimize=True)
print(f"{out}: {len(frames)} frames of {size}px (crop {box[2] - box[0]:.0f}px around ({cx:.0f}, {cy:.0f}))")
