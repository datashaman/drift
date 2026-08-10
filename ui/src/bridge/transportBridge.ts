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
  physicsStepCount: number
  physicsCatchUpStepCount: number
  physicsCatchUpLimitHitCount: number
  commandQueueDepth: number
  maximumCommandQueueDepth: number
  coalescedMoveCount: number
  rejectedCommandCount: number
  commandPressureEventCount: number
}

export type PhraseRole = 'rhythm' | 'bass' | 'harmony' | 'lead'

export interface PhraseSnapshot {
  id: string
  name: string
  role: PhraseRole
  currentVariantId: string
  midiChannel: number
  position: { x: number; y: number }
  velocity: { x: number; y: number }
  radius: number
  mass: number
  dragged: boolean
  playing: boolean
}

export interface WorldDiagnostics {
  physicsStepCount: number
  physicsCatchUpStepCount: number
  physicsCatchUpLimitHitCount: number
  droppedSnapshotCount: number
  maximumSnapshotIntervalMs: number
  commandQueueDepth: number
  maximumCommandQueueDepth: number
  coalescedMoveCount: number
  rejectedCommandCount: number
  commandPressureEventCount: number
}

export interface WorldSnapshot {
  sequence: number
  engineTimeMs: number
  transport: {
    playing: boolean
    bpm: number
    bar: number
    beat: number
  }
  phrases: PhraseSnapshot[]
  diagnostics: WorldDiagnostics
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
    physicsStepCount: 0,
    physicsCatchUpStepCount: 0,
    physicsCatchUpLimitHitCount: 0,
    commandQueueDepth: 0,
    maximumCommandQueueDepth: 0,
    coalescedMoveCount: 0,
    rejectedCommandCount: 0,
    commandPressureEventCount: 0,
  },
}

export const initialWorldSnapshot: WorldSnapshot = {
  sequence: 0,
  engineTimeMs: 0,
  transport: { playing: false, bpm: 120, bar: 1, beat: 1 },
  phrases: [],
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

export type TransportCommand =
  | { type: 'app.connect'; payload: Record<string, never> }
  | { type: 'transport.play'; payload: Record<string, never> }
  | { type: 'transport.stop'; payload: Record<string, never> }
  | { type: 'transport.setTempo'; payload: { bpm: number } }
  | { type: 'midi.selectOutput'; payload: { outputId: string } }
  | {
      type: 'phrase.dragStart'
      payload: { phraseId: string; dragSessionId: string }
    }
  | {
      type: 'phrase.move'
      payload: {
        phraseId: string
        dragSessionId: string
        position: { x: number; y: number }
      }
    }
  | {
      type: 'phrase.dragEnd'
      payload: { phraseId: string; dragSessionId: string }
    }
  | {
      type: 'phrase.throw'
      payload: {
        phraseId: string
        dragSessionId: string
        velocity: { x: number; y: number }
      }
    }

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
      type: 'world.snapshot'
      payload: WorldSnapshot
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

function isFiniteNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value)
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
    typeof diagnostics.bridgeReconnectCount === 'number' &&
    typeof diagnostics.physicsStepCount === 'number' &&
    typeof diagnostics.physicsCatchUpStepCount === 'number' &&
    typeof diagnostics.physicsCatchUpLimitHitCount === 'number' &&
    typeof diagnostics.commandQueueDepth === 'number' &&
    typeof diagnostics.maximumCommandQueueDepth === 'number' &&
    typeof diagnostics.coalescedMoveCount === 'number' &&
    typeof diagnostics.rejectedCommandCount === 'number' &&
    typeof diagnostics.commandPressureEventCount === 'number'
  )
}

