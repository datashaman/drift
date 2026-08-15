# Drift POC technical design

| Field | Value |
| --- | --- |
| Status | Proposed implementation baseline |
| Related product document | [PRD.md](./PRD.md) |
| Architecture | JUCE native engine with embedded React WebView |

## 1. Purpose

This document defines an implementation baseline for the Drift proof of concept. It converts the product behavior in the PRD into component boundaries, runtime responsibilities, data flow, threading, protocols, and verification seams.

The design optimizes for answering the product experiment quickly while preserving reliable musical timing. It is not a production plugin architecture.

## 2. Architecture decision

Drift will be implemented as a native JUCE desktop application with an embedded React WebView.

JUCE provides the instrument runtime: native MIDI access, reliable musical scheduling, application lifecycle, and a path toward future native audio or plugin formats if the experiment succeeds.

React provides the experimental interaction surface. The spatial interface is expected to evolve rapidly, so web technology allows the interaction and visual language to change without coupling presentation work to musical scheduling.

The boundary is deliberately semantic. Other future control surfaces—MIDI controllers, automation, or an agent/MCP client—should be able to issue the same commands without depending on the React UI.

### 2.1 Non-negotiable constraint

The WebView must never participate in timing-critical musical execution.

```text
UI event
  ↓ semantic command
native engine
  ↓ spatial state change
musical intent
  ↓ quantizer
phrase state
  ↓ scheduler
MIDI
```

Rendering stalls, WebView scheduling, and JavaScript garbage collection must not affect MIDI event time.

## 3. System context

```text
┌───────────────────────────────────────────────┐
│ Drift desktop process                         │
│                                               │
│  ┌─────────────────────┐  commands/events     │
│  │ React + PixiJS UI   │◄──────────────────► │
│  │ embedded WebView    │                      │
│  └─────────────────────┘  ┌────────────────┐ │
│                            │ JUCE engine    │ │
│                            │                │ │
│                            │ world/physics  │ │
│                            │ music/clock    │ │
│                            │ MIDI           │ │
│                            └───────┬────────┘ │
└────────────────────────────────────┼──────────┘
                                     │ MIDI
                                     ▼
                          external synth / DAW
```

## 4. Ownership boundaries

### 4.1 Native engine owns

- Authoritative world and phrase state.
- Global transport, tempo, meter, beat, and bar position.
- Phrase playback and event scheduling.
- Quantization and pending musical intents.
- MIDI output discovery, selection, and device lifecycle.
- Spatial simulation, bounds, collision, and proximity.
- Musical mappings from proximity, collision, and speed.
- Collision cooldowns.
- Application startup and shutdown.
- Debug measurements and event history exposed to the UI.

### 4.2 React UI owns

- The visual composition of the application.
- PixiJS world rendering and animation.
- Phrase labels and visual state.
- Pointer sampling and gesture recognition.
- Drag and throw commands.
- Transport and MIDI-device controls.
- Debug overlay rendering.
- Interpolation between native state snapshots.

### 4.3 React UI does not own

- Canonical phrase position or velocity.
- Collision or proximity outcomes.
- Current or pending musical variants.
- Musical clock or quantization.
- MIDI scheduling.
- Musical mapping rules.

During a drag, the UI may optimistically render the pointer-controlled position, but the engine remains authoritative and echoes the accepted state.

## 5. Runtime model and threads

The initial implementation should use three logical execution contexts.

### 5.1 JUCE message thread

Responsibilities:

- Window and WebView lifecycle.
- Receiving bridge messages.
- Validating and enqueueing commands.
- Publishing UI snapshots/events.
- MIDI device selection requests that require message-thread APIs.

The message thread must not perform timing-critical scheduling or blocking work.

### 5.2 Engine thread

A dedicated high-resolution worker owns mutable world and music state.

Responsibilities:

- Fixed-step spatial simulation.
- Proximity and collision calculation.
- Converting spatial changes to musical intents.
- Applying quantized state transitions.
- Advancing transport state.
- Producing timestamped MIDI events ahead of their deadlines.
- Building immutable UI snapshots.

The engine loop must not call into the WebView or parse JSON.

### 5.3 MIDI delivery

Use JUCE's timestamp-capable MIDI output path or a dedicated MIDI sender so already-scheduled events are delivered independently from rendering and WebView work.

