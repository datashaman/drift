# Drift POC implementation plan

| Field | Value |
| --- | --- |
| Status | Ready for issue breakdown |
| Product requirements | [PRD.md](./PRD.md) |
| Technical design | [TECHNICAL-DESIGN.md](./TECHNICAL-DESIGN.md) |
| Delivery strategy | Vertical, runnable slices |
| Initial platform | macOS desktop |

## 1. Outcome

Deliver a proof of concept in which a user can route MIDI to an external instrument, start four synchronized phrases, and experience a composition that can be shaped, perturbed, or observed as its spatial ecosystem evolves.

The POC is complete only when it answers the product question: can spatial dynamics produce a musically compelling environment with meaningful opportunities for intervention, observation, or both?

## 2. Delivery principles

1. **Retire timing risk first.** Prove deterministic transport and MIDI scheduling before adding a WebView or physics.
2. **Build vertical slices.** Every milestone ends in a runnable capability with an explicit exit gate.
3. **Keep the native engine authoritative.** React renders state and sends intent; it never schedules music or decides musical outcomes.
4. **Test musical behavior without hardware.** Clocks and MIDI destinations must be replaceable with deterministic fakes.
5. **Add one mapping at a time.** Collision, speed, and proximity enter separately so their musical effect can be heard and evaluated.
6. **Prefer observable simplicity.** Hard-coded material and direct rules are appropriate for the POC.
7. **Stop if the metaphor fails.** Do not harden or expand a spatial mapping that users cannot learn or control.

## 3. Critical path

```text
repository foundation
  → transport + MIDI timing
  → packaged WebView bridge
  → authoritative spatial world
  → collision-to-variant mapping
  → speed-to-activity mapping
  → proximity-to-rhythm mapping
  → POC hardening and evaluation
```

The authored phrase set and basic React visual exploration can proceed alongside the native timing work, but integration follows the path above.

## 4. Definition of done for every milestone

A milestone is complete when:

- Its automated tests pass locally.
- The new capability is reachable in the runnable application or a named executable test harness.
- Failure and edge paths relevant to the slice are exercised.
- Debug output is sufficient to explain incorrect behavior.
- Product and technical docs are updated if implementation invalidates an assumption.
- No later milestone is required to demonstrate the slice's core claim.

## 5. Milestone 0 — Repository and architecture foundation

**Goal:** Establish a reproducible native/UI workspace and record the decisions implementation must preserve.

### Work

- Create `docs/adr/0001-application-architecture.md` recording:
  - JUCE native application with embedded React WebView.
  - Native ownership of authoritative and timing-critical state.
  - Semantic command/event boundary.
  - React/PixiJS as a replaceable presentation layer.
- Add the top-level CMake project and pin JUCE.
- Create the native application target and native test target.
- Create the TypeScript React/Vite workspace and UI test command.
- Add formatting, linting, and test scripts with one documented local verification command.
- Add CI for native configure/build/tests and UI install/build/tests.
- Establish debug and release build profiles.
- Add a minimal native window and a placeholder UI build artifact; bridge behavior is deferred to Milestone 2.

### Exit gate

- A clean clone can configure, build, and test the native project from documented commands.
- The UI can install, test, and produce a production bundle.
- CI runs the same checks.
- The native application launches a window on macOS.

### Explicitly deferred

- MIDI output.
- WebView resource loading and bridge behavior.
- Spatial rendering.

## 6. Milestone 1 — Native transport and MIDI timing

**Goal:** Prove that musical time remains stable and independently testable before UI integration.

### Work

#### Domain and timing

- Implement value types for musical time, beats, bars, tempo, phrase IDs, and variant IDs.
- Implement a monotonic clock abstraction with production and fake-clock implementations.
- Implement play, stop, tempo, meter, and transport-position calculation.
- Implement beat/bar quantization and exact-once boundary transitions.

#### Phrase playback

- Implement `NoteEvent`, `Variant`, and `Phrase`.
- Author one hard-coded phrase and enumerate its loop occurrences across scheduling windows.
- Implement the scheduled-through watermark and timestamp conversion.
- Ensure every note-on has a paired note-off.

#### MIDI boundary

- Define a MIDI sink interface.
- Implement a recording fake sink for tests.
- Implement JUCE MIDI output enumeration, open/close, timestamped delivery, and panic/all-notes-off.
- Add a minimal native play/stop control or command-line/debug trigger for real-device testing.

#### Verification

