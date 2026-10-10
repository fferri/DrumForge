# DrumForge

An 8-track drum and percussion **VST3 instrument** built with [JUCE](https://juce.com).

Every one of the eight tracks can load any of **seven synthesis engines**, so the same
instrument covers an 808-style kit, an acoustic-style kit, hand percussion, FM blocks and
bells, and everything in between. Tracks are triggered over MIDI using the General MIDI
drum map, and each track has its own drive, pan, level and reverb send.

![DrumForge](docs/screenshot.png)

---

## The seven engines

Each engine is a distinct synthesis technique, not a preset of the others.

| # | Engine | Technique | Typical use |
|---|--------|-----------|-------------|
| 1 | **Kick** | Pitch-swept sine + filtered noise beater click | Kicks, 808 bass drums |
| 2 | **Snare** | Three decaying shell modes + band-limited noise wires | Snares, rimshots |
| 3 | **Membrane** | Six modal partials (harmonic ⟷ circular-membrane ratios) | Toms, congas, woodblocks |
| 4 | **Metal** | Six band-limited squares at inharmonic ratios, heavily filtered | Closed/open hats, shakers |
| 5 | **Clap** | Band-passed noise gated by a retriggering staircase envelope | Claps, snaps |
| 6 | **Cymbal** | Four ring-modulated oscillator pairs + stick-attack burst | Crashes, rides |
| 7 | **FM Perc** | Two-operator phase modulation with a decaying index | Cowbells, claves, bells |

Every engine exposes the same four macro knobs, but each one **means something specific to
that engine** — the UI relabels them live:

| Engine | Decay | Tone | Character | Sweep |
|--------|-------|------|-----------|-------|
| Kick | body decay | beater click | punch (sweep speed) | sweep depth |
| Snare | body decay | noise colour | snap | shell bend |
| Membrane | decay | bright | material (tonal ⟷ inharmonic) | bend |
| Metal | decay | filter | metallic ⟷ noise | bend |
| Clap | tail | tone | spread | burst count |
| Cymbal | decay | filter | complexity | snap |
| FM Perc | decay | tone | ratio | index |

Plus five per-track controls common to all engines: **Tune** (±24 semitones),
**Level**, **Pan**, **Drive** (per-track saturation) and **Send** (to the shared reverb).

There are no samples anywhere in the plugin — everything is generated in real time.

---

## Track layout and MIDI

The default kit follows the General MIDI percussion map:

| Track | Primary note | Default engine | GM instrument |
|-------|--------------|----------------|---------------|
| 1 | 36 (C1) | Kick | Bass drum |
| 2 | 38 (D1) | Snare | Acoustic snare |
| 3 | 42 (F#1) | Metal | Closed hi-hat |
| 4 | 46 (A#1) | Metal | Open hi-hat |
| 5 | 39 (D#1) | Clap | Hand clap |
| 6 | 45 (A1) | Membrane | Low tom |
| 7 | 49 (C#2) | Cymbal | Crash cymbal |
| 8 | 51 (D#2) | Cymbal | Ride cymbal |

Common aliases fold onto the same tracks (35 → kick, 40 → snare, 44 → closed hat,
41/43/47/48/50 → toms, 52/55/57 → cymbals, 53/59 → ride), so GM drum files play back
correctly. Unmapped notes are ignored.

**The closed and pedal hi-hats choke the open hi-hat**, exactly as in General MIDI.

You can also play any track from the UI: click its pad, and the click's vertical position
sets the velocity (soft at the top, hard at the bottom).

---

## Architecture

```
Source/
  Core/
    DspUtils.h            RNG, polyBLEP, TPT state-variable filter, one-pole, DC blocker
    Envelopes.h           percussive AD envelope + exponential decay contour
    DrumModel.h           ModelId, ModelInfo, ModelParams, engine base class
    Models/               one file per engine, plus the factory and metadata table
    DrumTrack.h           one track: engine hosting, per-track FX, metering
    Parameters.h/.cpp     APVTS layout, GM note map, factory kit defaults
    DrumKitProcessor.*    the AudioProcessor: 8 tracks, MIDI, reverb bus, master
  UI/
    Theme.*               dark drum-machine look and feel
    ModelKnob.*           rotary + live-relabelling caption
    TrackStrip.*          pad, engine selector, knobs, mute/solo, meter
    DrumKitEditor.*       window layout
  PluginEntry.cpp         createPluginFilter()
Tools/
  RenderDemo.cpp          offline WAV renderer
  RenderTests.cpp         offline verification suite (106 checks)
  RenderScreenshot.cpp    editor smoke test / screenshot generator
  HostLoadTest.cpp        loads the built .vst3 through a real VST3 host (22 checks)
```

Design points worth knowing:

- **No audio-thread allocation or locking.** All seven engines are instantiated per track
  up front, so changing a track's engine just retargets a pointer. UI pad clicks travel to
  the audio thread through a lock-free `AbstractFifo`.
- **Retrigger de-clicking.** Hitting a track while it is still ringing would otherwise cut
  the waveform and click. The track captures its last output sample and cross-fades it out
  over 1.5 ms underneath the new hit.
- **Latch-at-trigger parameters.** A new hit adopts the knob positions immediately instead
  of gliding towards them, while moves made *during* a ring are still smoothed.
- **Block-rate smoothing** for engine parameters and **per-sample smoothing** for
  level/pan/drive/send.
- **Master soft clipper** (`tanh`) guarantees the output never exceeds full scale, however
  densely the kit is played.

---

## Building

Requirements: macOS 11+, Xcode command line tools, CMake 3.22+.

```sh
git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For a universal binary add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

The VST3 bundle lands at `build/DrumForge_artefacts/Release/VST3/DrumForge.vst3`. To
install it for your DAW:

```sh
cp -R build/DrumForge_artefacts/Release/VST3/DrumForge.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

Then rescan plugins in your DAW. DrumForge appears as an instrument named **DrumForge**
under the *Drum* category.

## Using it

Each track strip, top to bottom:

- **Pad** — click to audition. The click's vertical position sets the velocity (soft at
  the top of the pad, hard at the bottom). The pad lights up on MIDI hits too.
- **Engine selector** — swaps the synthesis engine. Changing it loads that engine's own
  default settings, so a track is immediately playable after a swap.
- **Four macro knobs** — `Decay`, `Tone`, `Character`, `Sweep`. Their captions change to
  describe what they do for the loaded engine (see the table above).
- **Per-track controls** — `Tune`, `Level`, `Pan`, `Drive`, `Send`.
- **M / S** — mute and solo.
- **Level meter** — the thin bar between the pad and the engine selector.

The top bar holds the global `Master` level and the shared reverb (`Size`, `Damp`, `Mix`,
`Width`). `Send` on each track feeds that reverb bus post-fader.

Double-click any knob to return it to its default.

---

## Verifying

```sh
cmake --build build
./build/DrumForgeTests_artefacts/Release/DrumForgeTests              # 106 checks
./build/DrumForgeHostTest_artefacts/Release/DrumForgeHostTest.app/Contents/MacOS/DrumForgeHostTest   # 22 checks
./build/DrumForgeScreenshot_artefacts/Release/DrumForgeScreenshot.app/Contents/MacOS/DrumForgeScreenshot docs/screenshot.png
./build/DrumForgeRender_artefacts/Release/DrumForgeRender renders    # writes WAVs you can listen to
```

**`DrumForgeTests`** drives the processor headlessly and checks that every engine produces
audible output, stays finite, decays to true silence, never exceeds full scale and responds
within 30 ms; that all engines work at 44.1/48/88.2/96 kHz; that retriggering a ringing
track produces no step discontinuity; that the GM note map, the hi-hat choke group, mute,
solo and the post-fader reverb send all behave; that state round-trips exactly; and that
the plugin survives the full host lifecycle - repeated `prepareToPlay`, `releaseResources`
in between, sample-rate and buffer-size changes, and blocks larger or smaller than the
prepared size.

**`DrumForgeHostTest`** is the end-to-end check. It loads the built `.vst3` from disk
through JUCE's real VST3 hosting code — the same path a DAW takes — and verifies the bus
layout (no audio input, stereo output), MIDI input, parameter exposure, audio rendering,
editor construction and painting, state save/restore, and the prepare/release cycles a DAW
performs on buffer-size changes and device re-activation.

**`DrumForgeScreenshot`** instantiates the editor at several window sizes and renders it to
a PNG, which catches layout and painting problems that the headless tests cannot.

**`DrumForgeRender`** writes one isolated hit per engine, one bar per track using the
factory kit, and a four-bar demo pattern (`drumforge_demo.wav`, plus a dry copy).

All four exit non-zero on failure and are safe to run in CI.

---

## Licensing note

JUCE 9 is dual-licensed: **AGPLv3** or a commercial licence. This project links JUCE as a
subdirectory and is therefore distributed under the AGPLv3 unless you hold a commercial
JUCE licence. If you plan to ship DrumForge as a closed-source product, you need the
commercial licence from Raw Material Software.
