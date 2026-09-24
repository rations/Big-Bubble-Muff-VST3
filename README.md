# BigBubbleMuff

A native **Linux VST3 + Standalone** emulation of the Russian **"Bubble Font" Big
Muff Pi** fuzz/distortion (schematic versions V7C Green/Black Russian and V8 Small
Box Black Russian — circuit-identical). Built in C++20 with [JUCE](https://juce.com)
and a full **Wave Digital Filter** circuit model via
[chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf).

The three pedal controls — **Sustain**, **Tone**, **Volume** — behave like the
real pedal because the model is derived from the actual schematic netlist
([docs/netlist.md](docs/netlist.md)), not a generic distortion curve. Two utility
controls — an **Output** trim and a pre-gain **Gate** — sit alongside them.

## Interface
- Skinned pedal editor (faceplate + rotary knobs + indicator LED + footswitch).
- **Resizable** window with the artwork's aspect ratio locked.
- A slim top bar with a **preset** dropdown: save and recall knob positions. User
  presets are stored as plain `.xml` files under `~/.config/BigBubbleMuff/Presets`.
- The **Standalone** app adds a native titlebar and a File/Help menu
  (`Preferences` → audio device settings, `About`).

## Status
Phased build:
- **Phase A** — per-stage WDF model with WDF diode-pair clippers, input booster,
  passive tone stack and output recovery. **Complete & verified**: builds clean,
  tests pass, pluginval strictness 10 passes, ASan/UBSan clean, binary hardened
  (Full RELRO / NX / PIE / stack canaries).
- **Phase B** — joint BJT+diode R-type nonlinear roots for maximum fidelity
  (planned).

## Install (pre-built release)

Download `BigBubbleMuff-<version>-linux-<arch>.tar.gz` from the Releases page and
extract it:

```bash
tar -xzf BigBubbleMuff-0.1.0-linux-x86_64.tar.gz
cd BigBubbleMuff-0.1.0
./install.sh          # per-user (~/.vst3, ~/.local/bin); run as root for system-wide
```

`install.sh` is POSIX `sh` and distro-agnostic: it checks that the required
runtime libraries are present and, if any are missing, prints the exact package
list for your package manager (**apt**, **pacman**, **xbps**, **dnf**) and offers
to install them. Flags: `--yes` (don't prompt before installing packages),
`--no-deps` (skip the check). Remove everything again with `./uninstall.sh`.

The standalone also gets a `bigbubblemuff.desktop` entry so it appears in your
application menu.

### Debian / Devuan package

A `.deb` is provided for Debian, Ubuntu and derivatives. It has **no systemd
dependency**, so it installs cleanly on **Devuan** and other sysvinit systems:

```bash
sudo apt install ./bigbubblemuff_0.1.0_amd64.deb
```

This installs the VST3 to `/usr/lib/vst3`, the standalone to `/usr/bin`, and only
pulls in shared-library dependencies (ALSA, freetype, fontconfig, X11).

## Build

Requirements: CMake ≥ 3.25, a C++20 compiler (GCC 14 / Clang), Ninja, and Linux
audio/GUI dev packages (ALSA, X11, freetype, fontconfig).

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Artefacts:
- VST3 → `build/BigBubbleMuff_artefacts/Release/VST3/BigBubbleMuff.vst3`
  (also copied to `~/.vst3`).
- Standalone → `build/BigBubbleMuff_artefacts/Release/Standalone/BigBubbleMuff`.

Offline / pinned-local JUCE:
`-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` (JUCE 8.0.13 checkout).

### Building release packages

```bash
./packaging/makedist.sh   # → dist/BigBubbleMuff-<version>-linux-<arch>.tar.gz
./packaging/makedeb.sh    # → dist/bigbubblemuff_<version>_<arch>.deb
```

`makedist.sh` builds Release, strips the binaries, and stages the VST3,
standalone, `install.sh`/`uninstall.sh`, desktop entry, icon and `COPYING` into a
tarball. `makedeb.sh` builds the systemd-free `.deb` (override the package
maintainer with the `MAINTAINER` env var).

## Dependencies (pinned by tag + SHA)
| Dependency  | Version | Commit                                     |
|-------------|---------|--------------------------------------------|
| JUCE        | 8.0.13  | `7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2` |
| chowdsp_wdf | v1.0.0  | `36b5775555af21f0f417d2bc866ba7b4b2788614` |
| VST3 SDK    | 3.8     | local: `/home/human/third_party/vst3sdk`   |

## Haiku (native VST3, no JUCE)

`haiku/` holds a second, independent build of the same pedal for **Haiku OS**: a
native VST3 written directly against the Steinberg SDK with an Interface Kit
editor. JUCE has no Haiku backend, so nothing there includes or links it — the
circuit model is the same C++ and the editor uses the same `gui/*.png` artwork.
The Standalone app is not part of this port; the plug-in is loaded by a VST3 host
such as JackDAW.

```bash
# on Haiku, with VST3-haiku checked out alongside this repository
cd haiku
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
make install                       # -> ~/config/non-packaged/add-ons/media/VST3/
make validate                      # Steinberg validator
sh packaging/make-hpkg.sh          # optional .hpkg
```

From a Linux dev host, `make -C haiku sync remote-build remote-install
remote-validate` does the same over ssh.

## License
The repository is licensed in two parts, because only one of them links JUCE:

- **Everything except `haiku/`** — `src/`, the Linux CMake target, tests and
  tools — is **GPLv3-or-later**, see [COPYING](COPYING). JUCE is used under its
  GPLv3 option; chowdsp_wdf is BSD-3; the Steinberg VST3 SDK under its
  GPLv3-compatible terms.
- **`haiku/`** is **MIT**, see [haiku/LICENSE](haiku/LICENSE). With JUCE out of
  the picture there is no copyleft dependency left: the VST3 SDK 3.8.0 is MIT and
  Haiku's kits are MIT. The circuit sources there are copies of the author's own
  JUCE-free code, relicensed by their author.

## Credits / trademarks
Schematic traced by **Kit Rae** ([BigMuffPage](https://www.bigmuffpage.com)).
Circuit analysis reference: ElectroSmash "Big Muff Pi Analysis". "Big Muff" is a
trademark of Electro-Harmonix and "VST" of Steinberg Media Technologies GmbH; this
is an independent emulation with no affiliation or endorsement.