All outbound note events must be paired with note-off events. Stop, device change, shutdown, and transport reset must send an all-notes-off/panic sequence appropriate for the active channels.

### 5.4 Communication between contexts

- UI commands are parsed and validated on the message thread, then passed to the engine through a bounded thread-safe queue.
- The engine writes immutable snapshots to a double/triple buffer or atomic shared pointer.
- The message thread publishes the most recent snapshot to React; stale intermediate snapshots may be dropped.
- Discrete events such as collision, intent queued, transition applied, and MIDI error use a bounded event queue.
- Musical state commands may never be silently dropped. High-frequency drag updates may be coalesced by phrase ID.

No lock held by the UI/message thread may block the engine thread.

## 6. Clocks and update rates

The POC uses separate rates for musical scheduling, spatial simulation, and presentation.

| Concern | Initial rate/strategy | Notes |
| --- | --- | --- |
| Musical position | Derived from monotonic high-resolution time | Never frame-count based |
| MIDI look-ahead | 50 ms, evaluated in small engine intervals | Tune against target devices |
| Engine wake interval | Approximately 1–2 ms while playing | Scheduling granularity, not musical truth |
| Physics | Fixed 120 Hz | Stable and independent of render rate |
| UI snapshots | 30 Hz | Latest-state delivery; intermediate states droppable |
| Web rendering | `requestAnimationFrame` | Interpolate snapshots for smooth motion |

These are starting values, not product requirements. Debug instrumentation must make underruns, late MIDI events, and queue pressure visible before tuning them.

### 6.1 Musical position

Transport position is calculated from a monotonic start time, accumulated pause duration, and tempo. It must not be advanced by assuming an engine callback duration.

At 120 BPM in 4/4:

- One quarter note is 500 ms.
- One bar is 2,000 ms.

Quantization computes the next eligible boundary in transport time. State transitions are applied exactly once when that boundary is crossed.

### 6.2 Physics position

The engine uses an accumulator-based fixed timestep. Elapsed wall time is accumulated and consumed in fixed physics steps. A maximum catch-up count prevents a stalled process from running an unbounded simulation burst.

Musical time continues to derive from the monotonic clock even if physics catch-up is capped.

## 7. Native/UI protocol

The bridge transports versioned JSON envelopes. Commands express intent; events and snapshots describe accepted engine state.

### 7.1 Envelope

```json
{
  "protocolVersion": 1,
  "messageId": "01J...",
  "type": "phrase.throw",
  "payload": {}
}
```

Unknown versions or message types are rejected with a structured error event. Payloads are validated before entering the engine queue.

### 7.2 UI-to-engine commands

Initial command set:

| Command | Purpose |
| --- | --- |
| `app.connect` | Begin a UI session and request `app.ready` plus current authoritative state |
| `transport.play` | Start or resume transport |
| `transport.stop` | Stop transport and silence MIDI |
| `transport.setTempo` | Set BPM within a safe range |
| `midi.selectOutput` | Select an available output by stable runtime ID |
| `phrase.dragStart` | Begin direct manipulation of a phrase |
| `phrase.move` | Set target position during a drag |
| `phrase.dragEnd` | End direct manipulation without a throw |
| `phrase.throw` | End manipulation with release velocity |
| `debug.setEnabled` | Enable or disable debug data publication |

Example:

```json
{
  "protocolVersion": 1,
  "messageId": "01J5XQW7Q5",
  "type": "phrase.throw",
  "payload": {
    "phraseId": "bass",
    "velocity": { "x": 0.42, "y": -0.18 }
  }
}
```

Positions and velocities use normalized world coordinates so UI size and display density do not alter simulation semantics.

### 7.3 Engine-to-UI messages

| Message | Purpose |
| --- | --- |
| `app.ready` | Protocol version and initial capabilities |
| `transport.state` | Current authoritative transport and MIDI-output state |
| `world.snapshot` | Latest authoritative world and transport state |
| `transport.changed` | Discrete transport transition |
| `midi.outputsChanged` | Available MIDI outputs and current selection |
| `phrase.transitionQueued` | Pending quantized phrase change |
| `phrase.transitionApplied` | New phrase state became active |
| `world.collision` | Debug/visual collision event |
| `engine.warning` | Late event, queue pressure, or recoverable fault |
| `command.rejected` | Invalid or inapplicable UI command |

### 7.4 Snapshot shape

