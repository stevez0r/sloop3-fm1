# SLOOP 3

**A free groovebox firmware for the M-VAVE FM-1, rewritten from scratch.** Five tracks, 13 sound engines, 153
sounds, 46 drum kits, a step sequencer with a piano roll, 27 punch-in effects, six visualisers and a web editor.

> **⚠ Public beta.** SLOOP 3 plays like SLOOP 2.8, but almost every line is new: the sound engine, the sequencer and
> the interface were written again, from zero. It is being tested on real FM-1s right now. **Expect bugs, keep
> backups, and don't rely on it on stage yet.** Please [report what you find](../../issues/new/choose).

| | Version | Where |
|---|---|---|
| **SLOOP 3** | public beta, rewritten from scratch | this repository · [installer](https://isod89.github.io/sloop3-fm1/) |
| **SLOOP 2.8** | stable, tried and tested | [github.com/isod89/sloop-fm1](https://github.com/isod89/sloop-fm1) · [installer](https://isod89.github.io/sloop-fm1/) |

The two live apart: their own repositories, installers and releases. SLOOP 3 never writes SLOOP 2.8's data, so you
can go back at any time with the backup the installer makes.

![The screens of SLOOP 3: tracks, sounds, drums, the piano roll, the punch-in effects, a visualiser](docs/screens.png)

**[Install it from your browser →](https://isod89.github.io/sloop3-fm1/)** (Chrome or Edge, a USB data cable, through
m-bootloader)

**[The user guide →](https://stevez0r.github.io/sloop3-fm1/)** (new to the FM-1: start here) · **[What changed from SLOOP 2.8 →](docs/GUIDE.md)** · **[The web editor →](https://isod89.github.io/sloop3-fm1/editor.html)**

## What's new

**3.4.2: the issues.** A song changing a track's engine between patterns plays step 1 again (#24); ratchets follow
the swing (#17); a track keeps to the beat when its DIV changes while playing (#18, #26); the arpeggiator's and the
rolls' notes are recorded where they sound, 1/32 too (#28); each drum sound has its own LEVEL (#25); STRUM goes below
0, from the highest note down (#30); the SEL knobs stop at their ends; the punch-in goes on while its key is held
after FX is let go, as in 2.8.5; the keys of the track left no longer stay lit (#18, #26); the song plays from the LINE
shown (#27); the vibes decay alike above and below F#4 (#14); the mix 6 dB louder, its peaks still limited (#16);
CC 11 sets the level, CC 1 and CC 11 can be learnt, the keys' pressure acts as the mod wheel (#12, #21); in the
editor, the condition and ratchet lists stay open while it plays (#31); the installer asks the FM-1 again before
giving up (#13).

**3.4.1: reliability and levels.** Two causes of freezes fixed (a message said during a long flash erase, and the
interrupts' stack in memory the firmware reuses), a flash operation that never ends no longer stops the screen, every
factory sound brought to the same loudness as heard (with its effects, on three keys), and the drum kits' snares,
claps and hats evened out from one kit to the next. New checks run on every build and in the twin.

**3.4.0: SLOOP 2.8.5's workflow.** SLOOP 3 now has SLOOP 2.8.5's pages, layers, gestures and way of saving (the
8 patterns only change when you save them), with SLOOP 3's extras on top: 5 tracks, 8 patterns a track, 128 steps,
SOUNDS and FAVS, the NOTES piano roll, the DRUMS grid and the MIX page. See the
[release notes](../../releases) and [the guide](docs/GUIDE.md). **A project saved by 3.4.0 can't be read by 3.3.x**:
back up with the editor before installing.

Underneath, everything is new. The firmware was rewritten from scratch, and the new engine made room for features SLOOP 2.8
never had.

**The engine**

- **32-bit float sound**: every voice, filter and effect computed in floating point: cleaner filters, smoother
  envelopes, more headroom.
- **1.45 ms blocks** (64 samples instead of 256): the sound answers your keys, knobs and MIDI four times sooner.
- **Both cores at work**: 32 voices shared across the FM-1's two cores, plus 8 so releases ring out without stealing
  notes.
- **13 engines, all 153 factory sounds rebuilt value for value**: ANALOG, DIGITAL, PHASE, LOFI, TRIO, WHEEL, VOICE,
  NOISE, SAMPLE, GRAIN, FM6 (six-operator FM with your patch bank, DX7 voices by name), PHYS (strings, bars, drum
  heads) and **SLICE** (new): a sample cut into slices, one per key.
- **46 drum kits**: the 32 synthesised kits, the acoustic GM kit with its four treatments, your USR kits and your SYN
  kits. Each of the 16 sounds has its own TUNE and DECAY.

**The sequencer**

- **5 tracks**: four synths (one more than SLOOP 2.8) and the drums.
- Up to **128 steps** per pattern, **8 patterns per track**, each with its own sound.
- Live recording kept at **1/96 of a step**; QUANT before or after. **3 levels of undo.**
- **Your own breakbeats**: drop a break in the web editor; it finds every hit, cuts it into slices and sends it to
  the FM-1, on a synth track (SLICE: the slices on the keys) or as your own drum kit, saved under its name.
- Step **conditions** (FILL, NO FILL, 1 of 2 … 4 of 4, 10 %–90 % chance) and **parameter locks** on the sound and the
  mix.
- **NOTES**: a full-screen piano roll, 8 to 64 steps, each note with its own sheet (slide, nudge, fill, move, copy,
  delete).
- A **song** recorded live or written line by line.

**Sound, screen and connections**

- Per-track INSERT (SOFT, HARD, FOLD, FUZZ, CRUSH, PHASER, FLANGER, CHORUS) and SLICER; delay, reverb, chorus;
  **27 punch-in effects** (hold FX and play them like keys; FX + HOME locks the layer); DUST, DUCK and a DJ filter on
  the master; **TEXTURE** under the mix.
- **The battery** as an icon in the top right corner (its bars fill in turn on a computer's USB).
- **The keys light up to guide you**: hold HOME (OPEN, NEW, SAVE), hold SAVE (your 8 patterns, dim to bright).
- **SOUNDS**: every sound by category, each on a white key; 16 favourites; 32 sounds of your own.
- A new interface in English, colour themes, and **six visualisers** (HOME on the tracks screen).
- **USB audio** (class compliant, 44.1 or 48 kHz), USB MIDI in and out, MIDI in on the TRS jack.
- A **web editor** in the browser: LIVE, sounds, sequencer, mixer, song, samples, kits (and a drum kit from your own samples), FM6 bank, projects, settings,
  theme.
- **Your SLOOP 2.8 work comes along**: at its first start SLOOP 3 opens SLOOP 2.8's projects, user presets,
  favourites, samples, SYN kits, FM6 bank and settings. It only reads them.

## Install

SLOOP 3 installs from a web page over USB MIDI, **only through
[m-bootloader](https://github.com/jvitkauskas/m-bootloader)**'s MIDI MODE, and we recommend m-bootloader to every
SLOOP user. **It is there for your safety:** it checks every firmware before starting it, and if an install is
interrupted or a firmware is damaged, the FM-1 opens a recovery menu instead of hanging. You can always reinstall
from a browser, or use USB download mode, without opening the case.

The [installer page](https://isod89.github.io/sloop3-fm1/) is a guided wizard, in English or French, that walks
you through five steps:

1. **Before you start**: what a beta means, what you need (an FM-1, a USB data cable, Chrome or Edge).
2. **The safety net: m-bootloader, once.** The page shows how to check whether it is there (hold PLAY at power-on).
   If not, the recommended way is [M-Flasher](https://github.com/jvitkauskas/m-flasher) (Windows, macOS, Linux): put
   the FM-1 in USB download mode, **back up** the whole flash, then install `fm1-uboot.boot` from the
   [latest m-bootloader release](https://github.com/jvitkauskas/m-bootloader/releases/latest). An FM-1 still on
   M-VAVE's firmware can also take `fm1-uboot.fwsc` in M-VAVE's M-UPGRADE app.
3. **Connect**: hold PLAY at power-on, choose MIDI MODE, plug it in. The page finds the FM-1 by itself.
4. **Back up**: the whole flash (1 MB) as a file in your downloads. Nothing is written.
5. **Install**: about a minute. The package is read and checked before anything is erased.

You can also download `installer-sloop3-*.html` from the [latest release](../../releases/latest) and open it from
your disk.

**Going back to SLOOP 2.8:** restore the backup you made before installing SLOOP 3, with M-Flasher. SLOOP 2.8 and
everything it kept come back exactly as they were.

## Building

### On a PC: the tests, the PC twin

Any C11 compiler (gcc or clang) on Linux or macOS, Python 3 with NumPy and Pillow for the tools, Node.js for the
page and editor tests.

```sh
make test     # the sound, the sequencer, the saves, harmony, arpeggiator, levels, SLOOP 2.8 import, MIDI, the screens
make demo     # build/demo.wav
make twin     # after fw/build.sh: the whole firmware on a simulated FM-1, under the sanitizers (host/twin.sh)
```

The PC twin (`host/`) runs `fw/main.c` itself on a simulated FM-1: screen, panel, both cores, the flash and its
window, power cuts during saves, the editor protocol, MIDI. `host/twin.sh` runs every scenario.

### The firmware

You need the [pi32v2 LLVM toolchain](https://github.com/jvitkauskas/pi32v2-llvm), release `pi32v2-23.1.5-2`. Nothing
from JieLi is needed. On Linux x86-64:

```sh
curl -fLo toolchain.tar.xz https://github.com/jvitkauskas/pi32v2-llvm/releases/download/pi32v2-23.1.5-2/pi32v2-llvm-23.1.5-2-linux-x64.tar.xz
echo 'cb896562418a2be93edb9bac26af4bc81a106a32974002a78e6b9fc203c93990  toolchain.tar.xz' | sha256sum -c
mkdir toolchain && tar -xJf toolchain.tar.xz -C toolchain --strip-components=1
fw/build.sh                                            # or: fw/build.sh /path/to/toolchain/bin
```

This writes `build/fw/fw.area` (the application area), `build/fw/installer-fm1.html` (the installer page, the
firmware inside, one file) and `build/fw/editor-sloop3.html` (the web editor), and checks the page against a
simulated m-bootloader (`tests/install_sim.js`) and, with Playwright, as a user goes through it in Chromium
(`tests/installer_e2e.js`).

The firmware runs from RAM (an LZ4-packed image, unpacked at the start); the interface's code and the factory sample
bank are read in place through the flash window. The sound runs on both cores, block by block; the panel and its
lights are scanned by a timer interrupt.

## Source

| Path | What |
|---|---|
| `engine/` | the sound: voices, the 13 engines, drums, effects, the master |
| `seq/` | the sequencer: patterns, steps, locks, conditions, the song |
| `app/` | the interface: pages, drawing, fonts, presets, projects and saves, SLOOP 2.8 import, MIDI, the editor protocol, the visualisers |
| `fw/` | the firmware's entry, linker scripts, the build, the unpacker |
| `hal/fm1/`, `core/` | the FM-1's hardware layer (screen, panel, audio, USB, flash, the second core) and a small C library |
| `host/` | the PC twin and its scenarios |
| `web/` | the installer page, the package format m-bootloader reads, the web editor |
| `tools/` | the presets, kits, fonts, the sample bank, the application area, the page |
| `tests/` | the PC tests |
| `assets/` | the factory sample bank and its credits, the font licence |

## Status

SLOOP 3 is a **public beta**, and it is unofficial: not made, endorsed or supported by M-VAVE. Installing third-party
firmware is at your own risk. Please report problems in the [issues](../../issues), with the version (HOME) and what
the screen showed (HOME also shows the load: CPU, PEAK, LATE).

## Credits and licences

- SLOOP 3: GPL-3.0 (see [LICENSE](LICENSE)). It is new code, written for this project; the values of its factory
  sounds, kits and presets are SLOOP 2.8.5's ([github.com/isod89/sloop-fm1](https://github.com/isod89/sloop-fm1),
  GPL-3.0), whose earlier versions were based on Felucca by Leo Kuroshita (Hügelton Instruments).
- [m-bootloader](https://github.com/jvitkauskas/m-bootloader), [M-Flasher](https://github.com/jvitkauskas/m-flasher)
  and the [pi32v2 LLVM toolchain](https://github.com/jvitkauskas/pi32v2-llvm) are by Julius Vitkauskas (MIT). SLOOP 3
  includes none of their code: the installer page speaks m-bootloader's MIDI MODE protocol as its specification
  describes.
- The factory samples: CC0 recordings from VCSL and VSCO-2 Community Edition (Versilian Studios / Sam Gossner) and the
  Sonic Pi sample set ([assets/CREDITS-SAMPLES.txt](assets/CREDITS-SAMPLES.txt)).
- The screen's fonts are rendered from [Inter](https://github.com/rsms/inter), SIL Open Font License 1.1
  ([assets/fonts/OFL-Inter.txt](assets/fonts/OFL-Inter.txt)).
