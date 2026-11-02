# FM1 OP3N — technical notes

A four-track performance groovebox for young kids, built on FM1 Open V14.
FM1 OP3N replaces the stock panel application (`app/screen.c`). The FM1 binary
engine still makes every sound. FM1 OP3N decides what the keys, knobs and
screen do, and adds a hard output-level ceiling.

The stock UI is kept as `app/op3n/stock_screen.c.orig`. To go back, copy it
over `app/screen.c` and remove the `app/op3n/*.c` files from the build, or
flash the official V14/V15 firmware from the workbench.

## Tracks

| # | Track | Engine source | Screen | PUNCH | COLOR | SPACE | TWIST |
|---|-------|---------------|--------|-------|-------|-------|-------|
| 1 | DRUMS | 808 kit | Cassette tape | Attack, shorter decay | Tone, snappy, tune | Reverb/delay sends | 808 Drive (distortion) |
| 2 | BASS | 303 | Submarine depth gauge | Shorter decay, accent | Cutoff, env mod, resonance (capped) | Sends | 303 Drive |
| 3 | LEAD | DX7 "SYN LEAD 1" | Rocket | ADSR attack/decay, modulator rates | Modulator levels, feedback | Sends | LFO wobble (vibrato + tremolo) |
| 4 | MAGIC | DX7 "KALIMBA" + arp | Robot face | ADSR attack/decay, modulator rates | Modulator levels, feedback | Sends | Arp rate and gate |

Engine tracks 5 to 8 are muted and cleared. Every boot starts from the same
kit with a starter beat and bassline.

## Panel

| Control | FM1 OP3N |
|---------|---------|
| SELECT | Choose the track |
| KNOB 1 to 4 | PUNCH, COLOR, SPACE, TWIST for the current track |
| PRESETS | Tempo |
| MASTER | Volume, always under the hard ceiling |
| PLAY | Start / stop |
| REC | Record what you play into the current track |
| ARP | Arpeggiator on/off for the current track |
| SEL (SCL) | C major or C pentatonic (pentatonic at boot) |
| OCT− / OCT+ | Octave −2 to +2 |
| FX (hold) | Punch-in echo fill: all tracks into the delay with high feedback, restored on release |
| SEQ (hold) + white keys | Toggle the 16 steps. Key LEDs show the pattern while SEQ is held |
| Keys | Drum pads on DRUMS. Scale-locked notes elsewhere, with in-scale keys lit |

ALGORITHM, ENV, LFO, EDIT, GLO, HOME and SAVE are unused.

## Safety ceiling

`app/audio_output.c` ends every audio block with a stereo-linked peak limiter
and a brickwall clamp at `OPM_CEILING_Q23`. It runs after master gain, USB
playback and Bluetooth music are mixed in, so nothing reaches the DAC around
it. It uses integer math only, with a 32-bit divide, so the audio path pulls in
no Flash-resident compiler helpers.

**The default of 0.5 FS (−6 dBFS) is a placeholder.** Measure the real device
with the loudest headphones you expect, using an SPL meter or ear simulator,
then lower the constant until the worst case meets your target. 85 dB(A) is the
usual limit for children's headphones. A fixed analog limit in hardware is the
only guarantee that survives reflashing.

## Files

```
app/screen.c                 FM1 OP3N app: setup, controls, keys, LEDs, macro→engine, band painter
app/op3n/opm_macros.[ch]   Pure integer macro maths and the scale quantizer (unit-testable)
app/op3n/opm_scenes.[ch]   Four engine screens, header and knob HUD
app/op3n/opm_gfx.[ch]      Band rasterizer: lines, rects, circles, triangles, font, Q15 trig
app/audio_output.c           + kid-safe output ceiling
tools/op3n_host_test.c     Native test on the open simulator engine (25 checks)
tools/build_web_preview.py   Linux/macOS port of Build-Web.ps1 -LocalPreview
```

## Test on a PC (Linux/macOS)

```sh
clang -std=c11 -O2 -fwrapv -DFM1_HOST_TEST -DFM1_OPEN_WEB \
  -I web/engine -I web/synth -I third_party/synth -I sdk/include -I app/synth -I app \
  tools/op3n_host_test.c $(find app web third_party -name '*.c' -not -path 'app/board/*') -lm -o op3n_test
./op3n_test     # writes op3n_screens_*.ppm and op3n_demo.wav
```

## Try it in a browser

On Windows use `.\tools\Build-Web.ps1 -LocalPreview`. On Linux/macOS, with
WASI SDK 27 extracted at `$W`:

```sh
mkdir -p build/web
$W/bin/clang --sysroot=$W/share/wasi-sysroot -std=c11 -O2 -fwrapv -fno-fast-math -ffp-contract=off \
  -DFM1_HOST_TEST -DFM1_OPEN_WEB -mexec-model=reactor \
  -I web/engine -I web/synth -I third_party/synth -I sdk/include -I app/synth -I app \
  $(find app web third_party -name '*.c' -not -path 'app/board/*') \
  -Wl,--strip-all -Wl,-z,stack-size=131072 -Wl,--initial-memory=4194304 -Wl,--max-memory=33554432 \
  -o build/web/simulator.wasm
python3 tools/build_web_preview.py   # -> build/preview/index.html
# published copy: python3 tools/build_web_preview.py https://github.com/benm0ra/fm1_op3n/archive/refs/tags/vX.Y.Z.tar.gz docs/op3n-preview.html
```

Note: the browser simulator runs the open engine sources. The device runs the
binary `libfm1_engine.a`, which should behave the same but is not guaranteed to.

## Build and flash the device (Windows)

1. Install JieLi toolchain 2.5.3 (see README.md), then set
   `$env:FM1_PI32_TOOLCHAIN = 'C:\JL\pi32'`.
2. Run `./sdk/Build.ps1 -FirmwareOnly -Toolchain 'C:\JL\pi32'`.
3. In the FM1 workbench, back up projects, voices and samples, then flash.
   You can return to the official firmware from the same workbench.

## Known limits (v1)

- DX7 edits apply from the next note. Notes already sounding keep their old
  COLOR/PUNCH; 303 and 808 changes are heard right away.
- No save: FM1 OP3N rebuilds its kit at every boot. It never writes project
  slots, but it does replace the working project in RAM.
- The robot's eye dance follows the arp clock (tempo × rate) while the MAGIC
  track sounds. It is not phase-locked to individual notes.
- The screen redraws 11 scene bands per frame at up to 20 fps, within a 4 ms
  budget per poll. On the device the frame rate is whatever the SPI link
  sustains; the HUD bands are resent only when they change.

Licence: GPLv3 with the FM1 SDK linking exception, the same as the APP sources.
