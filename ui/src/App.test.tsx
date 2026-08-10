// @vitest-environment jsdom

import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'

import { App } from './App'
import {
  createTransportBridge,
  initialTransportState,
  type JuceBackend,
  type TransportBridge,
  type TransportCommand,
  type TransportState,
} from './bridge/transportBridge'

class FakeTransportBridge implements TransportBridge {
  readonly connected = true
  readonly commands: TransportCommand[] = []
  private listener?: (state: TransportState) => void

  send(command: TransportCommand) {
    this.commands.push(command)
  }

  subscribe(listener: (state: TransportState) => void) {
    this.listener = listener
    return () => {
      this.listener = undefined
    }
  }

  publish(state: TransportState) {
    this.listener?.(state)
  }
}

afterEach(cleanup)

describe('Drift transport interface', () => {
  it('sends play, tempo, and stop intents while rendering authoritative state', () => {
    const bridge = new FakeTransportBridge()
    render(<App bridge={bridge} />)

    expect(bridge.commands).toContainEqual({ type: 'ui.ready' })

    fireEvent.click(screen.getByRole('button', { name: /play/i }))
    expect(bridge.commands).toContainEqual({ type: 'transport.play' })

    const tempoInput = screen.getByLabelText('Tempo in BPM')
    fireEvent.change(tempoInput, { target: { value: '96' } })
    expect(bridge.commands).not.toContainEqual({ type: 'transport.setTempo', bpm: 9 })
    fireEvent.blur(tempoInput)
    expect(bridge.commands).toContainEqual({ type: 'transport.setTempo', bpm: 96 })

    act(() => {
      bridge.publish({
        ...initialTransportState,
        playing: true,
        bpm: 96,
        bar: 3,
        beat: 2.5,
        beatPosition: 9.5,
        scheduledEventCount: 18,
      })
    })

    expect(screen.getByText('Playing')).toBeTruthy()
    expect(screen.getByText('03')).toBeTruthy()
    expect(screen.getByText('2.50')).toBeTruthy()
    expect(screen.getByText('18 events')).toBeTruthy()

    fireEvent.click(screen.getByRole('button', { name: /stop/i }))
    expect(bridge.commands).toContainEqual({ type: 'transport.stop' })
  })

  it('adapts JUCE event channels to the typed transport bridge', () => {
    let stateListener: ((payload: unknown) => void) | undefined
    const emitted: Array<{ eventId: string; payload: unknown }> = []
    const backend: JuceBackend = {
      emitEvent: (eventId, payload) => emitted.push({ eventId, payload }),
      addEventListener: (_eventId, listener) => {
        stateListener = listener
        return 42
      },
      removeEventListener: () => undefined,
    }
    const bridge = createTransportBridge(backend)
    let received = initialTransportState
    bridge.subscribe((state) => {
      received = state
    })

    bridge.send({ type: 'transport.play' })
    expect(emitted).toContainEqual({
      eventId: 'drift.command',
      payload: { type: 'transport.play' },
    })

    stateListener?.({ ...initialTransportState, bar: 4, beat: 1 })
    expect(received.bar).toBe(4)
  })

  it('disables transport controls outside the native host', () => {
    render(<App bridge={createTransportBridge(undefined)} />)

    expect((screen.getByRole('button', { name: /play/i }) as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByText('Browser preview')).toBeTruthy()
  })
})
