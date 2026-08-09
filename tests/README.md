# Tests

`harness.cpp` builds `build\lovoip-harness.exe`, a minimal offline CLAP host
that loads `build\lovoip.clap` and renders a WAV through it.

## Automated checks (run by harness)

- `--selftest`: silence input must produce silence output (peak < 1e-4).
- Parameter round-trip: after `flush()`, `get_value()` must return the quality
  value that was set; mismatch exits with code 4.

## Manual / listening checks

`scripts\render.ps1` renders each clip in `voice_clips\` (CC0, see
`external\licenses\voice_clips.md`) at quality 8, 15 and 22. Listen or run a
spectral check on `build\out\`:

Expected behavior:

- A single **Quality** parameter (integer 8..22, default 22) couples bandlimit
  (kHz) and bitrate (kbps). Lower = more band-limited and grittier.
- Quality 8 output should have little energy above ~4 kHz; 22 should retain
  the most high-frequency content.
- The plugin preserves input level: output RMS tracks input, no clipping.
- The signal path is sample-synchronous (zero added latency); output aligns
  with input at offset 0.
