import { useEffect, useRef } from 'react'
import type { PointerEvent as ReactPointerEvent } from 'react'

import type {
  Application,
  Container,
  Graphics,
  Text as PixiText,
} from 'pixi.js'

import type { PhraseSnapshot, WorldSnapshot } from './bridge/transportBridge'
import {
  appendPointerSample,
  clampPositionForPhrase,
  estimateReleaseVelocity,
  findPhraseAtPosition,
  normalisePointerPosition,
  shouldClearOptimisticDrag,
  type NormalizedPoint,
  type PointerSample,
} from './dragInteraction'
import { interpolatePhrases } from './worldInterpolation'

interface PhraseWorldProps {
  snapshot: WorldSnapshot
  onDragStart(phraseId: string, dragSessionId: string): void
  onDragMove(
    phraseId: string,
    dragSessionId: string,
    position: NormalizedPoint,
  ): void
  onDragEnd(phraseId: string, dragSessionId: string): void
  onThrow(
    phraseId: string,
    dragSessionId: string,
    velocity: NormalizedPoint,
  ): void
}

interface SceneNode {
  container: Container
  core: Graphics
  direction: Graphics
  label: PixiText
  selection: Graphics
}

interface OptimisticDrag {
  phraseId: string
  dragSessionId: string
  pointerId: number
  position: NormalizedPoint
  samples: PointerSample[]
  startedSequence: number
  releasedSequence?: number
}

let dragSessionSequence = 0

function nextDragSessionId() {
  dragSessionSequence += 1
  return `drag-${dragSessionSequence}`
}

const phraseColours: Record<string, number> = {
  bass: 0xf38b73,
  melody: 0x6ed0c4,
  chords: 0xa49be8,
  drums: 0xf0c96b,
}

function colourForPhrase(phrase: PhraseSnapshot) {
  return phraseColours[phrase.id] ?? 0xeef1e8
}

function variantLabel(phrase: PhraseSnapshot) {
  return phrase.pendingVariantId
    ? `${phrase.currentVariantId} → ${phrase.pendingVariantId}`
    : phrase.currentVariantId
}