```json
{
  "protocolVersion": 1,
  "type": "world.snapshot",
  "sequence": 1842,
  "engineTimeMs": 43122.4,
  "payload": {
    "transport": {
      "playing": true,
      "bpm": 120,
      "bar": 12,
      "beat": 2.25
    },
    "phrases": [
      {
        "id": "bass",
        "position": { "x": 0.31, "y": 0.62 },
        "velocity": { "x": 0.08, "y": -0.03 },
        "radius": 0.06,
        "variant": "B",
        "pendingVariant": null,
        "activity": 0.48,
        "dragged": false,
        "playing": true
      }
    ]
  }
}
```

Sequence numbers let React discard out-of-order snapshots. `engineTimeMs` lets it interpolate positions between snapshots without becoming authoritative.

## 8. Native domain model

Illustrative C++ types:

```cpp
struct NoteEvent {
    double beat;
    int note;
    int velocity;
    double durationBeats;
};

struct Variant {
    VariantId id;
    double lengthBeats;
    std::vector<NoteEvent> events;
    float activity;
};

struct Phrase {
    PhraseId id;
    std::string name;
    PhraseRole role;
    Vec2 position;
    Vec2 velocity;
    float radius;
    float mass;
    int midiChannel;
    std::vector<Variant> variants;
    VariantId currentVariant;
    std::optional<VariantId> pendingVariant;
    bool dragged;
};

struct MusicalIntent {
    PhraseId phraseId;
    IntentType type;
    IntentValue value;
    Quantization quantization;
    MusicalTime applyAt;
};
```

Identifiers should be stable value types rather than UI array indexes.

## 9. Engine pipelines

### 9.1 Physics pipeline

For each fixed physics step:

1. Apply pending drag/move/throw commands.
2. Integrate position and velocity.
3. Resolve world-boundary collisions.
4. Detect phrase overlaps.
5. Track contact begin/end and collision cooldown.
6. Calculate pairwise normalized proximity.
7. Calculate normalized speed/activity.
8. Produce musical intents when thresholds or contact events require them.

The collision system distinguishes a new contact from continued overlap. Cooldown belongs to the phrase pair, not only to an individual phrase.

### 9.2 Intent and quantization pipeline

```text
spatial observation
  → mapping rule
  → MusicalIntent
  → deduplicate/coalesce
  → calculate quantized applyAt
  → pending-intent queue
  → apply on boundary
  → publish transition event
```

Only the intent processor may change a phrase's musical variant because of spatial behavior. This creates a testable seam between physics and music.

Continuous inputs such as speed and proximity should be smoothed and quantized into stable states to prevent rapid variant flapping. Hysteresis is preferred over reacting to every small numeric change.

### 9.3 MIDI scheduling pipeline

For each scheduling window:

1. Determine the transport-time range not yet scheduled.
2. For every phrase, enumerate note events whose loop occurrences fall in that range.
3. Resolve the variant active at each event's musical time.
4. Convert musical time to monotonic timestamps.
5. Enqueue note-on and paired note-off messages.
6. Move the scheduled-through watermark forward.

Variant transitions must not invalidate MIDI events that have already been handed to the output beyond the look-ahead window. The look-ahead should remain short enough that a next-boundary change is scheduled with the correct variant.

## 10. Musical mappings

Mappings are isolated behind an interface so each experiment can be changed without altering physics or MIDI scheduling.

```cpp
class MusicalMapping {
public:
    virtual std::vector<MusicalIntent> evaluate(
        const WorldObservation& observation,
        const MusicalState& state) = 0;
};
```

Initial mappings:

- `CollisionVariantMapping`: a new contact requests the next compatible variant at the next bar.
- `SpeedActivityMapping`: smoothed speed selects sparse, normal, or active material at the next bar.
- `ProximityRhythmMapping`: pairwise proximity selects a rhythmic-coupling state at a configured quantization boundary.

`CollisionVariantMapping` uses the six stable unordered pair rules below. The target advances through its authored variant order `A → B → C → A`.

| Ordered pair | Target phrase |
| --- | --- |
| `bass`, `chords` | `chords` |
| `bass`, `drums` | `bass` |
| `bass`, `melody` | `bass` |
| `chords`, `drums` | `drums` |
| `chords`, `melody` | `chords` |
| `drums`, `melody` | `melody` |

