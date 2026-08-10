export const protocolVersion = 1
export const commandEventId = 'drift.command'
export const eventEventId = 'drift.event'

export interface MidiOutputInfo {
  id: string
  name: string
}

export type MidiOutputStatus = 'disconnected' | 'connected' | 'error'

export interface EngineDiagnostics {
  schedulingWatermarkBeat: number
  lateMidiEventCount: number
  maximumEngineLatenessMs: number
  bridgeReconnectCount: number
}

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
  diagnostics: EngineDiagnostics
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
  diagnostics: {
    schedulingWatermarkBeat: 0,
    lateMidiEventCount: 0,
    maximumEngineLatenessMs: 0,
    bridgeReconnectCount: 0,
  },
}

export type TransportCommand =
  | { type: 'app.connect'; payload: Record<string, never> }
  | { type: 'transport.play'; payload: Record<string, never> }
  | { type: 'transport.stop'; payload: Record<string, never> }
  | { type: 'transport.setTempo'; payload: { bpm: number } }
  | { type: 'midi.selectOutput'; payload: { outputId: string } }

export type CommandEnvelope = TransportCommand & {
  protocolVersion: typeof protocolVersion
  messageId: string
}

export interface CommandRejection {
  commandMessageId: string
  code: string
  message: string
}

export type BridgeEventEnvelope =
  | {
      protocolVersion: typeof protocolVersion
      messageId: string
      type: 'app.ready'
      payload: { protocolVersion: typeof protocolVersion }
    }
  | {
      protocolVersion: typeof protocolVersion
      messageId: string
      type: 'transport.state'
      payload: TransportState
    }
  | {
      protocolVersion: typeof protocolVersion
      messageId: string
      type: 'command.rejected'
      payload: CommandRejection
    }

export interface JuceBackend {
  emitEvent(eventId: string, payload: unknown): void
  addEventListener(eventId: string, listener: (payload: unknown) => void): unknown
  removeEventListener(token: unknown): void
}

export interface TransportBridge {
  readonly connected: boolean
  send(command: CommandEnvelope): void
  subscribe(listener: (event: BridgeEventEnvelope) => void): () => void
}

declare global {
  interface Window {
    __JUCE__?: {
      backend?: JuceBackend
    }
  }
}

let commandSequence = 0

export function createCommand(command: TransportCommand): CommandEnvelope {
  commandSequence += 1
  return {
    protocolVersion,
    messageId: `ui-${commandSequence}`,
    ...command,
  } as CommandEnvelope
}

function isMessageId(value: unknown): value is string {
  return typeof value === 'string' && /^[A-Za-z0-9._:-]{1,64}$/.test(value)
}

function isTransportState(payload: unknown): payload is TransportState {
  if (typeof payload !== 'object' || payload === null) return false

  const state = payload as Partial<TransportState>
  const diagnostics = state.diagnostics as Partial<EngineDiagnostics> | undefined
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
    typeof state.midiError === 'string' &&
    typeof diagnostics === 'object' &&
    diagnostics !== null &&
    typeof diagnostics.schedulingWatermarkBeat === 'number' &&
    typeof diagnostics.lateMidiEventCount === 'number' &&
    typeof diagnostics.maximumEngineLatenessMs === 'number' &&
    typeof diagnostics.bridgeReconnectCount === 'number'
  )
}

function isBridgeEventEnvelope(payload: unknown): payload is BridgeEventEnvelope {
  if (typeof payload !== 'object' || payload === null) return false

  const envelope = payload as {
    protocolVersion?: unknown
    messageId?: unknown
    type?: unknown
    payload?: unknown
  }

  if (envelope.protocolVersion !== protocolVersion || !isMessageId(envelope.messageId)) {
    return false
  }

  if (envelope.type === 'app.ready') {
    return (
      typeof envelope.payload === 'object' &&
      envelope.payload !== null &&
      (envelope.payload as { protocolVersion?: unknown }).protocolVersion === protocolVersion
    )
  }

  if (envelope.type === 'transport.state') return isTransportState(envelope.payload)

  if (envelope.type === 'command.rejected') {
    if (typeof envelope.payload !== 'object' || envelope.payload === null) return false
    const rejection = envelope.payload as Partial<CommandRejection>
    return (
      typeof rejection.commandMessageId === 'string' &&
      typeof rejection.code === 'string' &&
      typeof rejection.message === 'string'
    )
  }

  return false
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
      const token = resolvedBackend.addEventListener(eventEventId, (payload) => {
        if (isBridgeEventEnvelope(payload)) listener(payload)
      })

      return () => resolvedBackend.removeEventListener(token)
    },
  }
}
