import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, it } from 'vitest'

import { App } from './App'

describe('Drift shell', () => {
  it('describes the packaged shell without claiming unfinished capabilities', () => {
    const markup = renderToStaticMarkup(<App />)

    expect(markup).toContain('Drift')
    expect(markup).toContain('Shell loaded')
    expect(markup).toContain('Engine link')
    expect(markup).toContain('Next slice')
    expect(markup).toContain('MIDI')
    expect(markup).toContain('Not connected')
  })

  it('renders the four initial phrase roles', () => {
    const markup = renderToStaticMarkup(<App />)

    for (const phrase of ['BASS', 'MELODY', 'CHORDS', 'DRUMS']) {
      expect(markup).toContain(phrase)
    }
  })
})
