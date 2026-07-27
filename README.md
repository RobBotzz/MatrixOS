# MatrixOS

An application host for a 64x32 RGB LED matrix panel driven by a Raspberry Pi Zero 2 W,
operated with a single rotary encoder. It runs small self-contained "apps" — animations,
timers, games, and later network-backed displays — and lets you switch between them from
a launcher menu.

The longer-term goal is an **appliance**: a small number of finished units that someone
without technical knowledge can set up themselves — plug it in, join a WiFi network from
their phone, done. See [ADR-0007](docs/adr/0007-appliance-provisioning.md).

**Status:** pre-alpha. Two apps and a launcher run on the panel and in the terminal simulator,
driven by the shell at 60 FPS. Still missing for v0.1: the encoder and home-button backend, so
the device is currently operated by keyboard. See [docs/roadmap.md](docs/roadmap.md) for what
is planned and in which order.

## Documentation

| Document | Purpose |
| --- | --- |
| [docs/requirements.md](docs/requirements.md) | Functional and non-functional requirements, scope boundaries |
| [docs/architecture.md](docs/architecture.md) | Module structure, app model, data flow |
| [docs/roadmap.md](docs/roadmap.md) | Release plan and the app backlog |
| [docs/device-setup.md](docs/device-setup.md) | Every manual step applied to a device, and why |
| [docs/adr/](docs/adr/) | Architecture decision records — why things are the way they are |

## Hardware

| Component | Choice |
| --- | --- |
| Compute | Raspberry Pi Zero 2 W, 64-bit Raspberry Pi OS (aarch64) |
| Display | One 64x32 RGB LED matrix panel (HUB75) |
| Wiring | Direct wiring, `hardware_mapping = "regular"` (no Adafruit HAT/Bonnet) |
| Input | One rotary encoder with integrated push button, plus a dedicated home button |

The panel is driven through [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix)
(vendored as a submodule). That library needs root privileges and stable timing; see
[docs/requirements.md](docs/requirements.md) for the resulting platform constraints.

## Repository layout

```
src/
  main.cpp           composition root: picks the backends, runs
  os/                the shell: tick loop, app lifecycle, logging
  gfx/               Surface, colour — a plain RGB pixel buffer
  hal/               Display and Input interfaces + backends (matrix, sim)
  apps/              the apps themselves
tests/               host-only unit tests (Catch2)
external/            vendored dependencies (rpi-rgb-led-matrix submodule)
pi-deployment/       scripts to pull a build onto the device
docs/                requirements, architecture, decisions, roadmap
.github/workflows/   CI
```

## Getting the sources

The LED matrix library is a submodule and is **required** to build:

```bash
git clone https://github.com/RobBotzz/MatrixOS.git
cd MatrixOS
git submodule update --init --recursive
```

## Build

### On the development machine (simulator target)

The simulator build renders to the terminal instead of real hardware, so the project can
be developed and tested without the Pi. See
[ADR-0002](docs/adr/0002-display-abstraction-and-simulator.md).

```bash
cmake --preset default
cmake --build build
ctest --test-dir build          # unit tests
./build/bin/MatrixOS            # runs the app in the terminal, Ctrl-C to quit
```

On the device the rotary encoder and the home button drive everything. In the simulator —
and with `--keyboard` on the device — the **arrow keys** rotate, **space** presses and **h**
is the home button. `h` opens the launcher and takes you back to the app you came from.

| Flag | Effect |
| --- | --- |
| `--keyboard` | keep the panel but take input from stdin, so the device is drivable over SSH |
| `--simulate` | force the terminal display even on the Pi |
| `--verbose` | trace the input path event by event |
| `--test-pattern` | show the diagnostic frame without starting the shell |

The diagnostic frame is a border, a corner marker and three colour gradients, so wrong
geometry, a mirrored panel or a swapped channel order are visible at a glance. The same frame
is also available as an app in the launcher.

The host build does not need the submodule at all — it never compiles the LED library.

### Cross-compile for the Pi

Requires an aarch64 cross toolchain (`gcc-aarch64-linux-gnu`, `g++-aarch64-linux-gnu`).

```bash
cmake --preset pi-zero
cmake --build build-pi
```

The resulting binary lands in `build-pi/bin/`. CI uses `pi-zero-ci`, which is the same
preset plus `ccache`, so a green CI run means this build works too.

## Run on the device

The matrix library requires root (it maps `/dev/mem` for GPIO and DMA access):

```bash
sudo ./MatrixOS
```

Panel geometry for one 64x32 panel with `regular` wiring is compiled in as the default; the
library's `--led-*` flags override it. `--simulate` forces the terminal backend even on the
Pi, which is useful over SSH.

Device-level tuning (disabling onboard sound, isolating a CPU core, GPIO slowdown) is
documented in [docs/requirements.md](docs/requirements.md) under platform constraints.

## Deploy

`pi-deployment/deploy.sh` pulls the artifact of the newest successful `main` build onto the
device. The script is self-contained: copy that one file across, and the device needs no clone
of this repository — which is what lets the token stay scoped to `Actions: Read-only`.

On the device: `jq`, `unzip`, `curl`, plus a `.env` next to the script holding `GITHUB_TOKEN`
(mode `600`).

```bash
~/MatrixOS/pi-deployment/deploy.sh && sudo systemctl restart matrixos
```

The restart is not optional: without it the service keeps running the previous binary. The
`systemd` unit lives in `pi-deployment/matrixos.service`; installing it and every other manual
step applied to a device is recorded in [docs/device-setup.md](docs/device-setup.md).

This is the development channel. A versioned, restartable release channel is planned —
see [ADR-0005](docs/adr/0005-deployment-model.md).

## License

**GPL-2.0** — see [LICENSE](LICENSE).

MatrixOS statically links rpi-rgb-led-matrix, whose source headers place it under the GNU
GPL "version 2" with no "or later" clause. A distributed build is therefore a derived work,
which rules out GPLv3 and makes GPL-2.0 the licence that works.
