import { describe, expect, it } from 'vitest'

import type { WorldSnapshot } from './bridge/transportBridge'
import { interpolatePhrases } from './worldInterpolation'

function snapshot(sequence: number, x: number, y: number): WorldSnapshot {
  return {
    sequence,
    engineTimeMs: sequence * 33,
    motionPaused: false,
    transport: { playing: true, bpm: 120, bar: 1, beat: 1 },
    phrases: [
      {
        id: 'bass',
        name: 'BASS',
        role: 'bass',
        currentVariantId: 'A',
        pendingVariantId: null,
        pendingVariantApplyBeat: null,
        midiChannel: 1,
        position: { x, y },
        velocity: { x: 0.03, y: 0.04 },
        radius: 0.045,
    mass: 1.3,
    dragged: false,
        playing: true,
      },
    ],
    collisions: [],
    diagnostics: {
      physicsStepCount: sequence * 4,
      physicsCatchUpStepCount: 0,
      physicsCatchUpLimitHitCount: 0,
      collisionContactBeginCount: 0,
      collisionIntentQueuedCount: 0,
      collisionTransitionAppliedCount: 0,
      droppedSnapshotCount: sequence * 3,
      maximumSnapshotIntervalMs: 34,
      commandQueueDepth: 0,
      maximumCommandQueueDepth: 0,
      coalescedMoveCount: 0,
      rejectedCommandCount: 0,
      commandPressureEventCount: 0,
    },
  }
}

describe('world snapshot interpolation', () => {
  it('interpolates positions without changing authoritative phrase state', () => {
    const earlier = snapshot(1, 0.2, 0.3)
    const later = snapshot(2, 0.4, 0.5)
    const [phrase] = interpolatePhrases(earlier, later, 0.5)

    expect(phrase.position.x).toBeCloseTo(0.3)
    expect(phrase.position.y).toBeCloseTo(0.4)
    expect(phrase.speed).toBeCloseTo(0.05)
    expect(phrase.directionRadians).toBeCloseTo(Math.atan2(0.04, 0.03))
    expect(later.phrases[0].position).toEqual({ x: 0.4, y: 0.5 })
  })

  it('clamps interpolation and accepts a phrase missing from the earlier snapshot', () => {
    const earlier = { ...snapshot(1, 0.2, 0.3), phrases: [] }
    const later = snapshot(2, 0.4, 0.5)

    expect(interpolatePhrases(earlier, later, -1)[0].position).toEqual({ x: 0.4, y: 0.5 })
    expect(interpolatePhrases(snapshot(1, 0.2, 0.3), later, 2)[0].position)
      .toEqual({ x: 0.4, y: 0.5 })
  })
})
