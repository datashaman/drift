# MIDI output smoke test

Drift sends MIDI and does not contain an internal synthesizer. Connect the selected output to a hardware synth or a software instrument in a DAW to hear the phrase.

1. Connect a MIDI destination. On macOS, a hardware interface, a DAW-created virtual input, or an enabled IAC Driver bus in Audio MIDI Setup will work.
2. Build and launch Drift:

   ```sh
   cmake --preset debug
   cmake --build --preset debug --target Drift
   open build/debug/Drift_artefacts/Debug/Drift.app
   ```

3. Choose the destination under **MIDI output**. Drift should show its human-readable name and report a connected state.
4. Start a software instrument or hardware synth listening on MIDI channel 1, then press **Play** in Drift.
5. Confirm the four-beat bass phrase repeats and that note-on events have corresponding note-off events. A MIDI monitor can be used when an audible synth is unavailable.
6. Press **Stop**, change outputs while playing, and quit Drift. Each action should silence sounding notes; no note should remain stuck.
7. Disconnect the selected device and wait up to one second. Drift should remain usable and report the missing output as an error.
