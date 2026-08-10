import { useEffect, useRef } from 'react'

import type {
  Application,
  Container,
  Graphics,
  Text as PixiText,
} from 'pixi.js'

import type { PhraseSnapshot, WorldSnapshot } from './bridge/transportBridge'
import { interpolatePhrases } from './worldInterpolation'

interface PhraseWorldProps {
  snapshot: WorldSnapshot
}

interface SceneNode {
  container: Container
  core: Graphics
  direction: Graphics
  label: PixiText
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

export function PhraseWorld({ snapshot }: PhraseWorldProps) {
  const hostRef = useRef<HTMLDivElement>(null)
  const transitionRef = useRef({
    previous: snapshot,
    current: snapshot,
    startedAtMs: 0,
    durationMs: 33,
  })

  useEffect(() => {
    const transition = transitionRef.current
    transitionRef.current = {
      previous: transition.current.sequence > 0 ? transition.current : snapshot,
      current: snapshot,
      startedAtMs: performance.now(),
      durationMs: Math.min(
        100,
        Math.max(16, snapshot.engineTimeMs - transition.current.engineTimeMs),
      ),
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
        const label = new Text({
          text: `${phrase.name} · ${phrase.currentVariantId}`,
          style: {
            fill: colour,
            fontFamily: 'SFMono-Regular, Consolas, monospace',
            fontSize: 11,
            letterSpacing: 1.1,
          },
        })

        container.addChild(direction, core, label)
        app.stage.addChild(container)
        const node = { container, core, direction, label }
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
          node.container.position.set(
            phrase.position.x * app.screen.width,
            phrase.position.y * app.screen.height,
          )
          node.container.alpha = phrase.playing ? 1 : 0.62
          node.core.scale.set(radiusPixels)
          node.direction.rotation = phrase.directionRadians
          node.direction.scale.set(radiusPixels * 2.1)
          node.label.position.set(radiusPixels + 10, -7)
          node.label.text = `${phrase.name} · ${phrase.currentVariantId}`
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

  return (
    <div className="phrase-world" ref={hostRef}>
      <ul className="phrase-world-accessibility" aria-label="Phrase world">
        {snapshot.phrases.map((phrase) => (
          <li data-phrase-id={phrase.id} key={phrase.id}>
            {phrase.name} · {phrase.currentVariantId} · {phrase.playing ? 'playing' : 'stopped'}
          </li>
        ))}
      </ul>
    </div>
  )
}
