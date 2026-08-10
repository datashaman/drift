import type { PhraseSnapshot, WorldSnapshot } from './bridge/transportBridge'

export interface InterpolatedPhrase extends PhraseSnapshot {
  directionRadians: number
  speed: number
}

export function interpolatePhrases(
  previous: WorldSnapshot,
  current: WorldSnapshot,
  interpolation: number,
): InterpolatedPhrase[] {
  const amount = Math.min(1, Math.max(0, interpolation))
  const previousById = new Map(previous.phrases.map((phrase) => [phrase.id, phrase]))

  return current.phrases.map((phrase) => {
    const earlier = previousById.get(phrase.id) ?? phrase
    const x = earlier.position.x + (phrase.position.x - earlier.position.x) * amount
    const y = earlier.position.y + (phrase.position.y - earlier.position.y) * amount

    return {
      ...phrase,
      position: { x, y },
      directionRadians: Math.atan2(phrase.velocity.y, phrase.velocity.x),
      speed: Math.hypot(phrase.velocity.x, phrase.velocity.y),
    }
  })
}