Contact begins are sorted lexicographically by their stable ordered phrase IDs before evaluation. A phrase accepts at most one pending variant transition; subsequent same-tick or pre-boundary contacts targeting that phrase are observed but do not replace the accepted intent. This makes simultaneous outcomes independent of physics-body iteration order and prevents contradictory pending state.

If two mappings request incompatible changes for the same phrase and boundary, a deterministic policy resolves them. For the POC, explicit collision changes should take priority over continuous speed/proximity changes.

## 11. React and PixiJS design

### 11.1 Application layers

```text
React shell
├── transport and MIDI controls
├── bridge client and snapshot store
├── debug overlay
└── PixiJS world canvas
    ├── phrase sprites
    ├── relationship lines
    ├── pending-state indicators
    └── pointer gesture adapter
```

React should not rerender the component tree every animation frame. PixiJS objects read from a small render model updated by the snapshot store.

### 11.2 Interpolation

React retains the two most recent authoritative snapshots. The render loop interpolates position using their sequence and engine timestamps. Limited extrapolation may use authoritative velocity if a snapshot is late, then reconcile smoothly when a new snapshot arrives.

During drag, pointer position is rendered immediately and `phrase.move` commands are rate-limited/coalesced. The next engine snapshot confirms the canonical position.

### 11.3 PixiJS validation gate

The first UI slice must prove:

- Four objects render and animate smoothly in the embedded WebView.
- Pointer hit testing and drag/throw gestures work.
- Text/state indicators remain legible.
- The production bundle loads from application resources without a development server.

If PixiJS materially complicates embedding without improving the POC, an HTML Canvas implementation is an acceptable substitution. Standard DOM animation should only be chosen if it meets the same interaction and frame-rate needs with less code.

## 12. MIDI device lifecycle

- Enumerate outputs on startup and when the OS device list changes.
- Expose opaque runtime IDs and human-readable names to React.
- Open only the selected device.
- On device change, stop scheduling, silence and close the old device, open the new one, then resume only on an explicit or preserved transport policy.
- Surface failure as `engine.warning`/device state; do not crash the engine.
- On stop and shutdown, clear future scheduling and send panic/all-notes-off.

The POC does not persist device choice across launches unless this is trivial on the target platform.

## 13. Proposed source layout

```text
drift/
├── CMakeLists.txt
├── cmake/
├── Source/
│   ├── App/
│   │   ├── DriftApplication.*
│   │   └── MainWindow.*
│   ├── Bridge/
│   │   ├── CommandParser.*
│   │   ├── Protocol.*
│   │   └── WebViewBridge.*
│   ├── Domain/
│   │   ├── Phrase.*
│   │   ├── Transport.*
│   │   ├── Variant.*
│   │   └── World.*
│   ├── Engine/
│   │   ├── DriftEngine.*
│   │   ├── EngineThread.*
│   │   └── SnapshotPublisher.*
│   ├── Music/
│   │   ├── IntentProcessor.*
│   │   ├── Mappings.*
│   │   ├── MidiScheduler.*
│   │   └── Quantizer.*
│   ├── Spatial/
│   │   ├── CollisionSystem.*
│   │   ├── PhysicsWorld.*
│   │   └── ProximitySystem.*
│   └── Tests/
├── ui/
│   ├── package.json
│   ├── src/
│   │   ├── bridge/
│   │   ├── components/
│   │   ├── debug/
│   │   ├── world/
│   │   └── main.tsx
│   └── vite.config.ts
└── docs/
```

Names may change during the first vertical slice, but ownership boundaries should remain.

## 14. Build and resource pipeline

- CMake is the top-level native build.
- JUCE is consumed as a pinned dependency.
- The UI is a TypeScript React application built with Vite.
- The production UI build emits static files.
- CMake invokes or depends on the UI production build and embeds its output as JUCE binary resources, or copies it into a deterministic application resource directory if required by the selected WebView backend.
- Development mode may load a local Vite server behind an explicit build option; packaged builds must not depend on a server or network access.
- A generated protocol-version constant is shared or verified across native and UI builds.

The exact JUCE WebView bridge API and platform resource scheme must be validated with a minimal macOS vertical slice before wider implementation. Windows is not required for the first POC unless separately requested.

## 15. Testing strategy

### 15.1 Native unit tests