export function PhraseWorld({
  snapshot,
  onDragStart,
  onDragMove,
  onDragEnd,
  onThrow,
}: PhraseWorldProps) {
  const hostRef = useRef<HTMLDivElement>(null)
  const optimisticDragRef = useRef<OptimisticDrag | undefined>(undefined)
  const transitionRef = useRef({
    previous: snapshot,
    current: snapshot,
    startedAtMs: 0,
    durationMs: 33,
  })

  useEffect(() => {
    const transition = transitionRef.current
    const optimistic = optimisticDragRef.current
    const reconcileOptimistic = optimistic
      ? shouldClearOptimisticDrag(optimistic, snapshot)
      : false
    const previous = reconcileOptimistic && optimistic
      ? {
          ...snapshot,
          phrases: snapshot.phrases.map((phrase) =>
            phrase.id === optimistic.phraseId
              ? { ...phrase, position: optimistic.position }
              : phrase,
          ),
        }
      : transition.current.sequence > 0
        ? transition.current
        : snapshot

    transitionRef.current = {
      previous,
      current: snapshot,
      startedAtMs: performance.now(),
      durationMs: Math.min(
        100,
        Math.max(16, snapshot.engineTimeMs - transition.current.engineTimeMs),
      ),
    }
    if (reconcileOptimistic) {
      optimisticDragRef.current = undefined
    }
  }, [snapshot])

  useEffect(() => {
    if (import.meta.env.MODE === 'test') return

    const host = hostRef.current
    if (!host) return

    let cancelled = false
    let application: Application | undefined

    const initialise = async () => {
      const { Application, Container, Graphics, Text } = await import('pixi.js')
      const app = new Application()
      await app.init({
        resizeTo: host,
        backgroundAlpha: 0,
        antialias: true,
        autoDensity: true,
        resolution: window.devicePixelRatio,
        preference: 'webgl',
      })

      if (cancelled) {
        app.destroy(true)
        return
      }

      application = app
      app.canvas.className = 'phrase-world-canvas'
      host.appendChild(app.canvas)
      const nodes = new Map<string, SceneNode>()

      const makeNode = (phrase: PhraseSnapshot): SceneNode => {
        const colour = colourForPhrase(phrase)
        const container = new Container()
        const core = new Graphics()
          .circle(0, 0, 1)
          .fill({ color: 0x172448 })
          .stroke({ color: colour, width: 0.22 })
        const direction = new Graphics()
          .moveTo(0, 0)
          .lineTo(1, 0)
          .lineTo(0.76, -0.16)
          .moveTo(1, 0)
          .lineTo(0.76, 0.16)
          .stroke({ color: colour, width: 0.08, alpha: 0.62 })
        const selection = new Graphics()
          .circle(0, 0, 1)
          .stroke({ color: 0xeef1e8, width: 0.1, alpha: 0.82 })
        const label = new Text({
          text: `${phrase.name} · ${variantLabel(phrase)}`,
          style: {
            fill: colour,
            fontFamily: 'SFMono-Regular, Consolas, monospace',
            fontSize: 11,
            letterSpacing: 1.1,
          },
        })

        container.addChild(selection, direction, core, label)
        app.stage.addChild(container)
        const node = { container, core, direction, label, selection }
        nodes.set(phrase.id, node)
        return node
      }

      const renderFrame = () => {
        const transition = transitionRef.current
        const elapsed = performance.now() - transition.startedAtMs
        const amount = transition.durationMs <= 0 ? 1 : elapsed / transition.durationMs
        const phrases = interpolatePhrases(transition.previous, transition.current, amount)
        const activeIds = new Set(phrases.map((phrase) => phrase.id))

        for (const [phraseId, node] of nodes) {
          if (activeIds.has(phraseId)) continue
          app.stage.removeChild(node.container)
          node.container.destroy({ children: true })
          nodes.delete(phraseId)
        }

        const minimumDimension = Math.min(app.screen.width, app.screen.height)
        for (const phrase of phrases) {
          const node = nodes.get(phrase.id) ?? makeNode(phrase)
          const radiusPixels = phrase.radius * minimumDimension
          const optimistic = optimisticDragRef.current
          const isOptimisticallyDragged = optimistic?.phraseId === phrase.id
          const position = isOptimisticallyDragged ? optimistic.position : phrase.position
          node.container.position.set(
            position.x * app.screen.width,
            position.y * app.screen.height,
          )
          node.container.alpha = phrase.playing ? 1 : 0.62
          node.core.scale.set(radiusPixels)
          node.direction.rotation = phrase.directionRadians
          node.direction.scale.set(radiusPixels * 2.1)
          node.selection.visible = phrase.dragged || isOptimisticallyDragged
          node.selection.scale.set(radiusPixels * 1.28)
          node.label.position.set(radiusPixels + 10, -7)
          node.label.text = `${phrase.name} · ${variantLabel(phrase)}`
        }
      }

      app.ticker.add(renderFrame)
    }

    void initialise()

    return () => {
      cancelled = true
      application?.destroy(true, { children: true })
    }
  }, [])

  const pointerPosition = (event: ReactPointerEvent<HTMLDivElement>) =>
    normalisePointerPosition(
      event.clientX,
      event.clientY,
      event.currentTarget.getBoundingClientRect(),
    )

  const handlePointerDown = (event: ReactPointerEvent<HTMLDivElement>) => {
    if (optimisticDragRef.current) return

    const position = pointerPosition(event)
    const phrase = findPhraseAtPosition(snapshot.phrases, position)
    if (!phrase) return

    event.preventDefault()
    event.currentTarget.setPointerCapture(event.pointerId)
    const clamped = clampPositionForPhrase(position, phrase.radius)
    const dragSessionId = nextDragSessionId()
    optimisticDragRef.current = {
      phraseId: phrase.id,
      dragSessionId,
      pointerId: event.pointerId,
      position: clamped,
      samples: [{ position: clamped, timeMs: event.timeStamp }],
      startedSequence: snapshot.sequence,
    }
    onDragStart(phrase.id, dragSessionId)
    onDragMove(phrase.id, dragSessionId, clamped)
  }

  const handlePointerMove = (event: ReactPointerEvent<HTMLDivElement>) => {
    const optimistic = optimisticDragRef.current
    if (!optimistic || optimistic.pointerId !== event.pointerId
        || optimistic.releasedSequence !== undefined) return

    const phrase = snapshot.phrases.find((candidate) => candidate.id === optimistic.phraseId)
    if (!phrase) return

    event.preventDefault()
    const clamped = clampPositionForPhrase(pointerPosition(event), phrase.radius)
    optimistic.position = clamped
    optimistic.samples = appendPointerSample(optimistic.samples, {
      position: clamped,
      timeMs: event.timeStamp,
    })
    onDragMove(phrase.id, optimistic.dragSessionId, clamped)
  }

  const endPointerDrag = (event: ReactPointerEvent<HTMLDivElement>) => {
    const optimistic = optimisticDragRef.current
    if (!optimistic || optimistic.pointerId !== event.pointerId
        || optimistic.releasedSequence !== undefined) return

    const phrase = snapshot.phrases.find((candidate) => candidate.id === optimistic.phraseId)
    if (phrase) {
      const clamped = clampPositionForPhrase(pointerPosition(event), phrase.radius)
      optimistic.position = clamped
      optimistic.samples = appendPointerSample(optimistic.samples, {
        position: clamped,
        timeMs: event.timeStamp,
      })
      onDragMove(phrase.id, optimistic.dragSessionId, clamped)
      onThrow(
        phrase.id,
        optimistic.dragSessionId,
        estimateReleaseVelocity(optimistic.samples),
      )
    }
    optimistic.releasedSequence = snapshot.sequence
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId)
    }
  }

  const cancelPointerDrag = (event: ReactPointerEvent<HTMLDivElement>) => {
    const optimistic = optimisticDragRef.current
    if (!optimistic || optimistic.pointerId !== event.pointerId
        || optimistic.releasedSequence !== undefined) return

    onDragEnd(optimistic.phraseId, optimistic.dragSessionId)
    optimistic.releasedSequence = snapshot.sequence
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId)
    }
  }

  return (
    <div className="phrase-world" ref={hostRef}>
      <div
        aria-label="Drag phrase field"
        className="phrase-world-pointer-layer"
        onPointerCancel={cancelPointerDrag}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={endPointerDrag}
      />
      <ul className="phrase-world-accessibility" aria-label="Phrase world">
        {snapshot.phrases.map((phrase) => (
          <li data-phrase-id={phrase.id} key={phrase.id}>
            {phrase.name} · {variantLabel(phrase)} · {phrase.playing ? 'playing' : 'stopped'} · {phrase.dragged ? 'selected' : 'free'}
          </li>
        ))}
      </ul>
    </div>
  )
}