- Unit-test quantization at exact boundaries and immediately before/after them.
- Test loops that cross bars and scheduling-window boundaries.
- Test stop, device change, and shutdown with active notes.
- Simulate at least ten minutes with a fake clock and verify phrase phase alignment.
- Run a real ten-minute MIDI soak against a selected device or software MIDI monitor.

### Exit gate

- One phrase plays through real MIDI at 120 BPM.
- Play and stop behave safely with no stuck notes.
- Simulated and real ten-minute tests show no accumulating transport drift.
- Scheduling behavior can be verified without a WebView or MIDI hardware.

### Decision checkpoint

Measure and record the initial MIDI look-ahead and engine wake interval. Keep the technical design defaults only if the measurements support them.

## 7. Milestone 2 — Embedded React bridge

**Goal:** Prove the complete packaged native/UI boundary without introducing spatial complexity.

### Work

- Embed the React production bundle in the JUCE application.
- Support an optional Vite development URL behind an explicit development setting.
- Restrict production navigation to packaged application resources.
- Implement protocol versioning and validated JSON envelopes.
- Implement:
  - `app.ready`
  - `transport.play`
  - `transport.stop`
  - `transport.setTempo`
  - `transport.changed`
  - a minimal `world.snapshot` containing transport state
  - `command.rejected`
- Add a mock browser bridge for UI tests and browser-only development.
- Surface protocol or bridge errors in a small debug panel.
- Verify that WebView reload reconnects and receives a fresh authoritative snapshot while native state remains valid.

### Exit gate

- The packaged application runs without a development server or network access.
- React starts and stops native MIDI playback.
- React displays authoritative BPM, play state, bar, and beat.
- Invalid messages are rejected without changing engine state.
- Deliberately stalling or reloading the UI does not disturb MIDI timing.

### Decision checkpoint

Record the chosen JUCE bridge API, resource URL scheme, and production/development loading strategy in ADR-0001 or a follow-up ADR if the choice has meaningful alternatives.

## 8. Milestone 3 — Four-phrase spatial world

**Goal:** Make the authoritative native world visible and directly manipulable, without musical mappings.

### Work

#### Initial composition

- Author all four roles: drums, bass, chords, and melody.
- Author an initial compatible variant for each role.
- Assign the required MIDI channels and verify the static composition sounds coherent.

#### Native world

- Implement world bounds and four phrase bodies in normalized coordinates.
- Implement fixed-step integration and boundary bounce.
- Implement the engine thread and bounded UI-command queue.
- Implement immutable, sequenced world snapshots at the initial publication rate.
- Add timing, queue-depth, and physics catch-up counters.

#### UI world

- Validate PixiJS in the embedded WebView.
- Render phrase name, movement, selection, and playback state.
- Interpolate between authoritative snapshots.
- Implement `phrase.dragStart`, coalesced `phrase.move`, `phrase.dragEnd`, and `phrase.throw`.
- Render pointer-controlled position optimistically during drag, then reconcile to native snapshots.

#### Verification

- Unit-test fixed-step integration and boundary collision deterministically.
- Test queue coalescing so stale drag positions do not block transport commands.
- Stress dragging and resizing while MIDI plays.

### Exit gate

- Four coherent phrases remain synchronized through MIDI.
- All four native-owned objects move and bounce in the UI.
- Every object can be dragged and thrown.
- UI stalls and high-frequency gestures do not compromise MIDI timing.
- Packaged PixiJS performance and interaction are acceptable.

### Decision checkpoint

Keep PixiJS if it makes interaction/rendering simpler at acceptable cost. Otherwise switch to plain Canvas before adding mapping-specific visuals.

## 9. Milestone 4 — Collision and quantized variants

**Goal:** Deliver the first complete spatial-to-musical interaction.

### Work

- Author three compatible variants per phrase, including current activity metadata.
- Implement phrase overlap detection.
- Track contact begin/end separately from sustained overlap.
- Implement per-pair collision cooldown.
- Implement `MusicalIntent`, pending-intent storage, and the intent processor.
- Implement `CollisionVariantMapping` with deterministic variant selection.
- Apply the requested transition at the next bar boundary.
- Publish `phrase.transitionQueued`, `phrase.transitionApplied`, and debug collision events.
- Show current and pending variants on phrase objects.
- Add collision/cooldown state to the debug overlay.

### Exit gate

