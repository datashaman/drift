import { useEffect, useMemo, useState } from 'react'

import { PhraseWorld } from './PhraseWorld'

import {
  createCommand,
  createTransportBridge,
  initialTransportState,
  initialWorldSnapshot,
  type CommandRejection,
  type TransportBridge,
} from './bridge/transportBridge'

interface AppProps {
  bridge?: TransportBridge
}

export function App({ bridge }: AppProps) {
  const transportBridge = useMemo(() => bridge ?? createTransportBridge(), [bridge])
  const [transport, setTransport] = useState(initialTransportState)
  const [worldSnapshot, setWorldSnapshot] = useState(initialWorldSnapshot)
  const [tempoDraft, setTempoDraft] = useState(String(initialTransportState.bpm))
  const [bridgeReady, setBridgeReady] = useState(false)
  const [lastRejection, setLastRejection] = useState<CommandRejection>()

  useEffect(() => {
    const unsubscribe = transportBridge.subscribe((event) => {
      if (event.type === 'app.ready') setBridgeReady(true)
      if (event.type === 'transport.state') setTransport(event.payload)
      if (event.type === 'world.snapshot') {
        setWorldSnapshot((current) =>
          event.payload.sequence > current.sequence ? event.payload : current,
        )
      }
      if (event.type === 'command.rejected') setLastRejection(event.payload)
    })
    transportBridge.send(createCommand({ type: 'app.connect', payload: {} }))
    return unsubscribe
  }, [transportBridge])

  useEffect(() => {
    setTempoDraft(String(Math.round(transport.bpm)))
  }, [transport.bpm])

  const toggleTransport = () => {
    transportBridge.send(createCommand({
      type: transport.playing ? 'transport.stop' : 'transport.play',
      payload: {},
    }))
  }

  const commitTempo = () => {
    const bpm = Number(tempoDraft)
    if (Number.isFinite(bpm) && bpm >= 40 && bpm <= 240) {
      transportBridge.send(createCommand({ type: 'transport.setTempo', payload: { bpm } }))
    } else {
      setTempoDraft(String(Math.round(transport.bpm)))
    }
  }

  const selectedMidiOutput = transport.midiOutputs.find(
    (output) => output.id === transport.selectedMidiOutputId,
  )

  const midiSummary =
    transport.midiStatus === 'connected' && selectedMidiOutput
      ? selectedMidiOutput.name
      : transport.midiStatus === 'error'
        ? 'Output error'
        : 'Disconnected'

  const controlsReady = transportBridge.connected && bridgeReady

  return (
    <main className={`shell ${transport.playing ? 'is-playing' : 'is-stopped'}`}>
      <header className="masthead">
        <div>
          <p className="eyebrow">Spatial phrase sequencer</p>
          <h1>Drift</h1>
        </div>

        <div className="transport" aria-label="Transport controls">
          <button
            className="transport-button"
            disabled={!controlsReady}
            onClick={toggleTransport}
            type="button"
          >
            <span aria-hidden="true">{transport.playing ? '■' : '▶'}</span>
            {transport.playing ? 'Stop' : 'Play'}
          </button>

          <label className="tempo-control">
            <span>Tempo</span>
            <input
              aria-label="Tempo in BPM"
              disabled={!controlsReady}
              max="240"
              min="40"
              onBlur={commitTempo}
              onChange={(event) => setTempoDraft(event.target.value)}
              onKeyDown={(event) => {
                if (event.key === 'Enter') event.currentTarget.blur()
              }}
              step="1"
              type="number"
              value={tempoDraft}
            />
            <span>BPM</span>
          </label>

          <label className="midi-output-control">
            <span>MIDI output</span>
            <select
              aria-label="MIDI output"
              disabled={!controlsReady || transport.midiOutputs.length === 0}
              onChange={(event) =>
                transportBridge.send(createCommand({
                  type: 'midi.selectOutput',
                  payload: { outputId: event.target.value },
                }))
              }
              value={transport.selectedMidiOutputId}
            >
              <option value="">
                {transport.midiOutputs.length === 0 ? 'No outputs found' : 'Choose output'}
              </option>
              {transport.midiOutputs.map((output) => (
                <option key={output.id} value={output.id}>
                  {output.name}
                </option>
              ))}
            </select>
          </label>
        </div>
      </header>

      <section className="field" aria-label="Phrase field preview">
        <div className="field-grid" aria-hidden="true" />
        <div className="relationship relationship--one" aria-hidden="true" />
        <div className="relationship relationship--two" aria-hidden="true" />

        <PhraseWorld
          onDragEnd={(phraseId, dragSessionId) =>
            transportBridge.send(createCommand({
              type: 'phrase.dragEnd',
              payload: { phraseId, dragSessionId },
            }))
          }
          onDragMove={(phraseId, dragSessionId, position) =>
            transportBridge.send(createCommand({
              type: 'phrase.move',
              payload: { phraseId, dragSessionId, position },
            }))
          }
          onDragStart={(phraseId, dragSessionId) =>
            transportBridge.send(createCommand({
              type: 'phrase.dragStart',
              payload: { phraseId, dragSessionId },
            }))
          }
          onThrow={(phraseId, dragSessionId, velocity) =>
            transportBridge.send(createCommand({
              type: 'phrase.throw',
              payload: { phraseId, dragSessionId, velocity },
            }))
          }
          snapshot={worldSnapshot}
        />

        <p className="field-note">
          {worldSnapshot.phrases.length === 0
            ? 'Awaiting authoritative world snapshot.'
            : transport.midiStatus === 'connected'
              ? 'Four-phrase composition online.'
              : 'Select a MIDI output to hear the four-phrase composition.'}
        </p>
      </section>

      <aside className="status" aria-label="Application status" aria-live="polite">
        <div className="status-heading">
          <span className="status-pulse" aria-hidden="true" />
          <span>
            {!transportBridge.connected
              ? 'Browser preview'
              : bridgeReady
                ? 'Native engine linked'
                : 'Connecting to engine'}
          </span>
        </div>

        <div className="musical-position">
          <span>BAR</span>
          <strong>{String(transport.bar).padStart(2, '0')}</strong>
          <span>BEAT</span>
          <strong>{transport.beat.toFixed(2)}</strong>
        </div>

        <dl>
          <div>
            <dt>Transport</dt>
            <dd>{transport.playing ? 'Playing' : 'Stopped'}</dd>
          </div>
          <div>
            <dt>Phrase</dt>
            <dd>4 roles / 4 beats</dd>
          </div>
          <div>
            <dt>Scheduled</dt>
            <dd>{transport.scheduledEventCount} events</dd>
          </div>
          <div>
            <dt>MIDI</dt>
            <dd>{midiSummary}</dd>
          </div>
          <div>
            <dt>Watermark</dt>
            <dd>{transport.diagnostics.schedulingWatermarkBeat.toFixed(2)} beats</dd>
          </div>
          <div>
            <dt>Late MIDI</dt>
            <dd>{transport.diagnostics.lateMidiEventCount} events</dd>
          </div>
          <div>
            <dt>Max late</dt>
            <dd>{transport.diagnostics.maximumEngineLatenessMs.toFixed(3)} ms</dd>
          </div>
          <div>
            <dt>Reconnects</dt>
            <dd>{transport.diagnostics.bridgeReconnectCount}</dd>
          </div>
          <div>
            <dt>Physics</dt>
            <dd>{worldSnapshot.diagnostics.physicsStepCount} steps</dd>
          </div>
          {worldSnapshot.collisions.map((collision) => (
            <div key={`${collision.firstPhraseId}:${collision.secondPhraseId}`}>
              <dt>{collision.firstPhraseId} + {collision.secondPhraseId}</dt>
              <dd>
                → {collision.targetPhraseId} / {collision.touching ? 'contact' : 'clear'} /{' '}
                {collision.cooldownRemainingMs.toFixed(0)} ms
              </dd>
            </div>
          ))}
          <div>
            <dt>Transitions</dt>
            <dd>
              {worldSnapshot.diagnostics.collisionContactBeginCount} contacts /{' '}
              {worldSnapshot.diagnostics.collisionIntentQueuedCount} queued /{' '}
              {worldSnapshot.diagnostics.collisionTransitionAppliedCount} applied
            </dd>
          </div>
          <div>
            <dt>Catch-up</dt>
            <dd>
              {worldSnapshot.diagnostics.physicsCatchUpStepCount} / {worldSnapshot.diagnostics.physicsCatchUpLimitHitCount} caps
            </dd>
          </div>
          <div>
            <dt>Dropped</dt>
            <dd>{worldSnapshot.diagnostics.droppedSnapshotCount} snapshots</dd>
          </div>
          <div>
            <dt>Snapshot max</dt>
            <dd>{worldSnapshot.diagnostics.maximumSnapshotIntervalMs.toFixed(1)} ms</dd>
          </div>
          <div>
            <dt>Command queue</dt>
            <dd>
              {worldSnapshot.diagnostics.commandQueueDepth} / {worldSnapshot.diagnostics.maximumCommandQueueDepth} max
            </dd>
          </div>
          <div>
            <dt>Coalesced</dt>
            <dd>{worldSnapshot.diagnostics.coalescedMoveCount} moves</dd>
          </div>
          <div>
            <dt>Rejected</dt>
            <dd>
              {worldSnapshot.diagnostics.rejectedCommandCount} / {worldSnapshot.diagnostics.commandPressureEventCount} pressure
            </dd>
          </div>
        </dl>

        {lastRejection && (
          <div className="bridge-rejection" role="alert">
            <span>Command rejected · {lastRejection.code}</span>
            <p>{lastRejection.message}</p>
          </div>
        )}
        {transport.midiError && <p className="midi-error">{transport.midiError}</p>}
        <p className="status-note">
          Musical time and MIDI scheduling live in the native engine. Connect the selected output to a synth or DAW to hear the composition.
        </p>
      </aside>
    </main>
  )
}
