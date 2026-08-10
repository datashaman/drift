import { useEffect, useMemo, useState } from 'react'

import {
  createTransportBridge,
  initialTransportState,
  type TransportBridge,
} from './bridge/transportBridge'

const phrases = [
  { name: 'BASS', className: 'phrase--bass', active: true },
  { name: 'MELODY', className: 'phrase--melody', active: false },
  { name: 'CHORDS', className: 'phrase--chords', active: false },
  { name: 'DRUMS', className: 'phrase--drums', active: false },
]

interface AppProps {
  bridge?: TransportBridge
}

export function App({ bridge }: AppProps) {
  const transportBridge = useMemo(() => bridge ?? createTransportBridge(), [bridge])
  const [transport, setTransport] = useState(initialTransportState)
  const [tempoDraft, setTempoDraft] = useState(String(initialTransportState.bpm))

  useEffect(() => {
    const unsubscribe = transportBridge.subscribe(setTransport)
    transportBridge.send({ type: 'ui.ready' })
    return unsubscribe
  }, [transportBridge])

  useEffect(() => {
    setTempoDraft(String(Math.round(transport.bpm)))
  }, [transport.bpm])

  const toggleTransport = () => {
    transportBridge.send({
      type: transport.playing ? 'transport.stop' : 'transport.play',
    })
  }

  const commitTempo = () => {
    const bpm = Number(tempoDraft)
    if (Number.isFinite(bpm) && bpm >= 40 && bpm <= 240) {
      transportBridge.send({ type: 'transport.setTempo', bpm })
    } else {
      setTempoDraft(String(Math.round(transport.bpm)))
    }
  }

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
            disabled={!transportBridge.connected}
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
              disabled={!transportBridge.connected}
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
        </div>
      </header>

      <section className="field" aria-label="Phrase field preview">
        <div className="field-grid" aria-hidden="true" />
        <div className="relationship relationship--one" aria-hidden="true" />
        <div className="relationship relationship--two" aria-hidden="true" />

        {phrases.map((phrase) => (
          <div
            className={`phrase ${phrase.className} ${phrase.active ? 'phrase--active' : 'phrase--queued'}`}
            key={phrase.name}
          >
            <span className="phrase-core" aria-hidden="true" />
            <span>{phrase.name}</span>
          </div>
        ))}

        <p className="field-note">Bass phrase online. Three roles queued.</p>
      </section>

      <aside className="status" aria-label="Application status" aria-live="polite">
        <div className="status-heading">
          <span className="status-pulse" aria-hidden="true" />
          <span>{transportBridge.connected ? 'Native engine linked' : 'Browser preview'}</span>
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
            <dd>Bass / 4 beats</dd>
          </div>
          <div>
            <dt>Scheduled</dt>
            <dd>{transport.scheduledEventCount} events</dd>
          </div>
          <div>
            <dt>MIDI</dt>
            <dd>Recording sink</dd>
          </div>
        </dl>

        <p>
          Musical time and phrase scheduling live in the native engine. This interface only sends intent and displays its state.
        </p>
      </aside>
    </main>
  )
}
