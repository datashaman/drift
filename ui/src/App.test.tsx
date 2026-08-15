// @vitest-environment jsdom

import { act, cleanup, fireEvent, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'

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
  type WorldSnapshot,
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

function worldEvent(
  sequence = 1,
  overrides: Partial<WorldSnapshot> = {},
): Extract<BridgeEventEnvelope, { type: 'world.snapshot' }> {
  return {
    protocolVersion,
    messageId: `native-world-${sequence}`,
    type: 'world.snapshot',
    payload: {
      sequence,
      engineTimeMs: 1250,
      transport: { playing: true, bpm: 120, bar: 1, beat: 3.5 },
      diagnostics: {
        physicsStepCount: 150,
        physicsCatchUpStepCount: 3,
        physicsCatchUpLimitHitCount: 0,
        collisionContactBeginCount: 2,
        collisionIntentQueuedCount: 1,
        collisionTransitionAppliedCount: 0,
        droppedSnapshotCount: 112,
        maximumSnapshotIntervalMs: 34.2,
        commandQueueDepth: 2,
        maximumCommandQueueDepth: 7,
        coalescedMoveCount: 12,
        rejectedCommandCount: 1,
        commandPressureEventCount: 0,
      },
      collisions: [
        { firstPhraseId: 'bass', secondPhraseId: 'chords', targetPhraseId: 'chords', touching: false, cooldownRemainingMs: 0 },
        { firstPhraseId: 'bass', secondPhraseId: 'drums', targetPhraseId: 'bass', touching: false, cooldownRemainingMs: 0 },
        { firstPhraseId: 'bass', secondPhraseId: 'melody', targetPhraseId: 'bass', touching: false, cooldownRemainingMs: 0 },
        { firstPhraseId: 'chords', secondPhraseId: 'drums', targetPhraseId: 'drums', touching: false, cooldownRemainingMs: 0 },
        { firstPhraseId: 'chords', secondPhraseId: 'melody', targetPhraseId: 'chords', touching: false, cooldownRemainingMs: 0 },
        { firstPhraseId: 'drums', secondPhraseId: 'melody', targetPhraseId: 'melody', touching: false, cooldownRemainingMs: 0 },
      ],
      phrases: [
        {
          id: 'drums',
          name: 'DRUMS',
          role: 'rhythm',
          currentVariantId: 'A',
          pendingVariantId: null,
          pendingVariantApplyBeat: null,
          midiChannel: 10,
          position: { x: 0.78, y: 0.58 },
          velocity: { x: -0.055, y: -0.035 },
          radius: 0.045,
          mass: 1.1,
          dragged: false,
          playing: true,
        },
        {
          id: 'bass',
          name: 'BASS',
          role: 'bass',
          currentVariantId: 'A',
          pendingVariantId: null,
          pendingVariantApplyBeat: null,
          midiChannel: 1,
          position: { x: 0.2, y: 0.28 },
          velocity: { x: 0.045, y: 0.025 },
          radius: 0.045,
          mass: 1.3,
          dragged: false,
          playing: true,
        },
        {
          id: 'chords',
          name: 'CHORDS',
          role: 'harmony',
          currentVariantId: 'A',
          pendingVariantId: null,
          pendingVariantApplyBeat: null,
          midiChannel: 2,
          position: { x: 0.45, y: 0.76 },
          velocity: { x: 0.035, y: -0.04 },
          radius: 0.045,
          mass: 1.5,
          dragged: false,
          playing: true,
        },
        {
          id: 'melody',
          name: 'MELODY',
          role: 'lead',
          currentVariantId: 'A',
          pendingVariantId: null,
          pendingVariantApplyBeat: null,
          midiChannel: 3,
          position: { x: 0.74, y: 0.2 },
          velocity: { x: -0.04, y: 0.05 },
          radius: 0.045,
          mass: 0.8,
          dragged: false,
          playing: true,
        },
      ],
      ...overrides,
    },
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
            physicsStepCount: 1200,
            physicsCatchUpStepCount: 8,
            physicsCatchUpLimitHitCount: 1,
            collisionContactBeginCount: 2,
            collisionIntentQueuedCount: 1,
            collisionTransitionAppliedCount: 0,
            commandQueueDepth: 2,
            maximumCommandQueueDepth: 7,
            coalescedMoveCount: 12,
            rejectedCommandCount: 1,
            commandPressureEventCount: 0,
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
    let receivedWorldSequence = 0
    bridge.subscribe((event) => {
      if (event.type === 'transport.state') received = event.payload
      if (event.type === 'world.snapshot') receivedWorldSequence = event.payload.sequence
    })

    const play = createCommand({ type: 'transport.play', payload: {} })
    bridge.send(play)
    expect(emitted).toContainEqual({ eventId: 'drift.command', payload: play })
    expect(subscribedEventId).toBe('drift.event')

    eventListener?.({ ...stateEvent({ ...initialTransportState, bar: 99 }), protocolVersion: 2 })
    expect(received.bar).toBe(1)

    eventListener?.(stateEvent({ ...initialTransportState, bar: 4, beat: 1 }))
    expect(received.bar).toBe(4)

    eventListener?.(worldEvent(5))
    expect(receivedWorldSequence).toBe(5)
    const invalidWorld = worldEvent(6)
    invalidWorld.payload.phrases[1] = {
      ...invalidWorld.payload.phrases[0],
      position: { x: 1.5, y: 0.2 },
    }
    eventListener?.(invalidWorld)
    expect(receivedWorldSequence).toBe(5)

    const invalidDiagnostics = worldEvent(7)
    invalidDiagnostics.payload.diagnostics.droppedSnapshotCount = -1
    eventListener?.(invalidDiagnostics)
    expect(receivedWorldSequence).toBe(5)

    const invalidPending = worldEvent(8)
    invalidPending.payload.phrases[1].pendingVariantId = 'B'
    eventListener?.(invalidPending)
    expect(receivedWorldSequence).toBe(5)

    const invalidCollision = worldEvent(9)
    invalidCollision.payload.collisions[0].targetPhraseId = 'unknown'
    eventListener?.(invalidCollision)
    expect(receivedWorldSequence).toBe(5)
  })

  it('renders four phrases from the latest authoritative world snapshot', () => {
    const bridge = new FakeTransportBridge()
    const { container } = render(<App bridge={bridge} />)

    expect(container.querySelectorAll('[data-phrase-id]')).toHaveLength(0)
    act(() => bridge.publish(worldEvent(4)))

    expect(container.querySelectorAll('[data-phrase-id]')).toHaveLength(4)
    expect(screen.getByText(/DRUMS · A/)).toBeTruthy()
    expect(screen.getByText(/BASS · A/)).toBeTruthy()
    expect(screen.getByText(/CHORDS · A/)).toBeTruthy()
    expect(screen.getByText(/MELODY · A/)).toBeTruthy()
    expect(screen.getByText('4 roles / 4 beats')).toBeTruthy()
    expect(container.textContent).toContain('112 snapshots')
    expect(container.textContent).toContain('34.2 ms')
    expect(container.textContent).toContain('2 contacts / 1 queued / 0 applied')
    expect(container.textContent).toContain('bass + chords')
    expect(container.textContent).toContain('→ chords / clear / 0 ms')
    expect(container.textContent).toContain('drums + melody')
    expect(container.textContent).toContain('→ melody / clear / 0 ms')

    const pendingWorld = worldEvent(5)
    pendingWorld.payload.phrases[1].pendingVariantId = 'B'
    pendingWorld.payload.phrases[1].pendingVariantApplyBeat = 4
    act(() => bridge.publish(pendingWorld))
    expect(screen.getByText(/BASS · A → B/)).toBeTruthy()

    const appliedWorld = worldEvent(6)
    appliedWorld.payload.phrases[1].currentVariantId = 'B'
    act(() => bridge.publish(appliedWorld))
    expect(screen.getByText(/BASS · B · playing/)).toBeTruthy()
    expect(container.textContent).not.toContain('BASS · A → B')

    act(() => {
      bridge.publish(worldEvent(4, { phrases: [] }))
    })
    expect(container.querySelectorAll('[data-phrase-id]')).toHaveLength(4)
  })

  it('captures pointer drag intent in normalized native commands', () => {
    const bridge = new FakeTransportBridge()
    render(<App bridge={bridge} />)
    act(() => bridge.publish(worldEvent(4)))

    const pointerLayer = screen.getByLabelText('Drag phrase field') as HTMLDivElement
    vi.spyOn(pointerLayer, 'getBoundingClientRect').mockReturnValue({
      x: 0,
      y: 0,
      left: 0,
      top: 0,
      right: 1000,
      bottom: 500,
      width: 1000,
      height: 500,
      toJSON: () => ({}),
    })
    pointerLayer.setPointerCapture = vi.fn()
    pointerLayer.hasPointerCapture = vi.fn(() => true)
    pointerLayer.releasePointerCapture = vi.fn()

    fireEvent.pointerDown(pointerLayer, { pointerId: 7, clientX: 200, clientY: 140 })
    expect(bridge.commands.slice(-2)).toMatchObject([
      {
        type: 'phrase.dragStart',
        payload: { phraseId: 'bass', dragSessionId: expect.any(String) },
      },
      {
        type: 'phrase.move',
        payload: {
          phraseId: 'bass',
          dragSessionId: expect.any(String),
          position: { x: 0.2, y: 0.28 },
        },
      },
    ])
    const dragStartCommand = bridge.commands.at(-2)
    if (dragStartCommand?.type !== 'phrase.dragStart') {
      throw new Error('Expected phrase.dragStart')
    }
    const dragSessionId = dragStartCommand.payload.dragSessionId
    expect(pointerLayer.setPointerCapture).toHaveBeenCalledWith(7)

    fireEvent.pointerMove(pointerLayer, { pointerId: 7, clientX: 500, clientY: 250 })
    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'phrase.move',
      payload: { phraseId: 'bass', dragSessionId, position: { x: 0.5, y: 0.5 } },
    })

    fireEvent.pointerUp(pointerLayer, { pointerId: 7, clientX: 500, clientY: 250 })
    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'phrase.throw',
      payload: {
        phraseId: 'bass',
        dragSessionId,
        velocity: { x: expect.any(Number), y: expect.any(Number) },
      },
    })
    const throwCommand = bridge.commands.at(-1)
    if (throwCommand?.type !== 'phrase.throw') {
      throw new Error('Expected phrase.throw')
    }
    const velocity = throwCommand.payload.velocity
    expect(Math.hypot(velocity.x, velocity.y)).toBeCloseTo(1.5)
    expect(pointerLayer.releasePointerCapture).toHaveBeenCalledWith(7)

    const selectedWorld = worldEvent(5)
    selectedWorld.payload.phrases[1].dragged = true
    act(() => bridge.publish(selectedWorld))
    expect(screen.getByText(/BASS · A · playing · selected/)).toBeTruthy()
  })

  it('ends an interrupted pointer lifecycle without throwing', () => {
    const bridge = new FakeTransportBridge()
    render(<App bridge={bridge} />)
    act(() => bridge.publish(worldEvent(4)))

    const pointerLayer = screen.getByLabelText('Drag phrase field') as HTMLDivElement
    vi.spyOn(pointerLayer, 'getBoundingClientRect').mockReturnValue({
      x: 0, y: 0, left: 0, top: 0, right: 1000, bottom: 500,
      width: 1000, height: 500, toJSON: () => ({}),
    })
    pointerLayer.setPointerCapture = vi.fn()
    pointerLayer.hasPointerCapture = vi.fn(() => true)
    pointerLayer.releasePointerCapture = vi.fn()

    fireEvent.pointerDown(pointerLayer, { pointerId: 8, clientX: 200, clientY: 140 })
    const dragStartCommand = bridge.commands.at(-2)
    if (dragStartCommand?.type !== 'phrase.dragStart') {
      throw new Error('Expected phrase.dragStart')
    }
    const dragSessionId = dragStartCommand.payload.dragSessionId
    fireEvent.pointerCancel(pointerLayer, { pointerId: 8 })

    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'phrase.dragEnd',
      payload: { phraseId: 'bass', dragSessionId },
    })
    expect(bridge.commands.filter(({ type }) => type === 'phrase.throw')).toHaveLength(0)
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
      bridge.publish(worldEvent(7))
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
      bridge.publish(worldEvent(8))
    })
    expect(screen.getByText('Native engine linked')).toBeTruthy()
    expect(screen.getByText('Playing')).toBeTruthy()
    expect(screen.getByText('07')).toBeTruthy()
    expect(screen.getByText('3.75')).toBeTruthy()
    expect(screen.getByText(/BASS · A/)).toBeTruthy()
  })

  it('disables commands outside the native host', () => {
    render(<App bridge={createTransportBridge(undefined)} />)

    expect((screen.getByRole('button', { name: /play/i }) as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByLabelText('MIDI output') as HTMLSelectElement).disabled).toBe(true)
    expect(screen.getByText('Browser preview')).toBeTruthy()
  })
})
