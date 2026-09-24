# BigBubbleMuff

A native Linux **VST3** and **LV2** emulation of the Russian **"Bubble Font" Big Muff Pi**
fuzz. The schematic covers the V7C Green/Black Russian and the V8 Small Box Black Russian,
which have the same circuit.

The engine simulates the **whole pedal as one circuit**. Every resistor, capacitor, transistor
and diode of the schematic, from the input jack to the Volume wiper, is solved together as a
nodal DK state-space model at 4× the host rate. The netlist is
[docs/netlist.md](docs/netlist.md). The model has no per-stage shortcut and no voicing curve,
and it is checked against ngspice (see [docs/circuit-model.md](docs/circuit-model.md)).

**Sustain**, **Tone** and **Volume** are the pedal's three pots, modelled as the real 100 k
potentiometers. Two plug-in controls sit alongside them: an **Output** trim, and a pre-gain
**Gate** for noisy pickups. A **footswitch** on the art switches the pedal in and out, and the
host's bypass works independently of it.

A digital sample of 1.0 is 1 V at the input jack, and 1 V at the output jack. At full Volume,
the plug-in's level is therefore the pedal's own. That is loud: a Big Muff has a lot of gain.

## Interface

- **The editor:** a skinned pedal with rotary knobs, an indicator LED and a footswitch.
  - It is drawn with Cairo and FreeType into an X11 window.
  - It resizes with the art's aspect ratio locked, from 0.5× to 2×.
- **Knobs:** drag to turn, and hold Shift for fine control. The mouse wheel works too.
- **The preset bar:** save, recall and delete knob positions.
  - Presets are plain-text `.bbmpreset` files in `~/.config/BigBubbleMuff/Presets`.
  - That folder is the only place the plug-in reads or writes. It never touches the network.

## Install (pre-built release)

```bash
tar -xzf BigBubbleMuff-1.0.0-linux-x86_64.tar.gz
cd BigBubbleMuff-1.0.0
./install.sh          # per-user: ~/.vst3 and ~/.lv2; as root: /usr/lib/vst3 and /usr/lib/lv2
```

`install.sh` is POSIX `sh` and works on any distribution:

- It checks for the three runtime libraries: cairo, FreeType and libX11.
- If any are missing, it names the packages for **apt**, **pacman**, **xbps** or **dnf**, and
  offers to install them.
- `--yes` installs them without asking. `--no-deps` skips the check.
- `./uninstall.sh` removes both bundles and keeps your presets.

### Debian / Devuan package

```bash
sudo apt install ./bigbubblemuff_1.0.0_amd64.deb
```

- It installs to `/usr/lib/vst3` and `/usr/lib/lv2`.
- It depends only on shared libraries: libc, libstdc++, cairo, FreeType and libX11.
- It has no maintainer scripts and no systemd dependency, so it installs cleanly on Devuan and
  other sysvinit systems.

## Build

**Requirements:**

- CMake ≥ 3.25, Ninja, and a C++20 compiler (GCC 14 or Clang).
- The development packages for cairo, FreeType, X11 and LV2 (≥ 1.18).
- lilv-0, optional: it enables the LV2 check tool.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The build writes:

- `build/VST3/Release/BigBubbleMuff.vst3`
- `build/lv2/BigBubbleMuff.lv2`

The VST3 SDK is fetched at the pinned commit. To build offline, pass
`-DFETCHCONTENT_SOURCE_DIR_VST3SDK=/path/to/vst3sdk`, pointing at a checkout of the same commit.

### Checks

```bash
# Sanitizers: tests under ASan + UBSan
cmake -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBBM_SANITIZE=ON && cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

# The Steinberg validator
build/bin/Release/validator build/VST3/Release/BigBubbleMuff.vst3

# LV2: exports and hardening, lilv load/run/state, and sord_validate
scripts/lv2-gate.sh build

# Formatting
clang-format --dry-run --Werror $(git ls-files '*.h' '*.cpp')
```

### Release packages

```bash
./packaging/makedist.sh   # → dist/BigBubbleMuff-<version>-linux-<arch>.tar.gz
./packaging/makedeb.sh    # → dist/bigbubblemuff_<version>_<arch>.deb
```

Both scripts build Release and strip the binaries. `packaging/gate.sh` then checks the packaged
bundles:

- Each module exports only its entry points.
- Each links only its allowed libraries. The LV2 audio half links nothing but the C/C++ runtime.
- RELRO, BIND_NOW and a non-executable stack are set.

`BBM_BUILD_DIR` picks the build tree, and `BBM_CMAKE_ARGS` passes extra configure options.

### The SPICE reference

`tools/spice/gen_goldens.sh` re-runs [docs/spice/bigmuff.cir](docs/spice/bigmuff.cir) in
ngspice. It writes the golden data in `tests/data/spice/`, which the circuit tests compare
against. `bbm_render in.wav out.wav` (with `--sustain`, `--tone` and the other knobs) renders
audio through the engine offline.

## Dependencies (pinned by tag + SHA)

| Dependency | Version | Commit |
|---|---|---|
| VST3 SDK | v3.8.0_build_66 | `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` |

cairo, FreeType, libX11 and the LV2 headers come from the system.

## Licence

**MIT**: see [LICENSE](LICENSE). The third-party notices are in [NOTICE](NOTICE):

- the VST3 SDK (MIT)
- the LV2 headers (ISC)
- the ngspice junction limiter (Modified BSD)
- the Liberation Sans fonts (SIL OFL 1.1)
- the dynamically linked system libraries

Earlier revisions of this repository were GPL-3.0-or-later, while they linked JUCE.

## Credits / trademarks

The schematic was traced by **Kit Rae** ([BigMuffPage](https://www.bigmuffpage.com)). The
circuit analysis references are:

- ElectroSmash, "Big Muff Pi Analysis"
- D. Yeh's 2009 Stanford thesis
- Holters & Zölzer, DAFx-11 and EUSIPCO 2015

"Big Muff" is a trademark of Electro-Harmonix, and "VST" of Steinberg Media Technologies GmbH.
BigBubbleMuff is an independent emulation, with no affiliation or endorsement.