- A deliberate collision audibly requests a predictable variant change.
- The UI shows the pending variant before it becomes active.
- The change occurs exactly once at the next bar.
- Sustained overlap does not repeatedly trigger changes.
- No collision directly emits an unquantized note.

### Product checkpoint

Run a short usability check: can a new user discover that collision causes a delayed phrase change, and do they understand the pending-state feedback?

## 10. Milestone 5 — Speed and phrase activity

**Goal:** Make throwing a phrase a predictable control for musical activity.

### Work

- Calculate normalized phrase speed.
- Smooth speed observations and define hysteretic sparse/normal/active bands.
- Map the bands to deliberately authored compatible variants.
- Implement `SpeedActivityMapping` with quantized changes.
- Define collision priority over simultaneous continuous speed intents.
- Visualize activity and speed without overwhelming the phrase label.
- Add speed, activity band, and pending activity to debug state.

### Exit gate

- Slow or stationary phrases audibly become sparser.
- Fast phrases audibly become more active.
- Small speed fluctuations do not cause variant flapping.
- Throwing produces a result users can predict after a brief demonstration.
- Collision intent priority is deterministic and tested.

### Decision checkpoint

Choose the smallest useful number of activity bands based on listening, not implementation convenience.

## 11. Milestone 6 — Proximity and rhythmic coupling

**Goal:** Make grouping and separation a predictable control for rhythmic relationship.

### Work

- Calculate and normalize pairwise proximity.
- Add smoothing/hysteresis where needed.
- Prototype the smallest two candidate mappings from the PRD using the same interface and authored material.
- Select one through a focused listening/interaction test.
- Implement the chosen `ProximityRhythmMapping` with quantized state changes.
- Define its conflict policy relative to collision and speed.
- Add optional relationship lines and pairwise debug values.

### Candidate order

Try the least generative candidates first:

1. Increasingly compatible rhythmic variants.
2. Shared-accent variants.
3. Phrase-start synchronization only if the first two are not audible or intuitive.

### Exit gate

- Moving two phrases together produces a gradual, audible increase in rhythmic relationship.
- Separating them reverses or relaxes the relationship predictably.
- Small distance jitter does not cause state flapping.
- Users can distinguish the effect from collision and speed.
- Mapping conflicts resolve deterministically and are visible in debug mode.

### Product checkpoint

If no candidate produces either a meaningful way to influence rhythmic coupling or compelling autonomous behavior, record the failed experiment and do not mask it with more complex generation.

## 12. Milestone 7 — POC hardening and evaluation

**Goal:** Produce a stable build suitable for evaluating the product thesis.

### Work

#### Product shell

- Complete play, stop, BPM, and MIDI-output controls.
- Show disconnected, selected, failed, and changed MIDI-device states.
- Finalize phrase, pending-state, relationship, and playback visuals.
- Keep debug mode available but disabled by default.

#### Resilience

- Verify panic/all-notes-off on stop, output change, shutdown, and errors.
- Test WebView reload and UI exceptions while playback continues.
- Bound queues, diagnostic history, and snapshot/event payloads.
- Verify external navigation restrictions in packaged builds.
- Exercise rapid drags, throws, collisions, resize, stop/start, and device changes.

#### Timing and performance

- Run the ten-minute MIDI soak with normal UI activity.
- Repeat while stressing rendering and debug output.
- Record engine lateness, late MIDI events, physics catch-up, queue pressure, and dropped snapshots.
- Tune rates only in response to observed failures.

#### Product evaluation

- Give a new user no operating instructions beyond selecting MIDI output.
- Confirm they can play, drag, throw, and hear a change.
- Run the five-minute evaluation from the PRD across deliberate composition, playful intervention, and observation.
- Capture how the user chose to relate to the system, which behavior they found compelling, and whether the result encouraged action or sustained observation.

### Exit gate

- Every PRD acceptance criterion has recorded evidence.
- No stuck notes occur across the resilience test matrix.
- Four phrases remain synchronized for at least ten minutes under UI stress.
- A new user can operate the core loop without documentation.
- The evaluation produces a clear continue/change/stop conclusion about the spatial metaphor.

## 13. Cross-cutting workstreams

### 13.1 Authored musical material

Start composition work during Milestone 1 and complete it before the relevant mapping milestone:

- Milestone 1: one timing-test phrase.
- Milestone 3: one coherent variant for each of four roles.
- Milestone 4: three compatible variants per role.
- Milestones 5–6: annotate and adjust variants for activity and rhythmic compatibility.

