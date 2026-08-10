# ADR-0001: Native engine with embedded web interface

- Status: Accepted
- Date: 2026-08-10

## Context

Drift needs two qualities that tend to evolve at different rates:

- Reliable musical time, MIDI scheduling, device lifecycle, and a future path to native audio and plugin formats.
- An experimental spatial interface whose visual language and direct-manipulation model will change rapidly during the POC.

Musical behavior cannot depend on browser rendering cadence, WebView scheduling, or JavaScript garbage collection. At the same time, implementing every visual experiment directly in native GUI code would couple interaction exploration to the instrument runtime.

## Decision

Drift will be a JUCE native desktop application with an embedded React WebView.

The JUCE engine owns all authoritative and timing-critical state, including transport, musical time, scheduling, quantization, MIDI, phrase state, spatial simulation, collision and proximity calculations, interaction rules, and application lifecycle.

React owns presentation and direct manipulation, including world rendering, phrase visualization, pointer gestures, controls, interpolation, animation, and debug visualization. PixiJS is the initial spatial renderer, subject to validation when the interactive world is implemented.

The boundary uses versioned semantic commands and state/events. The UI communicates intent such as `transport.play` or `phrase.throw`; it does not mutate engine internals or schedule musical events.

Packaged builds serve the compiled React assets from application resources without a network dependency. A development build may optionally use a local Vite server later, but that origin must never be enabled in a packaged release.

## Consequences

### Positive

- MIDI timing remains independent of UI performance.
- Native and UI behavior can be tested separately using clocks, MIDI sinks, and bridge mocks.
- The interaction surface can evolve without destabilizing the musical engine.
- Future control surfaces can use the same semantic command boundary.
- The application has a credible route toward native audio or plugin formats if the POC succeeds.

### Costs and risks

- The build must coordinate CMake/JUCE and Node/Vite toolchains.
- State synchronization and gesture coalescing require an explicit protocol.
- WebView behavior and resource loading must be validated on each supported platform.
- Debugging can cross native and JavaScript boundaries.

## Alternatives considered

### Browser-only application with Web MIDI

This would reduce initial build complexity, but browser timing and MIDI/device behavior would become part of the instrument runtime. It also provides a weaker path to native audio and plugin formats.

### Fully native JUCE interface

This would eliminate the bridge, but it would couple fast-moving visual experiments to native GUI implementation and make the spatial interaction language slower to iterate.

### Separate native engine and browser process

This would make the boundary explicit but add process management and transport complexity that the POC does not need.
