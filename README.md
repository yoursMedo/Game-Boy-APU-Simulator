# Game Boy APU Simulation

A from-scratch C++ simulation of the Nintendo Game Boy's Audio Processing Unit
(APU) — the chip responsible for all of the console's sound.

The simulation models all four hardware sound channels:
- **Two pulse channels** with selectable duty cycles, volume envelopes, and
  (on channel 1) an automatic frequency sweep
- **A wave channel** that plays arbitrary 32-sample waveforms from Wave RAM
- **A noise channel** driven by a linear feedback shift register (LFSR)

All channels are driven by a single master-clock stepping loop, with each timer
(pitch, length, envelope, sweep, and sample capture) advanced through a shared
accumulate-and-threshold pattern that mirrors how the real hardware divides one
clock into many rates.

The emulated APU is controlled exactly as the real chip is: through writes to its
memory-mapped registers (`0xFF10`–`0xFF26`). To verify correctness, the project
replays **VGM files** — timestamped logs of a game's register writes — through the
simulation and renders the result to a WAV file, reproducing recognizable Game Boy
music.

## Building
Built with CMake and C++23.
```cmake
cmake -B build && cmake --build build
./build/Gameboy_Demo path/to/track.vgm
```

## References
Hardware behaviour is based on the [Pan Docs](https://gbdev.io/pandocs/), the
community reference for Game Boy hardware.