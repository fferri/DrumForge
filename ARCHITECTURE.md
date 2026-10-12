# DrumForge architecture

How the plugin is built and why it is built that way. For *using* DrumForge see
[README.md](README.md); for *changing* it, including the golden rules, see
[AGENTS.md](AGENTS.md).

---

## The shape of it

One `AudioProcessor` (`DrumKitProcessor`) owns eight `DrumTrack`s and a shared reverb. Each
track owns **all nine synthesis engines at once** and a pointer to whichever is active.
Everything below the UI is plain C++ with no allocation on the audio thread.

The plugin is deliberately small in concept: eight tracks, each a triggerable sound with a
handful of parameters, plus a bus. The complexity lives in the engines, and in the decision
to make velocity a first-class modulation source rather than a hard-coded loudness curve.

---

## Source layout

```
Source/
  Core/
    DspUtils.h            RNG, polyBLEP, TPT state-variable filter, DC blocker,
                          wavefolder, the five distortion curves
    Envelopes.h           percussive AD envelope + exponential decay contour
    DrumModel.h           ModelId, ModelInfo, ModelParams, engine base class
    Models/               one file per engine, plus the factory and metadata table
    VelocityModulation.h  the velocity curve and how it moves a parameter
    ModulationState.*     per-parameter depths: saved with the session, not automatable
    KitLibrary.*          kits as data: capture, apply, XML, and the factory + user library
    DrumTrack.h           one track: engine hosting, per-track FX, metering
    Parameters.h/.cpp     APVTS layout, note maps, the factory kit table
    FactoryKits.inc       generated: the kit table, from kits/*.dfkit
    DrumKitProcessor.*    the AudioProcessor: 8 tracks, MIDI, reverb bus, master
    Settings.*            global preferences, kept out of the session
  UI/
    UiState.h             editor-wide state the knobs need on every paint (MOD armed)
    Theme.*               look and feel for everything except the knobs
    VelocityKnob.*        the rotary itself: band, last-hit dot, rim drag, context menu
    ModelKnob.*           velocity knob + live-relabelling caption + value readout
    TrackStrip.*          pad, engine + distortion selectors, knobs, mute/solo, meter
    DrumKitEditor.*       window layout, kit selector, MOD toggle
  PluginEntry.cpp         createPluginFilter()
```

---

## The engines

Each engine is a distinct synthesis technique rather than a preset of a common core. They
all derive from `DrumModel` and share the same four macro parameters, but each interprets
those macros in its own way.

| Engine | Technique |
|---|---|
| **Kick** | Pitch-swept sine plus a filtered noise beater click |
| **Snare** | Three decaying shell modes plus band-limited noise wires |
| **Membrane** | Six modal partials, swept between harmonic and circular-membrane ratios |
| **Metal** | Six band-limited squares at inharmonic ratios, heavily filtered |
| **Clap** | Band-passed noise gated by a retriggering staircase envelope |
| **Cymbal** | Four ring-modulated oscillator pairs plus a stick-attack burst |
| **FM Perc** | Two-operator phase modulation with a decaying index |
| **Clang** | Inharmonic FM, ring-modulated against a third oscillator |
| **Rumble** | Two detuned subs plus a low-passed noise bed, up to twelve seconds |

Two of them exist for a specific reason:

- **Clang** is the inharmonic engine. Its modulator and ring ratios move between √2, π, e
  and the golden ratio — never whole numbers — so its partials cannot align with the
  fundamental at any tuning. The `Clang` macro moves those ratios further from consonance
  and raises the FM index, taking it from a struck bell towards torn metal.
- **Rumble** supplies low end. Two sub oscillators a fraction of a percent apart beat slowly
  against each other, which is what gives long kick tails their movement. Decay reaches
  twelve seconds, so it doubles as a sustained bed. Through `Fold` drive it reads as part of
  the kit's body rather than a separate instrument.

The macro labels are per-engine, and `ModelFactory.h` carries the table that the UI reads to
relabel the knobs live. That table is the single place the UI and the engines agree on what
a macro means.

---

## Signal flow

```
MIDI or pad click
      |
      v
DrumTrack::trigger  ->  active engine renders into a mono buffer
      |
      v
de-click ramp  ->  DC blocker  ->  distortion (drive + curve)
      |
      v
pan + level  ->  stereo mix bus,  plus a post-fader send to the reverb
      |
      v
master gain  ->  soft clipper  ->  output
```

### Retrigger de-clicking

Hitting a track while it is still ringing would otherwise cut the waveform and click. The
track fades its **own** engine output out over 1.5 ms underneath the new hit.

The ordering matters and was got wrong once: the fade is applied **before** the DC blocker,
so every filter downstream stays continuous. Applying it after the blocker instead let the
blocker answer the step with a low-frequency thump — measured at 0.079, five times larger
than the click it was supposed to remove. Before the blocker the same step measures 0.0017.

