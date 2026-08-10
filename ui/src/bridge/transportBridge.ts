export const commandEventId = 'drift.command'
export const stateEventId = 'drift.state'

export type TransportCommand =
  | { type: 'ui.ready' }
  | { type: 'transport.play' }
  | { type: 'transport.stop' }
  | { type: 'transport.setTempo'; bpm: number }
  | { type: 'midi.selectOutput'; outputId: string }

export interface MidiOutputInfo {
  id: string
  name: string
}

export type MidiOutputStatus = 'disconnected' | 'connected' | 'error'

export interface TransportState {
  playing: boolean
  bpm: number
  beatPosition: number
  bar: number
  beat: number
  scheduledEventCount: number
  midiOutputs: MidiOutputInfo[]
  selectedMidiOutputId: string
  midiStatus: MidiOutputStatus
  midiError: string
}

export const initialTransportState: TransportState = {
  playing: false,
  bpm: 120,
  beatPosition: 0,
  bar: 1,
  beat: 1,
  scheduledEventCount: 0,
  midiOutputs: [],
  selectedMidiOutputId: '',
  midiStatus: 'disconnected',
  midiError: '',
}

export interface JuceBackend {
  emitEvent(eventId: string, payload: unknown): void
  addEventListener(eventId: string, listener: (payload: unknown) => void): unknown
  removeEventListener(token: unknown): void
}

export interface TransportBridge {
  readonly connected: boolean
  send(command: TransportCommand): void
  subscribe(listener: (state: TransportState) => void): () => void
}

declare global {
  interface Window {
    __JUCE__?: {
      backend?: JuceBackend
    }
  }
}

function isTransportState(payload: unknown): payload is TransportState {
  if (typeof payload !== 'object' || payload === null) return false

  const state = payload as Partial<TransportState>
  const midiOutputsAreValid =
    Array.isArray(state.midiOutputs) &&
    state.midiOutputs.every(
      (output) =>
        typeof output === 'object' &&
        output !== null &&
        typeof output.id === 'string' &&
        typeof output.name === 'string',
    )

  return (
    typeof state.playing === 'boolean' &&
    typeof state.bpm === 'number' &&
    typeof state.beatPosition === 'number' &&
    typeof state.bar === 'number' &&
    typeof state.beat === 'number' &&
    typeof state.scheduledEventCount === 'number' &&
    midiOutputsAreValid &&
    typeof state.selectedMidiOutputId === 'string' &&
    (state.midiStatus === 'disconnected' ||
      state.midiStatus === 'connected' ||
      state.midiStatus === 'error') &&
    typeof state.midiError === 'string'
  )
}

export function createTransportBridge(backend?: JuceBackend): TransportBridge {
  const resolvedBackend =
    backend ?? (typeof window === 'undefined' ? undefined : window.__JUCE__?.backend)

  if (!resolvedBackend) {
    return {
      connected: false,
      send: () => undefined,
      subscribe: () => () => undefined,
    }
  }

  return {
    connected: true,
    send: (command) => resolvedBackend.emitEvent(commandEventId, command),
    subscribe: (listener) => {
      const token = resolvedBackend.addEventListener(stateEventId, (payload) => {
        if (isTransportState(payload)) listener(payload)
      })

      return () => resolvedBackend.removeEventListener(token)
    },
  }
}