This work is a product dependency, not sample-data cleanup. Weak or incompatible material will make good interaction rules appear ineffective.

### 13.2 Observability

Add diagnostics with each owning subsystem rather than at the end:

| Milestone | Diagnostics introduced |
| --- | --- |
| 1 | Transport position, scheduling watermark, late MIDI, active notes |
| 2 | Protocol errors, reconnect count, command rejection |
| 3 | Engine lateness, physics catch-up, queue depth, snapshot sequence |
| 4 | Contacts, cooldowns, queued/applied intents |
| 5 | Smoothed speed, activity band, mapping decision |
| 6 | Pairwise proximity, coupling state, conflict resolution |

### 13.3 Documentation and decisions

- Keep the PRD focused on behavior and evaluation.
- Update the technical design when validated mechanics differ from its baseline.
- Record architectural decisions with meaningful alternatives in `docs/adr/`.
- Record experimental mapping outcomes in a short dated note under `docs/experiments/` if they will inform later design.

## 14. Dependency and parallelism map

| Work item | Depends on | Can proceed alongside |
| --- | --- | --- |
| Repository foundation | Nothing | ADR drafting |
| Authored phrase material | Basic domain format | Native timing, UI shell |
| Native timing/MIDI | Foundation | UI visual exploration |
| Packaged bridge | Foundation, transport API | Phrase authoring |
| Physics/world rendering | Bridge snapshots, engine loop | Remaining variants |
| Collision mapping | World, quantizer, variants | Debug UI |
| Speed mapping | World, intent processor, variants | Collision usability tuning |
| Proximity mapping | World, intent processor, variants | Visual refinement |
| Hardening/evaluation | All mappings | Documentation closeout |

Avoid parallel changes to the protocol, native domain model, and snapshot shape until Milestone 3 stabilizes their first end-to-end use.

## 15. Risk register

| Risk | Earliest retirement | Response |
| --- | --- | --- |
| MIDI timing drifts or jitters | Milestone 1 | Fake-clock tests, timestamped output, real soak before UI work |
| Packaged WebView bridge is awkward or platform-specific | Milestone 2 | Minimal end-to-end spike before spatial UI |
| WebView load affects MIDI | Milestone 2 | Stall/reload stress test with native scheduling active |
| PixiJS embedding cost exceeds its value | Milestone 3 | Validation gate with plain Canvas fallback |
| Gesture command volume blocks engine work | Milestone 3 | Bounded queue and per-phrase move coalescing |
| Collisions feel random rather than musical | Milestone 4 | Predictable variant cycling and explicit pending feedback |
| Continuous mappings flap | Milestones 5–6 | Smoothing, hysteresis, and quantized state changes |
| Musical material obscures interaction quality | Milestones 3–6 | Manually authored compatible variants and listening checks |
| Combined mappings become incomprehensible | Milestones 5–6 | Add separately, define priority, expose debug decisions |
| Spatial behavior provides neither meaningful agency nor compelling autonomy | Milestones 4–7 | Evaluate deliberate composition, playful intervention, and observation separately |

## 16. Issue breakdown strategy

When converting this plan into GitHub issues:

- Create one tracking issue per milestone.
- Create independently assignable child issues as vertical outcomes, not component-only layers.
- Give every issue a reproducible verification section.
- Label fully specified implementation issues `ready-for-agent`.
- Keep product checkpoints and real MIDI/listening tests explicitly `ready-for-human` when they require human perception or hardware.
- Do not open detailed issues for Milestones 5–7 until the preceding product checkpoint validates continued investment.

The first issue batch should cover only Milestones 0–2 plus the authored one-phrase timing fixture. That is enough to establish the architecture and retire the two largest technical risks without over-planning unvalidated interaction work.

## 17. Recommended first batch

1. Record ADR-0001 for the application architecture.
2. Bootstrap CMake/JUCE application and native tests.
3. Bootstrap React/Vite UI and UI tests.
4. Add CI and documented verification commands.
5. Implement fake/monotonic clock and transport.
6. Implement phrase loop enumeration and fake MIDI scheduler tests.
7. Implement real JUCE MIDI output, selection, stop, and panic behavior.
8. Run and record the ten-minute native timing soak.
9. Embed the packaged React UI in JUCE.
10. Implement the versioned play/stop/transport-state bridge.
11. Stress WebView reload/stall while native MIDI plays.

After this batch passes, plan Milestone 3 against what the bridge spike actually taught us.