### Distortion

Five curves (`soft`, `hard`, `fold`, `fuzz`, `crush`) with level compensation, so raising
Drive changes character rather than loudness. The make-up divisors were tuned so the five
land within 0.8–1.1 dB of each other at full Drive; the first attempt was 5–9 dB quieter
than clean. Drive at 0 is a true bypass.

Note that the compensation means **the distortion does not reduce crest factor**. That
matters for the kit design process below: drive is a character control, not a compressor.

### The master bus

A `tanh` soft clipper guarantees the output never exceeds full scale however densely the kit
is played, and the default master level is −5 dB.

**There is deliberately no compressor or limiter on the master.** Reference recordings are
mastered, so their crest factor is not achievable by an instrument that does not master
itself, and chasing it would mean baking a mastering stage into a plugin. See *Kit design*
below for what that implies for the factory kits.

---

## Real-time constraints

- **No allocation or locking on the audio thread.** All nine engines are instantiated per
  track up front, so changing a track's engine just retargets a pointer.
- **UI pad clicks reach the audio thread through a lock-free `AbstractFifo`**, not a lock.
- **Everything the audio thread reads is either a parameter pointer or an atomic.** Depths,
  the note map and the last-hit velocity are all mirrored into atomics.
- **`prepareToPlay` is re-entrant.** Hosts call it repeatedly on buffer-size changes and
  device re-activation. The track detaches its active-engine pointer before replacing the
  engine array, so a second call cannot dereference freed memory.

  This was a real crash: Ableton Live failed with `EXC_BAD_ACCESS` in `DrumTrack::reset()`
  on the *second* `prepareToPlay`, because `activeEngine` still pointed into the array that
  `prepare()` had just replaced. It only reproduced on a second call, which is why it
  survived the first round of testing.

---

## Parameters

`createParameterLayout()` builds the whole APVTS: eleven parameters per track plus the kit
selector, note map, master and four reverb controls. `cacheParameterPointers()` caches the
raw `std::atomic<float>*` for each, so the audio thread never does a string lookup.

Two behaviours worth knowing:

- **Latch-at-trigger.** A new hit adopts the knob positions immediately rather than gliding
  towards them, so a hit sounds like the knob looks. Moves made *during* a ring are still
  smoothed.
- **Two smoothing rates.** Block-rate for engine parameters, per-sample for
  level/pan/drive/send.

---

## Velocity modulation

This is the largest design decision in the project, and parts of it are counterintuitive.

### The curve

Bipolar and symmetric about **velocity 100**, the value most controllers send by default:
127 maps to +1, 100 to 0, 0 to −1. So a nominal hit plays every knob exactly as dialled
whatever the depths are, and depth 0 leaves a parameter bit-for-bit untouched at any
velocity.

Modulation is applied in **normalised parameter space**, not value space. That means a depth
of 0.1 is a tenth of the knob's travel whether the knob is in dB, semitones or a 0..1 macro,
and it is what makes the drawn band symmetric about the value.

Implementing the "nominal hit is bit-identical" promise exactly required an early return
when the bipolar weight is zero. Without it, even a zero offset was pushed through
`convertTo0to1`/`convertFrom0to1`, and that round trip is not exactly lossless — so the knob
was being silently perturbed at velocity 100, which is precisely the case the design claims
is untouched. A test asserting bit-identity caught it.

### What it replaced

Four hard-coded velocity responses used to live inside the engines: a fixed loudness curve
on all nine, `velocityBrightness` on Membrane only, and velocity-folded-into-index on Clang
and FM Perc only. They covered exactly the four macro slots, but only on the engines where
someone had thought to add them.

Velocity is now removed from `DrumModel::trigger` entirely. It enters the system in exactly
one place — as already-modulated parameter values — which is what makes the behaviour
adjustable instead of compiled in.

### Why depths are not parameters

Depth is deliberately **not** an `AudioProcessorParameter`. Automating a knob and automating
velocity already spans the space; depth is only the slope between them, which is the part
that should stay static. Keeping them out also avoids adding 72 entries to the host's
automation view for no gain.

They still have to survive a save, so they live as properties on a `MODULATION` child of the
APVTS's own tree. `copyState()` deep-copies and `replaceState()` preserves extra children, so
the on-disk format is unchanged and a session saved before the feature existed decodes as
"no modulation". The audio thread never touches that tree; every depth is mirrored into an
atomic and re-mirrored after the host replaces our state.

---

## Kits, presets and persistence

### Kits are data

A kit is 8 tracks × 11 parameters plus nine modulation depths each — the complete state
needed to reproduce a sound. `KitData` is that, and `captureKit`/`applyKit` move it in and
out of a processor.

