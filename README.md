# Drift

**An experimental spatial MIDI phrase sequencer where movement, proximity, and collisions shape the music.**

Drift explores a sequencer model built around a dynamic ecosystem of musical phrases rather than tracks arranged on a timeline. Short looping phrases exist as objects in a bounded 2D world. The user moves, throws, groups, separates, and collides them to change how the composition evolves.

The proof of concept asks one question:

> Can spatial dynamics produce a musical experience that is compelling to shape, perturb, or observe?

## The idea

The fundamental object in Drift is a **phrase**, not a note. Each phrase has both musical and spatial state:

```text
Musical                         Spatial
────────                        ───────
notes and durations             position
loop length                     velocity
MIDI channel                    radius
current/pending variant         mass
```

Spatial activity changes musical state through explicit, quantized rules:

```text
physical event
    ↓
musical intent
    ↓
quantization
    ↓
phrase state transition
    ↓
MIDI output
```

Physics never emits arbitrary notes directly. A collision may request a pattern change, but that change becomes active on a musical boundary.

## POC interaction vocabulary

- **Drag and throw:** manipulate phrase position and velocity directly.
- **Freeze motion:** pause autonomous movement without interrupting playback, then reposition phrases before resuming.
- **Collision:** request a phrase-variant change at the next bar.
- **Speed:** control how sparse or active a phrase is.
- **Proximity:** increase rhythmic coupling between nearby phrases.

The initial composition contains four synchronized phrases—drums, bass, chords, and melody—with three manually authored variants per role that already work together musically. Variants cycle predictably `A → B → C → A`.

Every collision pair has one explicit target:

| Collision | Phrase advanced |
| --- | --- |
| Bass + chords | Chords |
| Bass + drums | Bass |
| Bass + melody | Bass |
| Chords + drums | Drums |
| Chords + melody | Chords |
| Drums + melody | Melody |

New contacts are resolved in the table's stable pair order. If simultaneous contacts target the same phrase, the first accepted request wins and that phrase keeps one unambiguous pending transition until the next eligible bar.

Phrase speed also selects authored material. Native velocity magnitude is normalized against the maximum throw speed and smoothed with a deterministic 250 ms filter. Each phrase begins normal (`A`), slows into sparse (`C`), or accelerates into active (`B`). Hysteresis keeps the bands stable: normal enters sparse at `0.015` and sparse returns at `0.040`; normal enters active at `0.450` and active returns at `0.300`. Band changes take effect at the next eligible bar. Speed observation pauses during Freeze Motion and direct dragging, while audio playback continues. A collision targeting the same phrase and bar replaces a pending speed change.

Proximity currently has two deliberately temporary audition modes. Both derive a loose, linked, or tight coupling level from the smoothed surface gap between every phrase pair, with stable changes taking effect at the next eligible unscheduled bar. **Rhythm profiles** snap nearby phrases toward half-beat or beat grids without changing their notes, durations, or dynamics. **Shared accents** leave timing and density untouched and emphasize notes that already begin together. The selector changes modes on the next bar so the alternatives can be compared during uninterrupted playback; neither is yet designated as the production mapping.

## Architecture

Drift is designed as a native **JUCE** desktop application with an embedded **React** interface. **PixiJS** is the initial renderer for the spatial world.

- JUCE owns transport, scheduling, quantization, MIDI, phrase state, physics, collisions, proximity, and application lifecycle.
- React owns presentation, controls, pointer gestures, animation, and debug visualization.
- The UI sends semantic commands and renders authoritative engine snapshots.
- WebView timing never participates in musical scheduling.

```text
React + PixiJS
      │
      │ commands / snapshots
      ▼
  JUCE engine
      │
      │ timestamped MIDI
      ▼
external synth or DAW
```

## Scope

The POC focuses on 2D interaction, reliable MIDI output, four phrase objects, and one intentionally constrained composition.

It is not currently intended to provide audio synthesis, recording, a piano roll, an arrangement timeline, plugin hosting, project persistence, AI generation, or a production-ready DAW workflow.

## Project status

Drift is in the design and initial implementation phase. The first development batch is organized as vertical slices:

1. [Launch a packaged Drift shell with embedded React UI](https://github.com/datashaman/drift/issues/1)
2. [Play one looping phrase through the native transport](https://github.com/datashaman/drift/issues/2)
3. [Route playback to a selectable MIDI output safely](https://github.com/datashaman/drift/issues/3)
4. [Reject invalid bridge commands and recover from UI reload](https://github.com/datashaman/drift/issues/4)
5. [Prove transport stability under simulated UI stress](https://github.com/datashaman/drift/issues/5)
6. [Validate ten-minute playback through a real MIDI device](https://github.com/datashaman/drift/issues/6)

The native shell now connects React Play, Stop, Freeze Motion, BPM, MIDI-output, proximity-audition, and direct phrase-drag controls to a native-owned monotonic transport and a four-role composition: drums, bass, chords, and melody. Stop freezes both playback and movement; Freeze Motion pauses only autonomous movement so phrases can be caught and repositioned while MIDI continues uninterrupted. Untouched phrases retain their velocity for resume, while a phrase manipulated during the freeze is released stationary. Drift schedules every phrase from the same transport to its assigned MIDI channel and advances the phrase bodies in a bounded normalized world at a fixed 120 Hz. Validated pointer intent crosses a bounded, coalescing command queue; PixiJS follows the pointer optimistically and reconciles to latest-state native snapshots at 30 Hz. A bounded recent pointer sample window gives each release a native-owned throw velocity, while stationary releases settle and edge or corner throws reflect inside the field. Every phrase now has three compatible authored variants. Unique collisions, stable speed-band crossings, and stable pair-coupling changes queue deterministic changes at the next eligible unscheduled bar. Proximity transforms the resolved collision/speed material rather than selecting another variant. Physics and input pressure remain independent of musical time, and the interface exposes motion state, phrase speeds, activity bands, variants, exact pair proximity and coupling state, audition mode, transition counts, timing, catch-up, queue, coalescing, rejection, and snapshot diagnostics.

The native/UI bridge uses a validated protocol-versioned envelope. Invalid commands produce structured rejection events without mutating engine state, and every React load performs a fresh handshake that restores the current authoritative transport state without restarting playback.

## Development

### Requirements

- macOS 13 or newer
- Xcode command-line tools
- CMake 3.25 or newer
- Node.js 24 and npm

JUCE 9.0.0 is fetched and pinned by CMake. JavaScript dependencies are pinned by `ui/package-lock.json`.

Run the full local verification path:

```sh
./scripts/verify.sh
```

Build and launch the debug application:

```sh
cmake --preset debug
cmake --build --preset debug --target Drift
open build/debug/Drift_artefacts/Debug/Drift.app
```

Release builds use the equivalent `release` preset.

## Documentation

- [Product requirements](docs/PRD.md)
- [Technical design](docs/TECHNICAL-DESIGN.md)
- [Implementation plan](docs/IMPLEMENTATION-PLAN.md)
- [MIDI output smoke test](docs/MIDI-SMOKE-TEST.md)
- [Deterministic timing stress test](docs/TIMING-STRESS-TEST.md)
- [Open issues](https://github.com/datashaman/drift/issues)

## Success criteria

Drift succeeds if its spatial world produces musically compelling behavior and gives the user meaningful ways to influence its evolution—whether through deliberate composition, playful intervention, or observation.

Predictable action and response is one valuable outcome, but not the only one. Surprise, emergence, and autonomous behavior are also part of the experiment.

## License

Drift is available under the [MIT License](LICENSE).
