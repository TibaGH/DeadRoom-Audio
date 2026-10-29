# DeadRoom Audio

Three audio plugins built with JUCE 9, in one CMake project. Every plugin builds as
VST3, AU (macOS), AAX (macOS/Windows) and a standalone app.

| Plugin   | What it does |
|----------|--------------|
| Roomless | Removes room reverb from recordings made in imperfect rooms |
| Duotone  | Mid/Side Baxandall tone EQ |
| Lantern  | Linear-phase passive mastering EQ modelled on the Manley Massive Passive, with gentle tube warmth |

## Roomless (de-reverb)

Single-microphone late-reverb suppression in the STFT domain. The reverb tail in each
frequency bin is predicted from what was heard about 50 ms earlier, decayed according to
the room's RT60, and removed with a decision-directed Wiener gain. Artifacts are kept
down by the decision-directed estimate, a gain floor, fractional-octave smoothing across
frequency and a slow gain release. All channels share one gain curve, so the stereo image
does not move.

Controls:

- **Room decay**: how long the room rings out (RT60). Set it by ear or use **Learn room**.
- **Learn room**: play a part with pauses (speech is ideal), press again to stop. The
  plugin fits straight lines to free decays in six bands and sets Room decay to the median.
- **Bass decay**: multiplier on the decay below ~250 Hz; rooms usually ring longer there.
- **Strength**: how much of the predicted tail is removed (100 % = model estimate).
- **Max reduction**: no frequency is ever turned down further than this. Lower sounds more natural.
- **Smoothing**: higher values avoid watery or chirpy artifacts, at the cost of a slower response.
- **Mix**, **Output**, **Listen to removed** (hear only what is taken out).

Latency: 2048 samples at 44.1/48 kHz (4096 at 88.2/96 kHz).

## Duotone (Mid/Side Baxandall)

Separate Baxandall bass and treble shelves for the Mid and the Side signal. The shelves are
first-order analog prototypes with the pole at the chosen frequency and a zero that moves
with the gain, so a bigger boost reaches further into the midrange, like a real Baxandall
tone stack. Also: Mid and Side level, a 12 dB/oct low cut on the Side signal (keeps the
low end mono), Link (Side follows Mid) and Listen (Stereo, Mid only, Side only).
Zero latency, stereo only.

## Lantern (passive mastering EQ)

Follows the Massive Passive Mastering version as documented by Manley and UAD:

- 4 overlapping bands, 11 stepped frequencies each:
  - Band 1: 22 Hz to 1 kHz, Band 2: 82 Hz to 3.9 kHz
  - Band 3: 220 Hz to 10 kHz, Band 4: 560 Hz to 27 kHz
- Each band: Bell or Shelf (bands 1-2 shelve low, 3-4 shelve high), Boost/Cut switch,
  stepped gain up to 11 dB (16 steps), stepped bandwidth.
- A wide bell can only reach about 6 dB; the full 11 dB needs a narrow bandwidth.
- Shelves are gentle (first order) when wide, and get steeper with an opposite dip as the
  bandwidth control is turned up.
- Bands work in parallel, like the passive network: two boosts on the same frequency do not
  simply add up.
- High-pass: off / 12 / 16 / 23 / 30 / 39 Hz (18 dB/oct).
  Low-pass: off / 52 kHz (30 dB/oct) / 40 / 27 / 20 / 15 kHz (18 dB/oct).
- Trim: -2.5 to +2.5 dB in 0.5 dB steps.

The EQ curve is turned into a symmetric (linear-phase) FIR of 16384 taps and applied with
partitioned FFT convolution. When a control moves, a background thread designs a new FIR
and the audio crossfades to it over one block. Offline bounces redesign in line.

**Warmth** adds an asymmetric tube-style curve (mostly 2nd harmonic), 4x oversampled with
linear-phase filters. Measured on a 1 kHz sine at -6 dBFS: 0.33 % THD at the default 20 %,
1.2 % at 50 %, 3.8 % at 100 %. 0 % adds no distortion.

Latency: 8765 samples at 48 kHz (about 183 ms). Fine for mastering; not meant for tracking.

What is modelled and what is approximated: frequencies, ranges, filter values and the
bandwidth/gain interaction follow the manuals. The exact shape of the shelf overshoot, the
parallel-band maths and the special voicing of the 22/33 Hz and 16/27 kHz shelves are
approximations, not a circuit simulation.

## Building

Requirements: CMake 3.22+, a C++17 compiler (Xcode on macOS, Visual Studio 2022 on Windows).
JUCE 9.0.4 is downloaded automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On macOS add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for a universal build.
Use `-DJUCE_PATH=/path/to/JUCE` to build against a local JUCE checkout,
`-DDEADROOM_BUILD_AAX=OFF` to skip AAX, and `-DDEADROOM_BUILD_TESTS=ON` for the offline DSP
measurements (`DspTests`).

Built plugins end up in `build/plugins/<Name>/<Name>_artefacts/Release/<Format>/`.
The GitHub Actions workflow builds macOS and Windows and uploads all formats as artifacts.

## Installing

Download the latest build from the **Actions** tab (latest run, *Artifacts*), unzip it and
copy each plugin to the standard folder:

| Format | macOS | Windows |
|--------|-------|---------|
| VST3 | `/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` |
| AU | `/Library/Audio/Plug-Ins/Components/` | n/a |
| AAX | `/Library/Application Support/Avid/Audio/Plug-Ins/` | `C:\Program Files\Common Files\Avid\Audio\Plug-Ins\` |

The macOS builds are not signed or notarized yet, so remove the download quarantine once:

```bash
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Lantern.vst3
```

(repeat for each plugin you copied), then restart or rescan your DAW.

### AAX and Pro Tools

The AAX SDK ships with JUCE, so the AAX versions build without anything extra. Unsigned AAX
plugins only load in Pro Tools Developer. To use them in the regular Pro Tools they must be
signed with PACE's signing tools, which Avid provides for free to registered AAX developers.
Until then, the VST3 versions can be used in Pro Tools through a VST3 host plugin.

### Licensing

JUCE is dual-licensed under AGPLv3 and a commercial JUCE licence. Check
[JUCE's licence terms](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) before distributing these
plugins, especially closed-source or commercially.