- Quantizer returns correct next beat/bar boundaries.
- Fixed-step physics produces deterministic results from a known initial state.
- Boundary collisions preserve expected direction and bounded positions.
- Contact tracking emits one collision per contact/cooldown cycle.
- Proximity normalization and clamping are correct.
- Mapping rules create expected intents.
- Conflicting intents resolve deterministically.
- Phrase loop enumeration produces correct events across bar and loop boundaries.
- Stop/panic logic pairs or clears active notes.
- Protocol parsing rejects malformed, unknown, and out-of-range commands.

### 15.2 Native integration tests

- A fake clock advances transport deterministically.
- A fake MIDI sink verifies timestamps, channels, note-on/off pairs, and variant changes.
- Simulated WebView commands result in authoritative snapshots and events.
- UI snapshot backpressure drops stale state without blocking the engine.
- Ten simulated minutes maintain phase alignment across all phrases.

### 15.3 UI tests

- Snapshot reducer rejects old sequence numbers.
- Bridge commands have valid envelope and normalized values.
- Drag and throw gestures produce the expected semantic command sequence.
- Visual state reflects current and pending variants.
- A mock bridge supports browser-only development and automated tests.

### 15.4 Manual POC checks

- Route MIDI to a known software or hardware instrument.
- Play for at least ten minutes and listen/check logs for drift or stuck notes.
- Stress the WebView by resizing and enabling debug rendering; MIDI timing must remain stable.
- Drag continuously, throw rapidly, and create repeated collisions.
- Disconnect/reconnect or change the MIDI output and verify safe recovery.
- Run the five-minute intentionality evaluation from the PRD.

## 16. Observability

The engine records bounded diagnostic counters rather than unbounded logs:

- Engine loop lateness and maximum lateness.
- MIDI events scheduled late.
- Command queue depth, coalesced drag messages, and rejected commands.
- Snapshot sequence and dropped/coalesced snapshots.
- Physics catch-up steps and capped catch-up incidents.
- Active contacts and collision cooldowns.
- Pending and applied intents.
- Current scheduling watermark.

The React debug overlay reads these through snapshots or low-rate debug events.

## 17. Failure handling

- Invalid bridge commands are rejected and do not reach engine state.
- A full high-frequency command queue coalesces or drops stale movement commands; transport/device commands return a visible error and are never silently discarded.
- Missing MIDI output leaves the application usable visually and reports MIDI as disconnected.
- WebView reload requests a fresh `app.ready` and full snapshot; native playback may continue.
- Engine shutdown first stops transport and MIDI, then stops the worker, then destroys the WebView/window.
- Uncaught UI errors must not terminate the native engine.

## 18. Security and trust boundary

The embedded UI is packaged application code, but bridge input is still treated as untrusted:

- Validate type, size, numeric ranges, IDs, and protocol version.
- Do not expose arbitrary native method invocation.
- Do not provide filesystem or shell access through the bridge.
- Disable or restrict external navigation in the production WebView.
- Load only packaged resources in production.
- Bound message and queue sizes to prevent memory growth.

## 19. Implementation slices

Build the POC as vertical, playable increments:

1. **Native timing spike:** one hard-coded phrase to fake and real MIDI, with play/stop and a ten-minute timing check.
2. **WebView bridge spike:** packaged React view sends play/stop and renders native transport state.
3. **World slice:** four native phrase objects, fixed physics, snapshots, PixiJS rendering, drag and throw.
4. **Collision slice:** contact tracking creates a next-bar variant intent and visible pending state.
5. **Speed slice:** speed bands select activity variants with smoothing/hysteresis.
6. **Proximity slice:** one rhythmic-coupling mapping with visual and debug feedback.
7. **POC hardening:** MIDI selection, panic paths, observability, ten-minute soak, and five-minute user evaluation.

Each slice should remain runnable and audible before the next mapping is added.

## 20. Decisions to validate during implementation

The architectural boundary is decided; these mechanics remain hypotheses until the relevant slice proves them:

- The exact JUCE WebView bridge API and packaged-resource URL scheme.
- Whether the engine thread should combine physics and scheduling or split them after measurement.
- MIDI look-ahead duration on target devices.
- UI snapshot frequency and interpolation strategy.
- PixiJS versus plain Canvas after the world slice.
- The concrete proximity-to-rhythm mapping.
- Drag command coalescing frequency.
- Whether tempo editing is worth retaining in the POC UI.

Changes to these values do not require revisiting the core decision that JUCE owns authoritative/timing-critical state and React owns presentation and direct manipulation.
