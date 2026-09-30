# VHS — Audio Degradation Suite (VST3 / AU port)

A plug-in port of **VHS 1.12 by Memorecks**, reconstructed from the Reaktor 6 ensemble
`resources/VHS_1.12.ens`. The DSP was recovered by decoding the ensemble's binary format
(Primary structure, all 58 Core cells, samples, mic tables and snapshots) — see
`tools/ens_decoder/README.md` for the format notes.

```
plugin/                JUCE 7.0.12 project (CMake)
  Source/DSP.h           building blocks (each maps to a Reaktor module / Core macro)
  Source/VHSEngine.*     the signal chain
  Source/NoiseEngine.*   hum / hiss / crackle loop players + white noise
  Source/Plugin*.cpp     parameters, presets, UI
  Test/TestMain.cpp      offline test harness
  assets/                extracted from the .ens (background, samples, mic IRs) + label font (OFL)
  packaging/             macOS installer layout, Standalone entitlements, Windows Inno Setup script
tools/ens_decoder/     Python decoder + extract_assets.py (regenerates plugin/assets)
tools/update_factory_presets.py  copies factory presets edited in the plug-in into Presets.h
tools/knob_render/     Blender scripts that render the knob/switch filmstrips in plugin/assets/controls
.github/workflows/     CI: builds Windows VST3 and macOS VST3/AU/Standalone, runs the offline tests, builds installers
```

## Building

**macOS** (universal arm64 + x86_64, VST3 + AU + Standalone):

```
cd plugin && ./build_mac.sh            # add --install to copy into ~/Library/Audio/Plug-Ins
```

**Windows** (x64 VST3, Visual Studio 2022 + CMake 3.22+):

```
cd plugin && build_windows.bat
```

JUCE 7.0.12 is vendored in `plugin/third_party/JUCE`; if that folder is missing, CMake fetches
it. (7.0.12 rather than the installed 7.0.5 because 7.0.5 does not build against the macOS 15 SDK
that ships with Xcode 16.) The offline tests are built with `-DVHS_BUILD_TESTS=ON`.

## Installers

```
cd plugin && ./package_mac.sh          # dist/VHS-<version>-mac.pkg
cd plugin && package_windows.bat       # dist\VHS-<version>-windows.exe (needs Inno Setup 6)
```

CI builds both installers and uploads them as workflow artifacts (the macOS one unsigned).

The macOS installer puts the VST3 and AU in `/Library/Audio/Plug-Ins` (all users). The Standalone
app goes in `/Applications` and is optional. The Windows installer puts the VST3 in
`C:\Program Files\Common Files\VST3` and adds an uninstaller.

