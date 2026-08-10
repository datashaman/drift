// @vitest-environment jsdom

import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'

import { App } from './App'
import {
  createCommand,
  createTransportBridge,
  initialTransportState,
  protocolVersion,
  type BridgeEventEnvelope,
  type CommandEnvelope,
  type JuceBackend,
  type TransportBridge,
  type TransportState,
} from './bridge/transportBridge'

function readyEvent(messageId = 'native-ready'): BridgeEventEnvelope {
  return {
    protocolVersion,
    messageId,
    type: 'app.ready',
    payload: { protocolVersion },
  }
}

function stateEvent(
  state: TransportState,
  messageId = 'native-state',
): BridgeEventEnvelope {
  return {
    protocolVersion,
    messageId,
    type: 'transport.state',
    payload: state,
  }
}

class FakeTransportBridge implements TransportBridge {
  readonly connected = true
  readonly commands: CommandEnvelope[] = []
  private listener?: (event: BridgeEventEnvelope) => void

  send(command: CommandEnvelope) {
    this.commands.push(command)
  }

  subscribe(listener: (event: BridgeEventEnvelope) => void) {
    this.listener = listener
    return () => {
      this.listener = undefined
    }
  }

  publish(event: BridgeEventEnvelope) {
    this.listener?.(event)
  }
}

afterEach(cleanup)

describe('Drift bridge interface', () => {
  it('sends versioned commands and renders authoritative state envelopes', () => {
    const bridge = new FakeTransportBridge()
    render(<App bridge={bridge} />)

    expect(bridge.commands[0]).toMatchObject({
      protocolVersion,
      type: 'app.connect',
      payload: {},
    })
    expect(bridge.commands[0].messageId).toMatch(/^ui-\d+$/)

    act(() => bridge.publish(readyEvent()))
    fireEvent.click(screen.getByRole('button', { name: /play/i }))
    expect(bridge.commands.at(-1)).toMatchObject({
      protocolVersion,
      type: 'transport.play',
      payload: {},
    })

    const tempoInput = screen.getByLabelText('Tempo in BPM')
    fireEvent.change(tempoInput, { target: { value: '96' } })
    expect(bridge.commands.some((command) => command.type === 'transport.setTempo')).toBe(false)
    fireEvent.blur(tempoInput)
    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'transport.setTempo',
      payload: { bpm: 96 },
    })

    act(() => {
      bridge.publish(
        stateEvent({
          ...initialTransportState,
          playing: true,
          bpm: 96,
          bar: 3,
          beat: 2.5,
          beatPosition: 9.5,
          scheduledEventCount: 18,
          diagnostics: {
            schedulingWatermarkBeat: 9.7,
            lateMidiEventCount: 0,
            maximumEngineLatenessMs: 0.125,
            bridgeReconnectCount: 4,
          },
        }),
      )
    })

    expect(screen.getByText('Playing')).toBeTruthy()
    expect(screen.getByText('03')).toBeTruthy()
    expect(screen.getByText('2.50')).toBeTruthy()
    expect(screen.getByText('18 events')).toBeTruthy()
    expect(screen.getByText('9.70 beats')).toBeTruthy()
    expect(screen.getByText('0.125 ms')).toBeTruthy()

    fireEvent.click(screen.getByRole('button', { name: /stop/i }))
    expect(bridge.commands.at(-1)).toMatchObject({ type: 'transport.stop', payload: {} })
  })

  it('adapts only valid JUCE event envelopes to the typed bridge', () => {
    let eventListener: ((payload: unknown) => void) | undefined
    let subscribedEventId = ''
    const emitted: Array<{ eventId: string; payload: unknown }> = []
    const backend: JuceBackend = {
      emitEvent: (eventId, payload) => emitted.push({ eventId, payload }),
      addEventListener: (eventId, listener) => {
        subscribedEventId = eventId
        eventListener = listener
        return 42
      },
      removeEventListener: () => undefined,
    }
    const bridge = createTransportBridge(backend)
    let received = initialTransportState
    bridge.subscribe((event) => {
      if (event.type === 'transport.state') received = event.payload
    })

    const play = createCommand({ type: 'transport.play', payload: {} })
    bridge.send(play)
    expect(emitted).toContainEqual({ eventId: 'drift.command', payload: play })
    expect(subscribedEventId).toBe('drift.event')

    eventListener?.({ ...stateEvent({ ...initialTransportState, bar: 99 }), protocolVersion: 2 })
    expect(received.bar).toBe(1)

    eventListener?.(stateEvent({ ...initialTransportState, bar: 4, beat: 1 }))
    expect(received.bar).toBe(4)
  })

  it('selects opaque MIDI IDs and displays structured command rejections', () => {
    const bridge = new FakeTransportBridge()
    render(<App bridge={bridge} />)
    act(() => {
      bridge.publish(readyEvent())
      bridge.publish(
        stateEvent({
          ...initialTransportState,
          midiOutputs: [
            { id: 'runtime:42', name: 'Studio Synth' },
            { id: 'runtime:99', name: 'Loopback Bus' },
          ],
        }),
      )
    })

    fireEvent.change(screen.getByLabelText('MIDI output'), {
      target: { value: 'runtime:42' },
    })
    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'midi.selectOutput',
      payload: { outputId: 'runtime:42' },
    })

    act(() => {
      bridge.publish({
        protocolVersion,
        messageId: 'native-rejection',
        type: 'command.rejected',
        payload: {
          commandMessageId: bridge.commands.at(-1)?.messageId ?? '',
          code: 'unknown_id',
          message: 'The MIDI outputId is not currently available',
        },
      })
    })

    expect(screen.getByRole('alert').textContent).toContain('unknown_id')
    expect(screen.getByRole('alert').textContent).toContain('not currently available')
  })

  it('reconnects after reload and restores ongoing authoritative state', () => {
    const bridge = new FakeTransportBridge()
    const firstLoad = render(<App bridge={bridge} />)
    act(() => {
      bridge.publish(readyEvent('native-ready-1'))
      bridge.publish(
        stateEvent(
          { ...initialTransportState, playing: true, bar: 7, beat: 3.25 },
          'native-state-1',
        ),
      )
    })
    expect(screen.getByText('Playing')).toBeTruthy()
    firstLoad.unmount()

    render(<App bridge={bridge} />)
    expect(bridge.commands.filter((command) => command.type === 'app.connect')).toHaveLength(2)

    act(() => {
      bridge.publish(readyEvent('native-ready-2'))
      bridge.publish(
        stateEvent(
          { ...initialTransportState, playing: true, bar: 7, beat: 3.75 },
          'native-state-2',
        ),
      )
    })
    expect(screen.getByText('Native engine linked')).toBeTruthy()
    expect(screen.getByText('Playing')).toBeTruthy()
    expect(screen.getByText('07')).toBeTruthy()
    expect(screen.getByText('3.75')).toBeTruthy()
  })

  it('disables commands outside the native host', () => {
    render(<App bridge={createTransportBridge(undefined)} />)

    expect((screen.getByRole('button', { name: /play/i }) as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByLabelText('MIDI output') as HTMLSelectElement).disabled).toBe(true)
    expect(screen.getByText('Browser preview')).toBeTruthy()
  })
})
