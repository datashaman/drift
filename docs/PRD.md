# Drift: Spatial Phrase Sequencer

## Product requirements document

| Field | Value |
| --- | --- |
| Status | Draft for POC implementation |
| Product | Drift |
| Product description | Spatial phrase sequencer |
| Scope | Proof of concept |

## 1. Summary

Drift is an experimental 2D musical sequencer in which short, looping musical phrases exist as persistent objects moving through a shared space.

Instead of arranging notes or clips along a timeline, the user shapes a composition by manipulating phrase objects. Position, proximity, movement, and collisions influence how the phrases behave musically.

The POC exists to answer one question:

> Can manipulating musical phrases as spatial objects produce an interaction that is both musically useful and enjoyable?

Drift is not a DAW, production environment, or finished instrument. It is a focused experiment in whether spatial dynamics can produce a musical experience that is compelling to shape, perturb, or observe.

## 2. Product principles

### 2.1 The fundamental object is a phrase, not a note

A phrase is a short looping musical sequence associated with a role and MIDI channel. Each phrase exists simultaneously in two domains:

- Musical state: note events, loop length, variants, current and pending variant, MIDI channel, activity, and mute/playback state.
- Spatial state: position, velocity, radius, and mass.

### 2.2 Physics changes musical state; it does not generate notes

Spatial activity produces a musical intent. The intent becomes effective on a musical boundary and then changes phrase state:

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

A collision may request a pattern change, for example, but it must not emit an immediate arbitrary note. This distinction prevents the experience from becoming random physics accompanied by incidental sound.

### 2.3 Begin with a coherent composition

All phrases and variants are manually authored to work together. Spatial interaction transforms an already-musical composition rather than attempting to create coherence from randomness.

### 2.4 Support agency without eliminating emergence

The user should be able to form useful relationships between spatial actions and musical outcomes without needing complete control over the system. Legibility and meaningful influence matter, but surprise, emergence, and autonomous behavior are valid parts of the experience rather than failures to be designed away.

## 3. Goals

The POC must demonstrate:

- Four phrases playing simultaneously from one global transport.
- Phrases represented as visible objects in a bounded 2D world.
- Direct manipulation by dragging and throwing.
- Continuous movement and boundary collisions.
- Reliable phrase-to-phrase collision detection.
- Continuous proximity measurement.
- Spatial relationships that audibly change phrase behavior.
- Musical changes applied on quantized boundaries.
- Standard MIDI output to an external instrument, hardware device, or DAW.
- An experience that feels like manipulating a musical ecosystem rather than editing a sequence.

## 4. Non-goals

The POC does not include:

- Audio synthesis, recording, or audio-file playback.
- Piano-roll editing, arrangement timelines, tracks, mixers, or automation lanes.
- Plugin hosting or DAW plugin formats.
- MIDI input.
- Saving or loading projects.
- Undo and redo.
- AI generation or MCP integration.
- 3D graphics.
- Mobile support or multiplayer.
- Production-quality visual design.
- User-authored phrases or variants.

Musical material and configuration may be hard-coded where that keeps the experiment focused.

## 5. Primary experience

On launch, the user sees four phrase objects moving slowly around a bounded 2D space:

```text
┌──────────────────────────────────────────────┐
│                                              │
│      ● BASS →                                │
│                                              │
│                         ● MELODY              │
│                           ↙                  │
│                                              │
│              ● CHORDS                        │
│                                              │
│                                  ● DRUMS     │
│                                              │
└──────────────────────────────────────────────┘
  120 BPM                              ▶ PLAY
```

Pressing Play starts the global transport and all four phrases. The user can grab an object and reposition it. Releasing while moving throws the object at the pointer's release velocity. Objects keep moving and bounce off the world boundaries.

As phrases move, their relationships transform the composition:

- Bringing phrases together increases their rhythmic coupling.
- Moving a phrase faster makes it more active.
- Colliding phrases requests a variant change at the next bar.

The user should be able to discover this basic interaction vocabulary without documentation.

## 6. Initial composition

The POC ships with one constrained composition:

| Property | Initial value |
| --- | --- |
| Tempo | 120 BPM |
| Meter | 4/4 |
| Key | C minor |
| Phrase length | 1–2 bars |
| Phrase count | 4 |
| Variants | 3 per phrase |

The four phrases are:

| Phrase | Role | MIDI channel |
| --- | --- | ---: |
| Drums | Rhythm | 10 |
| Bass | Bass line | 1 |
| Chords | Harmony | 2 |
| Melody | Lead | 3 |

All twelve variants must be deliberately simple and mutually compatible. Random phrase generation is excluded.

## 7. Functional requirements

### 7.1 Global transport

The application provides:

- Play.
- Stop.
- BPM display and control.
- A default of 120 BPM in 4/4.

All phrase playback derives from the same transport clock. Phrase synchronization must not depend on the physics or rendering update rate.

Stopping must stop playback and send any MIDI messages required to prevent stuck notes.

### 7.2 MIDI output

The user can select an available MIDI output device. Each phrase emits standard MIDI note-on and note-off events on its configured channel.

No instrument-specific integration is required.

### 7.3 Movement

Each phrase has position, velocity, radius, and mass. When not being manipulated, it moves according to its velocity.

Objects bounce from the edges of the world. The simulation should feel predictable and tactile; perfect physical accuracy is unnecessary.

### 7.4 Drag and throw

Dragging directly controls a phrase's position. While dragging, the object visibly enters a selected state.

On release, recent pointer motion determines the phrase's velocity. A stationary release leaves the phrase stationary or nearly stationary; a fast release produces a visibly faster throw.

### 7.5 Phrase collisions

The system detects overlap between phrase objects. A new collision requests a state transition for one or both phrases.