**macOS signing.** Plain builds are only ad-hoc signed ("code object is not signed at all --
Replacing invalid signature with ad-hoc signature" is JUCE doing this after each VST3 build).
That is enough on the machine that built them. A copy downloaded by someone else is quarantined,
and Gatekeeper then blocks it. Distribute the `.pkg` rather than zipped bundles: files placed by
Installer are not quarantined, so only the installer itself has to get past Gatekeeper.

* Unsigned (default): users get "Apple could not verify VHS-….pkg". They open it anyway via
  System Settings > Privacy & Security > Open Anyway (or right-click > Open on macOS 14 and older).
* Signed and notarized: needs a paid Apple Developer Program membership and two certificates,
  *Developer ID Application* and *Developer ID Installer*. An *Apple Development* certificate
  is not enough. Then:

  ```
  xcrun notarytool store-credentials vhs-notary --apple-id <apple id> --team-id <TEAMID>   # once
  DEV_ID_APP="Developer ID Application: <Name> (<TEAMID>)" \
  DEV_ID_INSTALLER="Developer ID Installer: <Name> (<TEAMID>)" \
  NOTARY_PROFILE=vhs-notary ./package_mac.sh
  ```

  This signs the bundles with the hardened runtime (the Standalone gets the audio-input
  entitlement), signs the installer, notarizes and staples it. The result opens without warnings.

**Windows signing.** Unsigned VST3s load fine in every host. The unsigned installer shows a
SmartScreen "Windows protected your PC" prompt (More info > Run anyway) until it is signed with
an Authenticode certificate.

## Signal chain (as wired in the ensemble)

```
In -> Input (dB) -> [+Noise Pre] -> Chorus -> Mic Emulation -> Preamp -> Wear -> Comp
   -> 2-Band Saturator -> HiCut -> Wow & Flutter (Vintape) -> [+Noise Post]
   -> Wow & Flutter (Tape Mate) -> Level / Mono -> Dry/Wet -> On/Off (processed or dry input) -> Out
```

| Section | What the ensemble does |
|---|---|
| Noise | Five Sampler Loop modules (Hum1 = VHS hum, Hum2/Hum3/Hiss = "VHS Noise M1/M2/M3", Crackle = tape tracking/hiss recording), plus a ±A/2 noise oscillator. All six levels are -inf..+12 dB knobs, where +12 dB equals the original Reaktor maximum (6 for the loops, A = 0.05 for Noise); factory presets and older saved states are converted from the original linear values on load. Pre/Post switches choose the insert point. |
| Chorus | NI "Chorus Stereo" core cell: rate 0.05·100^Speed Hz, base delay 50^Delay ms, sweep (0.5+1.5·Chorus) ms / rate, stereo triangle LFO with L/R phase offset Width/2, 4-point interpolated delay, parabolic dry/wet crossfade. |
| Mic Emulation | 40 microphone models, each a 512-tap convolution with a table stored in the ensemble (44.1 kHz taps used as-is at every rate, as in Reaktor); knob = dry/wet crossfade. |
| Preamp | TONE: low shelf −Tone dB and high shelf +Tone dB at pitch 80 (831 Hz). Drive (0…20 dB): "HQ Saturator" core cell (4× oversampled, Catmull-Rom interpolation, polynomial x − 0.18963x³ + 0.0161817x⁵ clipped at ±1.875), input gain = Drive dB, output gain = **+**0.45·Drive dB (calibrated, see below). |
| Wear | Slew limiter, up = down = 5000…100 units/s. |
| Comp | "Magnitude" core cell: peak detector (instant attack, ln2/(0.02·SR) release) → 500 Hz 1-pole → gain 3/(7.94·env + 0.112), crossfaded with dry by Comp (0..0.9). |
| 2-Band Saturator | Linkwitz-Riley 4 crossover at P2F(Split); each band through two Saturator 2 stages (smooth 7th-order curve, unity small-signal gain, ±2 levels; the band "Sat" knobs drive level asymmetry), +5.3 dB make-up on a band whose Sat is above 0 (calibrated), band levels in dB; Saturate switch bypasses. |
| HiCut | Multi/LP 4-Pole (LP4, no resonance), cutoff pitch 160 → 20. |
| Wow & Flutter (Vintape) | Delay = 20 ms + Warp·sin(RPM/60 Hz) + Flutter·pulse(Hz, width = Shape), LFOs at control rate, 10 ms audio smoother, 50 ms delay line. |
| Wow & Flutter (Tape Mate) | Two cascaded NI "Tape-ish Delay Stereo" cells (the first modified by the original author): delay time (0.01 + m)² s where m = Flutter2 · LP4(random steps at P2F(fRate)) · 0.005 (depth trim — see `kFlutter2Scale`; taken literally the decoded wiring gives thousands of cents of pitch swing, unlike the subtle original); Wow adds band-limited noise (SVF lowpass at wRate Hz) to the delay time; 1-pole low/high cuts (LoCut 15 + 9984·x⁴ Hz up to 1.3 kHz, HiCut 15 + 17985·x² Hz, pitch-clamped to ≥ 4.9 kHz inside the cell), tanh saturation (level 2.5, calibrated; the ensemble says 1.5), wet gain vol² (1.125²). The second cell adds a fixed 2.5 ms pass with its own filters. |
| Output | Level (dB), Mono collapses both channels to the centre, Dry/Wet (new, not in the original) blends the processed signal with the latency-aligned dry input, On/Off relay selects processed or dry input. |

All 22 snapshots stored in the ensemble ("90s VHS Tape", "Vaporwave", "Tracking Problems",
"Be Kind; Rewind", …) are available as factory presets. The default state equals "90s VHS Tape".
The "..." button next to the preset selector saves the current settings (Save / Save As), and
renames or deletes user presets. User presets are `.vhspreset` XML files in
`~/Library/Application Support/Memorecks/VHS/Presets` (macOS) or `%APPDATA%\Memorecks\VHS\Presets`
(Windows). They appear after the factory presets in the host's program list. Saving under a
factory preset's name stores your version in its place, marked with `*`. "Restore Factory
Settings" brings back the original.

To change the built-in presets, edit and save them in the plug-in as above, then run
`python3 tools/update_factory_presets.py`. It writes them into `plugin/Source/Presets.h`, then
moves the `.vhspreset` files into an "Applied to factory presets" subfolder so they stop
overriding the new built-in presets. Rebuild afterwards. The first preset ("90s VHS Tape") also sets the
parameter defaults: the sound of a new instance, and what a double-clicked knob resets to.

## What was inferred rather than read directly

The structure, constants, knob ranges and presets come straight from the file. Behaviour of
closed-source Reaktor Primary modules had to be reproduced from NI's module reference:

* Shelving EQ slope (implemented as 2nd-order RBJ shelves), Stereo Mixer pan law (linear) used
  for Mono, Saturator 2's exact curve (parabolic, levels ±2, knee/asymmetry per the module help),
  Single Delay interpolation (4-point), and the 400 Hz default control rate for LFO/event paths.
* Sampler root key: samples play at their original speed (Pitch knobs at 0, as in every snapshot).
* The Magnitude compressor's detector follows the channel with the *smaller* magnitude — that is
  how the original is wired, so it is kept.
* Switch states that are not part of snapshots use the values saved in the ensemble:
  Chorus off, Mic off (model Reslo VMC2), Noise Pre on / Post off, both Tape-ish delays on.

## Gain staging calibrated against the original

`resources/test_tones` holds a 440 Hz / −12 dBFS sine and the same tone recorded through all 22
snapshots in Reaktor (48 kHz). Built literally from the decoded structure, the port came out 2–11 dB
quieter on most snapshots and had ~10–15 dB more 3rd harmonic on clean ones. These changes, each
tested against all the captures (`python3 tools/compare_reaktor.py`), bring the fundamental to
~1.5 dB rms of the original (was 5.8 dB):

| Change | Why |
|---|---|
| Saturator 2 curve: smooth 7th-order polynomial instead of parabolic | NI documents only 3rd/5th/7th harmonics at defaults; the parabolic curve over-distorted small signals |
| +5.3 dB on a saturator band whose Sat > 0 (faded in over Sat 0…0.05) | every snapshot with Sat > 0 on the band carrying the tone was ~2× louder in Reaktor, almost independent of the Sat amount |
| Drive output +0.45·Drive dB (was −0.45·Drive) | Drive snapshots were 7–10 dB louder *and* more distorted in Reaktor; the module on that path behaves like \|x\|, not an inverter |
| Tape-ish tanh level 2.5 (was 1.5) | the tape stage produced most of the excess 3rd harmonic on clean snapshots |
| +1.5 dB output trim | clean snapshots remained ~1.5 dB low |
| Tape HiCut knob law x² (was x⁴) | read from the "scale" core cell (LoCut is x⁴) |
| Mic IRs not resampled | Reaktor plays the 44.1 kHz taps at any rate; measured closer at 48 kHz |

What the captures cannot pin down: every Mic-on snapshot uses Mix = 1.0, so the Mic Mix law at
intermediate settings is unverified (still a linear crossfade), and Drive is only isolated by one
snapshot ("Alphamax").

## Differences from the Reaktor version

* Reports its constant ~22.6 ms latency (the tape delays) so hosts can compensate; the dry path
  used when switched off is delayed to match.
* Mic impulse responses use a zero-latency FFT convolver (~1–4 % CPU with everything on).
* The chorus LFO runs per sample instead of at control rate; gains are smoothed to avoid zipper noise.
* Resizable vector UI over the original cassette artwork, preset browser, input/output meters.

## Tests

`VHSTest` (offline): renders every preset at 44.1/48/96 kHz with and without Chorus+Mic, checks for
NaN/instability, verifies all 40 mic models, measures latency against the reported value, checks
bypass alignment, fuzzes automation, and measures CPU. The VST3 and AU pass pluginval at
strictness 10, and the AU passes `auval`.
