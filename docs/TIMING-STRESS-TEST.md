# Deterministic timing stress test

`DriftStressTests` proves that native transport and MIDI timing remain independent of UI observation and bridge lifecycle.

The harness runs two identical fake-clock simulations for 600 seconds at 120 BPM with a 2 ms engine interval:

- a baseline run with regular UI observation;
- a stressed run with one-second UI stalls every 50 seconds and a versioned bridge reconnect every 30 seconds.

The runs must produce identical transport phase, scheduled notes, scheduling times, and delivery timestamps. The stressed run also fails if it finds an unpaired note, duplicate or missing four-beat loop boundary, backward scheduling watermark, late MIDI event, missing reconnect, or timing error above tolerance.

The timing tolerance is **1 microsecond**. Fake time is deterministic and no operating-system MIDI device participates, so a microsecond allows harmless floating-point rounding while remaining far below real MIDI and scheduler jitter. A larger tolerance could conceal accumulating arithmetic drift across the ten-minute run.

Run the harness directly for its readable report:

```sh
cmake --preset debug
cmake --build --preset debug --target DriftStressTests
./build/debug/DriftStressTests
```

The normal `./scripts/verify.sh` path runs the harness through CTest and then prints its report explicitly in CI logs.