`scripts/kitgen.py` holds the archetypes for twenty of the kits and writes `kits/*.dfkit`;
`scripts/emit_factory_kits.py` compiles every `.dfkit` into `Source/Core/FactoryKits.inc`,
which `Parameters.cpp` includes inside its table. `kits/` is the source of truth and the
`.inc` is generated.

Three things this pipeline enforces:

- **A kit file cannot state a value the plugin would clamp.** `kitgen.py` clamps to the real
  parameter ranges and reports what it clamped. Four kits had tunings past +24 semitones and
  were silently clamped — worse, four tracks in one kit all landed on the same ceiling,
  collapsing a melodic spread onto one note.
- **The emitter rejects an unknown modulation target** rather than dropping it.
- **Engine and curve indices are emitted symbolically** (`mKick`, `dFold`) so the generated
  table still reads like a hand-written one in a diff.

### Built-in kits live in the binary

They are not written to disk on first run. Files on disk would be frozen copies, so improving
a kit in a later release would leave users with a stale version and a name clash, and
"restore the factory kits" would have to exist purely to undo what first run did. Users'
own kits are files, because those are content they may want to back up or share.

The three original kits keep indices 0–2 so a session that saved "kit 1" still recalls
Warehouse.

### Loading a kit replaces everything

`applyKit` used to take a `DepthPolicy` with a `levelOnly` mode that left the player's tone
depths alone. That existed only because the factory table could not carry more than a level
depth. Now that it carries all nine, partially applying a kit would just mean quietly losing
whatever its author dialled in, so the concept is gone: a kit is a complete description.

---

## Note maps

Two maps are supported, and they genuinely conflict — note 39 is a hand clap in General MIDI
and the first pad on a BeatStep Pro. So it is a switch, not a union, and each map ignores the
other's notes.

The map is a **real parameter**, so it is saved with the session and visible to the host. Its
*default* comes from a global preference file, so a one-time hardware choice applies to every
new instance without re-picking it per project.

That combination is not obvious, and it was arrived at the hard way. The first version was
global-only, which meant the plugin's MIDI behaviour depended on a file the host knew nothing
about. The host-load test — which asserted that GM notes 36 and 38 make a sound — then failed
on a machine where the preference had been set to BeatStep, because they correctly make no
sound at all. The plugin was right and the test was wrong, but a test that passes or fails
depending on whose Mac it runs on is not a test.

---

## The UI

### The knobs paint themselves

`DrumForgeLookAndFeel` deliberately does **not** implement `drawRotarySlider`. A LookAndFeel
instance serves every control in the editor, so it cannot hold per-knob state like a
modulation depth. `VelocityKnob` is a `Slider` subclass that draws its own band, dot and
pointer, and handles rim-drag for depth.

### The band and the audio share one implementation

Both the editor's band and the audio path's modulation go through `velocityBand()` and the
same normalised-space maths, so what is drawn cannot drift from what is heard. The band is
coloured by *velocity* rather than by screen position, which is what makes a negative depth
visibly reverse the gradient instead of merely sliding it.

### Layout constraints

The editor allows 1120×660 up to 2400×1500. The knob cells are **width-limited, not
height-limited**, which is why growing the track header to fit the `RND` button cost the
knobs nothing.

### Testing seams

`DrumKitEditor` exposes `refreshFromProcessor()` and `getTrackNoteLabel()` purely so a test
can drive one editor tick and assert what a track header reads. That seam exists because of a
specific bug: the code that refreshes the note label landed in the `TrackStrip` constructor
instead of the timer callback — both begin with the same line — so the label was correct when
the editor opened and frozen thereafter.

---

## The randomiser

`RND` rolls new settings for one machine. It is **constrained, not uniform**, because a
uniform draw over nine parameters mostly produces mud. Each engine carries a sweet-spot range
per macro in a table in `Randomizer.cpp`, taken from the ranges the factory kits actually
live in.

Two things it refuses to touch:

- **The engine.** It reads the current engine and never writes the model parameter. Choosing
  an engine is part of setting a track up; the useful gesture is "vary the machine I have".
- **Level, mute and solo.** Mix decisions, and changing them would undo the balance of the
  kit you are working in.

Tuning draws from musical intervals rather than freely, so a rolled machine cannot beat
against the rest of the kit.

---

## Kit design

The twenty-five factory kits were designed against reference recordings rather than by
ear alone, using `DrumForgeKitLab` and `scripts/kitmetrics.py`.

The most useful finding was that the four lanes are separated by **dynamics, not timbre**:

| Lane | Crest factor | Spectral tilt |
|---|---|---|
| industrial | +9.9 dB | −24.5 dB/decade |
| electronica | +12.3 dB | −19.3 |
| IDM | +16.5 dB | −18.3 |
| ambient | +19.5 dB | −19.0 |

