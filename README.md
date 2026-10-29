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

## Duotone (Baxandall mastering EQ)

A two-channel Baxandall EQ for mastering and the mix bus, modelled on the documented
behaviour of the Dangerous Music BAX EQ, working on Left/Right or Mid/Side.

**Faceplate:**

- Per channel: Low cut (off + 7 positions, 12 dB/oct Butterworth), Low shelf frequency
  (8 positions), Low level, High level (both +-5 dB in 0.5 dB steps), High shelf frequency
  (8 positions), High cut (off + 7 positions, 12 dB/oct).
- Engage lamp (off = bypass, also the host's bypass parameter), L/R or M/S, Link
  (channel 2 follows channel 1).

**Curves**, as documented for the BAX EQ: very broad first-order shelves that keep rising
beyond the audio band. The low shelf's listed frequency is its bottom shoulder; the high
shelf's listed frequency is mid-slope, with the set level reached exactly one octave
higher (5 dB at 3.2 kHz for 1.6 kHz). Published positions are used where they are
known (low shelf 54-166 Hz, high shelf 1.6/3.4/4.8/7.1/18 kHz, low cut 12-43 Hz, high cut
18/28/70 kHz); the other positions are estimates and marked as such in `BaxModel.h`.
The hardware is a DC-coupled op-amp design with THD+N below 0.002 %, so Duotone adds no
colouration of its own.

**Plugin-only controls**: Phase (Analog: minimum phase, about 3 ms latency; Linear: about
183 ms latency at 48 kHz) and Hide/Show curve. The window can be resized.

No schematic is public, so the EQ is modelled on its documented response, not
component by component.

## Lantern (passive mastering EQ)

A two-channel passive mastering EQ with a tube make-up amplifier, after the Manley Massive
Passive (Mastering version) as documented by Manley and UAD.

**Faceplate** (every control works like on the hardware):

- Per channel, 4 overlapping bands with 11 stepped frequencies each:
  - Band 1: 22 Hz to 1 kHz, Band 2: 82 Hz to 3.9 kHz
  - Band 3: 220 Hz to 10 kHz, Band 4: 560 Hz to 27 kHz
- Per band: Boost / Out / Cut switch, Shelf / Bell switch (bands 1-2 shelve low, 3-4 high),
  gain up to 11 dB in 16 steps, stepped bandwidth.
- A wide bell only reaches about 6 dB; the full 11 dB needs a narrow bandwidth. Shelves are
  gentle when wide and get steeper with an opposite dip as the bandwidth goes up.
- Bands work in parallel like the passive network: per Manley, four bands boosting the same
  frequency give the gain of one band, not the sum.
- Per channel: IN (EQ in/out; the tube stages stay in circuit, as on the hardware),
  Gain -2.5 to +2.5 dB in 0.5 dB steps, High pass off/12/16/23/30/39 Hz (18 dB/oct),
  Low pass off/52k (30 dB/oct)/40k/27k/20k/15k (18 dB/oct).
- Link: channel 2 follows channel 1. Power: off bypasses the plugin (it is the host's bypass
  parameter too), crossfaded and latency-matched.

**Plugin-only controls** in the strip above the faceplate:

- **Phase**: *Analog* uses a minimum-phase filter, which has the phase behaviour of the
  passive LC circuit (about 3 ms latency). *Linear* uses a linear-phase FIR (about 183 ms
  latency at 48 kHz, for mastering). Switching changes the reported latency.
- **Tube drive**: pushes the tubes and output transformer harder (0 to +12 dB) without
  changing the output level.
- **Hide/Show curve**: hides the EQ curve so only the faceplate is shown.

The window can be resized; the UI scales.

**Tube circuit.** After the EQ, the signal runs through a model of the make-up amplifier
described in Manley's manual: a 12AU7 voltage-gain stage and a 6922 line driver on a 300 V
supply (Koren triode model, solved exactly per stage, including grid current), coupling
capacitors, and an output transformer whose core saturates at low frequencies and high
levels. It runs 4x oversampled. Measured on a 1 kHz sine: 0.06 % THD at -18 dBFS (+4 dBu,
Manley's published figure), 0.23 % at -6 dBFS, 0.46 % at 0 dBFS, mostly 2nd harmonic; at
30 Hz the transformer adds clearly more 3rd harmonic.

What is modelled and what is estimated: frequencies, ranges, filter values, the
bandwidth/gain interaction, the parallel bands, the tube types and the supply voltage follow
the manuals. No schematic was available, so the EQ is modelled by its documented response
(not component by component), and the tube stage resistor values, levels and transformer
constants are estimates calibrated to the published THD figure. It is in the spirit of the
hardware, not a guaranteed match.

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

Download the latest build from the **Actions** tab (latest run, *Artifacts*) and unzip it.
On macOS the download contains `DeadRoom-Audio-macOS.zip`; unzip that one too (double-click
in Finder keeps the plugin bundles intact). Copy each plugin to the standard folder:

| Format | macOS | Windows |
|--------|-------|---------|
| VST3 | `/Library/Audio/Plug-Ins/VST3/` | `C:\Program Files\Common Files\VST3\` |
| AU | `/Library/Audio/Plug-Ins/Components/` | n/a |
| AAX | `/Library/Application Support/Avid/Audio/Plug-Ins/` | `C:\Program Files\Common Files\Avid\Audio\Plug-Ins\` |

The macOS builds are only signed ad hoc, not with an Apple Developer ID, so macOS reports
them as "damaged" after downloading. Remove the download quarantine once per plugin:

```bash
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/Lantern.vst3
```

If a plugin still refuses to load, sign it again locally:

```bash
sudo codesign --force --deep --sign - /Library/Audio/Plug-Ins/VST3/Lantern.vst3
```

Then restart or rescan your DAW.

### AAX and Pro Tools

The AAX SDK ships with JUCE, so the AAX versions build without anything extra. Unsigned AAX
plugins only load in Pro Tools Developer. To use them in the regular Pro Tools they must be
signed with PACE's signing tools, which Avid provides for free to registered AAX developers.
Until then, the VST3 versions can be used in Pro Tools through a VST3 host plugin.

### Licensing

JUCE is dual-licensed under AGPLv3 and a commercial JUCE licence. Check
[JUCE's licence terms](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md) before distributing these
plugins, especially closed-source or commercially.
