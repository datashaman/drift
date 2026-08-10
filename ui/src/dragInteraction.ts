import type { PhraseSnapshot, WorldSnapshot } from './bridge/transportBridge'

export interface NormalizedPoint {
  x: number
  y: number
}

export interface PointerSample {
  position: NormalizedPoint
  timeMs: number
}

export const maximumPointerSamples = 8
export const pointerSampleWindowMs = 120
export const maximumThrowSpeed = 1.5
export const stationaryVelocityThreshold = 0.015

export interface PointerBounds {
  left: number
  top: number
  width: number
  height: number
}

export interface OptimisticDragState {
  phraseId: string
  startedSequence: number
  releasedSequence?: number
}

export function normalisePointerPosition(
  clientX: number,
  clientY: number,
  bounds: PointerBounds,
): NormalizedPoint {
  if (bounds.width <= 0 || bounds.height <= 0) return { x: 0.5, y: 0.5 }

  return {
    x: Math.min(1, Math.max(0, (clientX - bounds.left) / bounds.width)),
    y: Math.min(1, Math.max(0, (clientY - bounds.top) / bounds.height)),
  }
}

export function appendPointerSample(
  samples: PointerSample[],
  sample: PointerSample,
): PointerSample[] {
  const earliestTime = sample.timeMs - pointerSampleWindowMs
  return [...samples, sample]
    .filter((candidate) => candidate.timeMs >= earliestTime)
    .slice(-maximumPointerSamples)
}

export function estimateReleaseVelocity(samples: PointerSample[]): NormalizedPoint {
  if (samples.length < 2) return { x: 0, y: 0 }

  const first = samples[0]
  const last = samples[samples.length - 1]
  const elapsedSeconds = (last.timeMs - first.timeMs) / 1000
  if (!Number.isFinite(elapsedSeconds) || elapsedSeconds <= 0) return { x: 0, y: 0 }

  const velocity = {
    x: (last.position.x - first.position.x) / elapsedSeconds,
    y: (last.position.y - first.position.y) / elapsedSeconds,
  }
  const speed = Math.hypot(velocity.x, velocity.y)
  if (!Number.isFinite(speed) || speed < stationaryVelocityThreshold) {
    return { x: 0, y: 0 }
  }
  if (speed <= maximumThrowSpeed) return velocity

  const scale = maximumThrowSpeed / speed
  return { x: velocity.x * scale, y: velocity.y * scale }
}

export function clampPositionForPhrase(
  position: NormalizedPoint,
  radius: number,
): NormalizedPoint {
  const minimum = Math.min(0.5, Math.max(0, radius))
  const maximum = 1 - minimum
  return {
    x: Math.min(maximum, Math.max(minimum, position.x)),
    y: Math.min(maximum, Math.max(minimum, position.y)),
  }
}

export function findPhraseAtPosition(
  phrases: PhraseSnapshot[],
  position: NormalizedPoint,
): PhraseSnapshot | undefined {
  return [...phrases].reverse().find((phrase) => {
    const distance = Math.hypot(
      phrase.position.x - position.x,
      phrase.position.y - position.y,
    )
    return distance <= phrase.radius * 1.35
  })
}

export function shouldClearOptimisticDrag(
  drag: OptimisticDragState,
  snapshot: WorldSnapshot,
): boolean {
  if (snapshot.sequence <= drag.startedSequence) return false

  const authoritative = snapshot.phrases.find((phrase) => phrase.id === drag.phraseId)
  if (!authoritative) return true

  if (drag.releasedSequence !== undefined) {
    return snapshot.sequence > drag.releasedSequence && !authoritative.dragged
  }

  return !authoritative.dragged && snapshot.sequence > drag.startedSequence + 2
}