For the POC, a collision advances or selects a different predefined variant. The transition becomes effective at the next bar boundary.

The object displays its pending state before the transition, for example `BASS A → B`. A short cooldown prevents repeated requests while objects remain in contact.

### 7.6 Proximity and rhythmic coupling

The distance between each pair of phrases continuously produces a normalized proximity value:

```text
0.0 = unrelated
1.0 = extremely close
```

Proximity controls rhythmic coupling. As two phrases approach, their rhythmic behavior becomes increasingly related.

The implementation should select the simplest audible mechanism from the following:

- Increased probability of shared accents.
- Increased rhythmic density.
- Synchronization of phrase starts.
- Selection of increasingly compatible rhythmic variants.

The effect must be gradual rather than binary and must remain musically quantized.

### 7.7 Speed and phrase activity

Object speed controls phrase activity. Slow or stationary objects use sparse variants; faster objects use busier variants.

The mapping may be continuous or use a few clearly defined bands. Resulting changes remain quantized.

### 7.8 Visual language

Each object communicates its state without an inspector. At minimum it represents:

- Phrase role or name.
- Current variant.
- Direction and movement.
- Selected/drag state.
- Pending transition.
- Playback state.

Faint relationship lines may visualize meaningful proximity. The main interface must avoid conventional DAW language: no tracks, timeline, or piano roll.

### 7.9 Debug mode

A developer overlay exposes:

- Rendering frame rate.
- BPM and current beat/bar.
- Object velocity.
- Pairwise proximity.
- Current and pending variants.
- Collision events and cooldowns.
- Emitted MIDI events.

The overlay may favor utility over visual polish.

## 8. Core product model

### World

```text
World
├── phrases[]
├── bounds
└── transport
```

### Phrase

```text
Phrase
├── id, name, role
├── position, velocity, radius, mass
├── midiChannel
├── variants[]
├── currentVariant
├── pendingVariant
└── proximity[]
```

### Variant

```text
Variant
├── id
├── length
├── events[]
└── activity
```

### Musical intent

Spatial behavior produces an explicit musical intent rather than directly mutating musical state:

```json
{
  "phraseId": "bass",
  "type": "CHANGE_VARIANT",
  "value": "B",
  "quantization": "BAR"
}
```

## 9. Technical constraints and architecture decision

The POC will be a native JUCE desktop application with an embedded React WebView.

- The JUCE engine owns transport, musical time, scheduling, quantization, MIDI, phrase state, spatial simulation, collision/proximity detection, musical interaction rules, and application lifecycle.
- The React UI owns presentation, pointer interaction, controls, animation, visual interpolation, and debug visualization.
- The UI communicates semantic user intent to the engine and renders engine state. It is not the authoritative source of musical or spatial state.
- WebView timing must never participate in timing-critical musical execution.
- PixiJS is the default renderer for the spatial canvas, subject to validation during the first UI slice.

Implementation details are defined in [TECHNICAL-DESIGN.md](./TECHNICAL-DESIGN.md).

## 10. Acceptance criteria

### Transport and MIDI

- Play starts all four phrases from the shared transport.
- Stop ends playback without stuck MIDI notes.
- Phrases remain audibly synchronized for at least ten minutes.
- The user can select an available MIDI output.
- Every phrase continuously emits its authored MIDI material while playing.

### Spatial interaction

- All four objects can be dragged and thrown.
- Objects move independently and bounce at world boundaries.
- Phrase collisions are reliably detected once per contact/cooldown cycle.
- Pairwise proximity is continuously calculated and visible in debug mode.

### Musical interaction

- A collision causes an audible phrase change at the next bar.
- Movement speed audibly affects phrase activity.
- Proximity audibly affects rhythmic relationships.
- Spatial events cannot produce accidental, unquantized notes.
- Pending changes are visually legible before they become active.

### Usability

Without documentation, a new user can:

1. Start playback.
2. Grab a phrase.
3. Move or throw it.
4. Hear a resulting musical change.

## 11. Evaluation plan

The POC is successful if its spatial world produces musically compelling behavior and gives the user a meaningful relationship with its evolution. That relationship may take several forms:

- **Deliberate composition:** the user predicts a class of result and performs a spatial action to produce it.
- **Playful intervention:** the user perturbs the system, hears how it responds, and uses that response to choose a next action.
- **Observation:** autonomous phrase behavior remains musically coherent, visually engaging, and interesting without continuous manipulation.

After approximately five minutes, a user should be able to describe how their actions affect the system, identify behavior they find musically or visually compelling, or express interest in watching the ecosystem continue to evolve.

A repeated intentional loop remains one strong success signal, but it is not the only one:

1. The user predicts or explores a class of musical result.
2. The user performs a spatial action or chooses to let the system evolve.
3. The system produces a legible or compelling response.
4. The result informs the user's next action, or sustains their interest without one.

Example spatial intuition:

```text
“I want the bass tighter with the drums.”
    → move them together

“I want the melody to become more active.”
    → throw it

“I want something different harmonically.”
    → collide melody with chords
```

The experiment has failed only if the spatial model contributes neither compelling autonomous behavior nor meaningful opportunities for intervention—for example, if the same musical experience would be served just as well by non-spatial controls.

## 12. Open product questions

The following should be resolved through the POC rather than designed in advance:

- Which proximity-to-rhythm mapping is most audible and intuitive?
- Should a collision change one phrase, both phrases, or depend on their roles?
- What cooldown duration feels intentional without making collisions unresponsive?
- How many speed/activity bands are perceptually useful?
- How much relationship visualization helps before the world becomes visually noisy?
- Does changing BPM belong in the primary POC interaction, or should tempo remain fixed during evaluation?
