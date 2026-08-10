const phrases = [
  { name: 'BASS', className: 'phrase--bass' },
  { name: 'MELODY', className: 'phrase--melody' },
  { name: 'CHORDS', className: 'phrase--chords' },
  { name: 'DRUMS', className: 'phrase--drums' },
]

export function App() {
  return (
    <main className="shell">
      <header className="masthead">
        <div>
          <p className="eyebrow">Spatial phrase sequencer</p>
          <h1>Drift</h1>
        </div>
        <span className="build-tag">POC / SHELL 01</span>
      </header>

      <section className="field" aria-label="Phrase field preview">
        <div className="field-grid" aria-hidden="true" />
        <div className="relationship relationship--one" aria-hidden="true" />
        <div className="relationship relationship--two" aria-hidden="true" />

        {phrases.map((phrase) => (
          <div className={`phrase ${phrase.className}`} key={phrase.name}>
            <span className="phrase-core" aria-hidden="true" />
            <span>{phrase.name}</span>
          </div>
        ))}

        <p className="field-note">Four phrases. One shared space.</p>
      </section>

      <aside className="status" aria-label="Application status">
        <div className="status-heading">
          <span className="status-pulse" aria-hidden="true" />
          <span>Shell loaded</span>
        </div>

        <dl>
          <div>
            <dt>Interface</dt>
            <dd>Packaged</dd>
          </div>
          <div>
            <dt>Engine link</dt>
            <dd>Next slice</dd>
          </div>
          <div>
            <dt>MIDI</dt>
            <dd>Not connected</dd>
          </div>
        </dl>

        <p>
          The field is awake. Transport and musical state will remain native-owned as the engine comes online.
        </p>
      </aside>
    </main>
  )
}
