# Reaktor 6 `.ens` decoder notes

Python tools used to reverse-engineer `resources/VHS_1.12.ens` for the plug-in port.
Nothing here writes to Reaktor files; everything reads the ensemble.

| Script | Purpose |
|---|---|
| `extract_assets.py` | Regenerates `plugin/assets/*` from the ensemble (`--presets` also dumps the original snapshot bank) |
| `rk.py` | Core tagged-file reader: `atad` chunk stream (zlib or raw) + TLV tree parser |
| `core.py` | Builds Core macro/module objects (modules, port terminals, wires, QuickBus/QuickConst) |
| `cshow.py` | Pretty-prints a Core structure with primitive names |
| `flat.py` | Flattens a Core macro into SSA pseudo-code (library macros kept as black boxes) |
| `ptok.py`, `prim.py` | Primary-level record tokenizer and module-tree builder |
| `pdump.py` | Dumps a Primary macro's children and wires |
| `knobs.py` | Knob decoding (range, step, stored value, label, info text) |
| `enscells.py` | Extracts all Core cells embedded in the ensemble to `cells.pkl` |

`pip install numpy soundfile pillow`. The ensemble path defaults to `../../resources/VHS_1.12.ens`
(override with `VHS_ENS=...`).

## File layout (VHS_1.12.ens, 92 MB)

| Offset | Content |
|---|---|
| `0x000000` | NI container header (`hsin`/`DSIN` chunks), `KEnsemble` record |
| `0x000750` | `RktX` + raw Core-format TLV: **table list** (40 float tables = mic impulse responses) |
| `0x287000` | 58 Core cells (`#NI#CS#Document##NI#Reaktor#Core#TaggedFile#` + `atad` chunks) |
| `0x99d000` | Primary structure: instrument record with its 1000x500 BGRA panel bitmap |
| `0xcea745`… | Sampler Loop modules each followed by an embedded `NIMapFile` + float32 sample data |
| `0x5835d81` | snapshot-able module table (type, id) |
| `0x5838000` | snapshot bank (`DSIN` chunks, one per preset, values normalised 0..1) |

## Primary structure

Records are `[` + u32 len + class name (`KSModul`, `KInPort`, `KOutPort`, `KEnsemble`) + payload + `]`.

* `KSModul`: u32 version, 0x00, u16 0x22f2, 0x00, **u32 type**, **u32 id**, u32, u32 nIn, u32 nOut, …
  Macros (type 4) and the instrument (type 9) store `] 03000000 <u32 childCount>` after their name;
  the children follow inline (depth-first) after the macro's own port records.
* Ports belong to the preceding module. `KOutPort`: u32 1, u32 kind (1 audio, 3 event),
  u32, u32 flag, then u32 count + (childIndex, inputPort) pairs. Indices refer to the position in
  the parent's child list. Macro ports carry a name (flag 4) before the destination list.
* Knob (type 0x14): f64 step @0x3c, f32 min @0x50, f32 max @0x54, normalised value @0x106,
  snapshot id (0x27xx) @0xc6, label + info text near the end of the record.
* Constant (0x64): f32 value @0x38. Buttons store on/off values @0x50/0x54; `0.5` = on.
* Switch/List (0x17, 0x18): selected entry is 1-based @0x106 (`-1` = none).

Type ids used by VHS (confirmed from port layouts, the R5 module reference and the order of
Reaktor's module menu): 4 macro, 5/6 in/out terminal, 0x14 knob, 0x16 button, 0x17 list/switch,
0x18 switch, 0x64 constant, 0x6e add, 0x6f multiply, 0x70 invert, 0x9a relay, 0x9b distributor,
0x9c amp/mixer (dB), 0x9d stereo amp/mixer, 0xcc noise, 0xce random, 0xde sampler loop,
0x104 LFO, 0x133 Multi/LP 4-Pole, 0x13c high shelf, 0x13e low shelf, 0x154 single delay,
0x169 Saturator 2, 0x174 slew limiter, 0x1c4 audio smoother, 0x20c core cell.

## Core cells

Chunk stream: `atad` u32 2, `crng` + `bilz` (zlib) or `enon` (stored), u32 compressed, u32 raw.
The concatenated payload is a TLV tree: u32 tag, u32 len, data; tags with high byte 0x02 are
containers, 0x03 leaves.

* `0x02b24bed` module: `0x03a2c92d` = u32 type + u8 isMacro; `0x02c2592d` position;
  `0x028f2d33` macro body.
* `0x02ce4bed` child groups in order inputs, outputs, internals (`0x03a3392d` = count).
* `0x02cf0cb0` property list: key (i32 id, i32 kind) -> value. `(1,2)` name, `(-2147483639,1)` port index,
  `(1,4)` constant value (f64), `(1,1)` int / compare mode.
* `0x02ceebe3` wires. Source kinds: 0 = (module, port), 1 = QuickBus index or named bus (e.g. `SR.C`),
  3 = inline QuickConst (f64, or int if 16 bytes). Destination kind 1 = QuickBus write.

Primitive type ids: 1 const, 2 add, 3 mul, 4 sub, 5 div, 10 router, 11 merge, 13 compare
(mode 0 `==`,1 `!=`,2 `<=`,3 `<`,4 `>=`,5 `>`), 17 negate, 18 abs, 22 read, 23 write, 30 R/W order,
44 array, 45 read[], 46 write[], 48 log(base), 49 exp(base), 50 table reference,
10000/11000 in/out ports, 12002/13002 cell ports.