Crest factor orders the lanes monotonically; tilt does not separate them at all. So kit design
varies drive, limiting headroom and density per lane, and leaves timbre free within a lane.

Two things follow that are easy to get wrong:

- **Patterns matter as much as kits.** Crest is dominated by how many hits land per bar, so
  each lane is rendered through a pattern whose density matches its genre. Rendering a sparse
  ambient kit through a four-on-the-floor pattern would push it into the limiter and make it
  measure like an industrial kit.
- **Crest is not a per-kit target.** The lever response was measured rather than assumed:
  +0.4 drive moved crest by 0.1 dB, and track level bought only ~2.5 dB for +9 dB because it
  works through the master limiter. What actually moves crest is how much hits overlap, so a
  short dry kit and a long washy kit in the *same* lane differ more than two different lanes
  do. Crest in a reference track is a property of arrangement and mastering, so it is
  advisory in the reports; the gate is spectral tilt plus level sanity.

---

## Build and packaging

JUCE is **not vendored**. It is cloned as a subdirectory, which is why `JUCE/` is gitignored
and why CI caches it — fetching it takes longer than the build.

### Code signing on macOS

CMake signs the bundle as part of *linking*, but JUCE writes
`Contents/Resources/moduleinfo.json` in a `POST_BUILD` step that runs afterwards. A fresh
build therefore ships a signature that does not seal that file, and macOS reports
`a sealed resource is missing or invalid` — an invalid signature, not merely an ad-hoc one.

The fix is a `POST_BUILD` command registered in `CMakeLists.txt` **after** `juce_add_plugin`.
Custom commands on a target run in the order they were added, so it runs after JUCE's own and
seals whatever is in the bundle by then. It then runs `codesign --verify`, so a regression
fails the build rather than shipping.

This hid for several releases because an **incremental** build re-signs the bundle after
`moduleinfo.json` already exists, so it verified locally while every clean build in CI, and
every download, did not. Deleting the bundle and rebuilding reproduced it immediately. That
is the reason `AGENTS.md` insists on testing the path CI actually runs.

### Why the packages are not notarised

Notarisation needs a paid Apple Developer account. A self-signed certificate does not help:
the build is already a valid signature — `codesign --verify` reports "valid on disk" — and
`spctl -a` still answers `rejected`, because Gatekeeper wants a signature chaining to Apple's
Developer ID rather than merely a valid one. The only way a self-signed certificate could
change that is if users trusted it as a root, which is not a reasonable thing to ask.

So the macOS package ships an install script that clears the quarantine flag instead. Windows
has the equivalent problem in SmartScreen, which a self-signed certificate does not solve
either.

---

## Testing

Four tools, all of which exit non-zero on failure and run in CI. What each one covers:

| Tool | Covers |
|---|---|
| `DrumForgeTests` (427 checks) | Everything headless: engines, level bounds, decay to true silence, sample rates, de-clicking, choke group, the velocity curve and its clamping, depth persistence, kit capture/restore, the randomiser's constraints, note maps, XML and filesystem round trips, the full host lifecycle |
| `DrumForgeHostTest` (23 checks) | End-to-end through JUCE's real VST3 hosting code — the same path a DAW takes: bus layout, MIDI, parameter exposure, rendering, editor construction and painting, state save/restore, prepare/release cycles |
| `DrumForgeScreenshot` | Editor layout and painting at four window sizes. The only way to check the band and dot actually draw |
| `DrumForgeRender` | Writes WAVs for listening. Not an assertion, but the quickest way to hear a regression |

### What is not covered

- **The editor's mouse gestures.** Rim drag, MOD drag and the right-click menu have no
  automated coverage; only the maths behind them does.
- **The `DrumForgeScreenshot` output is not compared against a reference image.** It would
  catch more, but image comparison is brittle across macOS versions.

### Two rules that exist because of specific bugs

- **Offline tools pin their settings directory before constructing a processor.** They build
  their note events from General MIDI numbers, so picking up a user's BeatStep preference
  silently ignored most of them and made the output depend on the machine. That invalidated a
  round of kit measurements before it was noticed.
- **Anything reading a user preference must not write to it during tests.** A test that
  silently changed the note map of whoever ran it would be a genuinely nasty bug to chase.

---

## Deliberately not done

- **No samples.** Every sound is synthesised. This is also why the reference recordings are
  used only as statistical targets and never shipped.
- **No master compressor or limiter.** See *The master bus*.
- **No user preset for the global note map beyond the preference file.** It is a session
  parameter whose default comes from a global file; a full preset browser for it would be
  disproportionate.
- **No AU build.** VST3 only, which is a `FORMATS` line away if it is ever wanted.
