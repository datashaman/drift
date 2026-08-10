import { describe, expect, it } from 'vitest'

import type { PhraseSnapshot, WorldSnapshot } from './bridge/transportBridge'
import {
  clampPositionForPhrase,
  findPhraseAtPosition,
  normalisePointerPosition,
  shouldClearOptimisticDrag,
} from './dragInteraction'

const bass: PhraseSnapshot = {
  id: 'bass',
  name: 'BASS',
  role: 'bass',
  currentVariantId: 'A',
  midiChannel: 1,
  position: { x: 0.2, y: 0.28 },
  velocity: { x: 0.045, y: 0.025 },
  radius: 0.045,
  mass: 1.3,
  dragged: false,
  playing: true,
}

function snapshot(sequence: number, dragged = false): WorldSnapshot {
  return {
    sequence,
    engineTimeMs: sequence * 33,
    transport: { playing: true, bpm: 120, bar: 1, beat: 1 },
    phrases: [{ ...bass, dragged }],
    diagnostics: {
      physicsStepCount: 0,
      physicsCatchUpStepCount: 0,
      physicsCatchUpLimitHitCount: 0,
      droppedSnapshotCount: 0,
      maximumSnapshotIntervalMs: 0,
      commandQueueDepth: 0,
      maximumCommandQueueDepth: 0,
      coalescedMoveCount: 0,
      rejectedCommandCount: 0,
      commandPressureEventCount: 0,
    },
  }
}

describe('phrase drag interaction', () => {
  it('normalizes pointer coordinates independently of field size and clamps to body bounds', () => {
    expect(normalisePointerPosition(300, 250, {
      left: 100,
      top: 50,
      width: 400,
      height: 400,
    })).toEqual({ x: 0.5, y: 0.5 })
    expect(normalisePointerPosition(1100, 650, {
      left: 100,
      top: 50,
      width: 2000,
      height: 1200,
    })).toEqual({ x: 0.5, y: 0.5 })
    expect(normalisePointerPosition(-100, 900, {
      left: 0,
      top: 0,
      width: 400,
      height: 400,
    })).toEqual({ x: 0, y: 1 })
    expect(clampPositionForPhrase({ x: 0, y: 1 }, 0.045)).toEqual({
      x: 0.045,
      y: 0.955,
    })
  })

  it('hit-tests phrase bodies and reconciles only to a newer accepted snapshot', () => {
    expect(findPhraseAtPosition([bass], { x: 0.21, y: 0.27 })?.id).toBe('bass')
    expect(findPhraseAtPosition([bass], { x: 0.8, y: 0.8 })).toBeUndefined()

    const activeDrag = { phraseId: 'bass', startedSequence: 10 }
    expect(shouldClearOptimisticDrag(activeDrag, snapshot(11, true))).toBe(false)
    expect(shouldClearOptimisticDrag(activeDrag, snapshot(12, false))).toBe(false)
    expect(shouldClearOptimisticDrag(activeDrag, snapshot(13, false))).toBe(true)

    const releasedDrag = {
      phraseId: 'bass',
      startedSequence: 10,
      releasedSequence: 12,
    }
    expect(shouldClearOptimisticDrag(releasedDrag, snapshot(12, true))).toBe(false)
    expect(shouldClearOptimisticDrag(releasedDrag, snapshot(13, false))).toBe(true)
  })
})
