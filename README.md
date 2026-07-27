# MatrixOS

An application host for a 64x32 RGB LED matrix panel driven by a Raspberry Pi Zero 2 W,
operated with a single rotary encoder. It runs small self-contained "apps" — animations,
timers, games, and later network-backed displays — and lets you switch between them from
a launcher menu.

The longer-term goal is an **appliance**: a small number of finished units that someone
without technical knowledge can set up themselves — plug it in, join a WiFi network from
their phone, done. See [ADR-0007](docs/adr/0007-appliance-provisioning.md).

**Status:** pre-alpha. Infrastructure (build, cross-compile, CI, deploy) exists; the
application itself is being built. See [docs/roadmap.md](docs/roadmap.md) for what is
planned and in which order.

## Documentation

| Document | Purpose |
| --- | --- |
| [docs/requirements.md](docs/requirements.md) | Functional and non-functional requirements, scope boundaries |
| [docs/architecture.md](docs/architecture.md) | Module structure, app model, data flow |
| [docs/roadmap.md](docs/roadmap.md) | Release plan and the app backlog |
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
main.cpp             entry point
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
```

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
sudo ./MatrixOS --led-cols=64 --led-rows=32 --led-chain=1
```

Device-level tuning (disabling onboard sound, isolating a CPU core, GPIO slowdown) is
documented in [docs/requirements.md](docs/requirements.md) under platform constraints.

## Deploy

`pi-deployment/deploy.sh` pulls the newest CI artifact onto the device using a GitHub PAT.
Prerequisites: `git`, `jq`, `unzip`, a PAT with read access to Actions, and a `.env` next
to the script providing `GITHUB_TOKEN`.

```bash
cd pi-deployment
./deploy.sh
```

This is the development channel. A versioned, restartable release channel is planned —
see [ADR-0005](docs/adr/0005-deployment-model.md).

## License

**To be decided.** MatrixOS statically links rpi-rgb-led-matrix, which is GPLv2. Any
distributed build is therefore a derived work and must be released under a GPLv2-compatible
license. Pick one before the repository goes public.