function isWorldSnapshot(payload: unknown): payload is WorldSnapshot {
  if (typeof payload !== 'object' || payload === null) return false

  const snapshot = payload as Partial<WorldSnapshot>
  const transport = snapshot.transport as Partial<WorldSnapshot['transport']> | undefined
  const diagnostics = snapshot.diagnostics as Partial<WorldDiagnostics> | undefined
  const validRoles: PhraseRole[] = ['rhythm', 'bass', 'harmony', 'lead']

  if (
    !Number.isSafeInteger(snapshot.sequence) ||
    (snapshot.sequence ?? 0) < 1 ||
    !isFiniteNumber(snapshot.engineTimeMs) ||
    snapshot.engineTimeMs < 0 ||
    typeof transport !== 'object' ||
    transport === null ||
    typeof transport.playing !== 'boolean' ||
    !isFiniteNumber(transport.bpm) ||
    !Number.isSafeInteger(transport.bar) ||
    !isFiniteNumber(transport.beat) ||
    !Array.isArray(snapshot.phrases) ||
    typeof diagnostics !== 'object' ||
    diagnostics === null ||
    !Number.isSafeInteger(diagnostics.physicsStepCount) ||
    (diagnostics.physicsStepCount ?? -1) < 0 ||
    !Number.isSafeInteger(diagnostics.physicsCatchUpStepCount) ||
    (diagnostics.physicsCatchUpStepCount ?? -1) < 0 ||
    !Number.isSafeInteger(diagnostics.physicsCatchUpLimitHitCount) ||
    (diagnostics.physicsCatchUpLimitHitCount ?? -1) < 0 ||
    !Number.isSafeInteger(diagnostics.droppedSnapshotCount) ||
    (diagnostics.droppedSnapshotCount ?? -1) < 0 ||
    !isFiniteNumber(diagnostics.maximumSnapshotIntervalMs) ||
    diagnostics.maximumSnapshotIntervalMs < 0 ||
    !Number.isSafeInteger(diagnostics.commandQueueDepth) ||
    (diagnostics.commandQueueDepth ?? -1) < 0 ||
    !Number.isSafeInteger(diagnostics.maximumCommandQueueDepth) ||
    (diagnostics.maximumCommandQueueDepth ?? -1) < 0 ||
    !Number.isSafeInteger(diagnostics.coalescedMoveCount) ||
    (diagnostics.coalescedMoveCount ?? -1) < 0 ||
    !Number.isSafeInteger(diagnostics.rejectedCommandCount) ||
    (diagnostics.rejectedCommandCount ?? -1) < 0 ||
    !Number.isSafeInteger(diagnostics.commandPressureEventCount) ||
    (diagnostics.commandPressureEventCount ?? -1) < 0
  ) {
    return false
  }

  const phraseIds = new Set<string>()
  return snapshot.phrases.every((phrase) => {
    if (typeof phrase !== 'object' || phrase === null) return false

    const candidate = phrase as Partial<PhraseSnapshot>
    const position = candidate.position as Partial<PhraseSnapshot['position']> | undefined
    const velocity = candidate.velocity as Partial<PhraseSnapshot['velocity']> | undefined
    const valid =
      isMessageId(candidate.id) &&
      typeof candidate.name === 'string' &&
      candidate.name.length > 0 &&
      validRoles.includes(candidate.role as PhraseRole) &&
      isMessageId(candidate.currentVariantId) &&
      Number.isSafeInteger(candidate.midiChannel) &&
      (candidate.midiChannel ?? 0) >= 1 &&
      (candidate.midiChannel ?? 0) <= 16 &&
      typeof candidate.playing === 'boolean' &&
      typeof position === 'object' &&
      position !== null &&
      isFiniteNumber(position.x) &&
      isFiniteNumber(position.y) &&
      position.x >= 0 &&
      position.x <= 1 &&
      position.y >= 0 &&
      position.y <= 1 &&
      typeof velocity === 'object' &&
      velocity !== null &&
      isFiniteNumber(velocity.x) &&
      isFiniteNumber(velocity.y) &&
      isFiniteNumber(candidate.radius) &&
      candidate.radius > 0 &&
      candidate.radius <= 0.5 &&
      position.x >= candidate.radius &&
      position.x <= 1 - candidate.radius &&
      position.y >= candidate.radius &&
      position.y <= 1 - candidate.radius &&
      isFiniteNumber(candidate.mass) &&
      candidate.mass > 0 &&
      typeof candidate.dragged === 'boolean'

    if (!valid || phraseIds.has(candidate.id as string)) return false
    phraseIds.add(candidate.id as string)
    return true
  })
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

  if (envelope.type === 'world.snapshot') return isWorldSnapshot(envelope.payload)

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
