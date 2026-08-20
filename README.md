# MatrixOS

[![CI](https://github.com/RobBotzz/MatrixOS/actions/workflows/pi-zero-ci.yml/badge.svg)](https://github.com/RobBotzz/MatrixOS/actions/workflows/pi-zero-ci.yml)

An application host for a 64x32 RGB LED matrix panel driven by a Raspberry Pi Zero 2 W,
operated with a single rotary encoder. It runs small self-contained "apps" — animations,
timers, games, and later network-backed displays — and lets you switch between them from
a launcher menu.

The longer-term goal is an **appliance**: a small number of finished units that someone
without technical knowledge can set up themselves — plug it in, join a WiFi network from
their phone, done. See [ADR-0007](docs/adr/0007-appliance-provisioning.md).

**Status:** **v0.4 built; the setup flow runs on hardware, the provisioning does not yet.** With v0.3 the device remembered
things; with v0.4 it can be handed to someone else. A unit that knows no network opens its own
WiFi access point, shows the name to join on the panel, and serves a setup page that any phone
lands on without typing an address. Once online it answers to `matrixos-xxxx.local` with a
configuration page — version, network, factory reset — and shows a clock that says
`NO TIME` until it has actually been told the time rather than displaying a plausible lie.
One script turns a blank card into a device; another takes the image. The same source still
runs in a terminal simulator, now including the whole setup flow via `--fake-wifi`.

The access point, the captive portal and the join have since run on a real radio and a real
phone. What is left is everything that separates a shipped unit from the maintainer's Pi, which
was configured by hand rather than by the script: a blank card provisioned end to end, two units
that do not collide, a read-only root and ten pulled plugs. The list is
[requirements.md §5.3](docs/requirements.md#53-v04--appliance).

## Apps

| App | What it does |
| --- | --- |
| Plasma | Four animated variants; rotate to switch, press to freeze. |
| Pomodoro | Focus/break cycle timer; rotate to set durations, press to advance, long-press to reset. |
| Snake | Rotate to turn, press to start; keeps a persistent high score. |
| Settings | Brightness and which app the device starts with. |
| Test Pattern | Diagnostic frame — border, corner marker, three colour gradients — for checking panel geometry and wiring at a glance. |

## Documentation

| Document | Purpose |
| --- | --- |
| [docs/requirements.md](docs/requirements.md) | Functional and non-functional requirements, scope boundaries |
| [docs/architecture.md](docs/architecture.md) | Module structure, app model, data flow |
| [docs/roadmap.md](docs/roadmap.md) | Release plan and the app backlog |
| [docs/device-setup.md](docs/device-setup.md) | Every step applied to a device, and why |
| [docs/image-build.md](docs/image-build.md) | Turning a provisioned device into a shipped image |
| [web/README.md](web/README.md) | The configuration page and how to change it |
| [docs/adr/](docs/adr/) | Architecture decision records — why things are the way they are |

## Hardware

| Component | Choice |
| --- | --- |
| Compute | Raspberry Pi Zero 2 W, 64-bit Raspberry Pi OS (aarch64) |
| Display | One 64x32 RGB LED matrix panel (HUB75) |
| Wiring | Direct wiring, `hardware_mapping = "regular"` (no Adafruit HAT/Bonnet) |
| Input | One rotary encoder with integrated push button, plus a dedicated home button |
| Setup | The device's own WiFi access point and a captive portal — no keyboard, no screen |

The panel is driven through [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix)
(vendored as a submodule). That library needs root privileges and stable timing; see
[docs/requirements.md](docs/requirements.md) for the resulting platform constraints.

## Repository layout

```
src/
  main.cpp           composition root: picks the backends, runs
  os/                the shell: tick loop, app lifecycle, logging, state, provisioning
  gfx/               Surface, colour — a plain RGB pixel buffer
  hal/               Display and Input interfaces + backends (matrix, sim)
  net/               HTTP server, setup portal, WiFi control
  apps/              the apps themselves
tests/               host-only unit tests (Catch2)
web/                 the React configuration page, compiled into the binary
tools/               generators whose output is checked in (font, web bundle)
external/            vendored dependencies (rpi-rgb-led-matrix submodule)
pi-deployment/       provisioning scripts and the deployment channel
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

On the device the rotary encoder and the home button drive everything. In the simulator — and
with `--keyboard` on the device — the keyboard stands in for both:

| Key | Event | On the device |
| --- | --- | --- |
| right / up arrow | `Rotate(+1)` | turn the encoder clockwise |
| left / down arrow | `Rotate(-1)` | turn the encoder counter-clockwise |
| space, enter | `Press` | click the encoder |
| `l` | `LongPress` | hold the encoder for 600 ms |
| `d` | `DoublePress` | not produced by default, see Q-4 in [requirements.md](docs/requirements.md) |
| `h` | `Home` | the dedicated home button ([ADR-0009](docs/adr/0009-dedicated-home-button.md)) |

`h` opens the launcher and takes you back to the app you came from.

**Hold and double-click get their own keys on purpose.** A terminal in raw mode reports
keystrokes, never releases, so a hold is only visible through autorepeat — whose start delay is
typically 500 ms, too close to the 600 ms long-press threshold to tell a hold from a tap without
delaying every single press. Deriving the gesture from key timing here would make the simulator
emit event sequences the hardware never emits, which is the opposite of what it is for
([ADR-0002](docs/adr/0002-display-abstraction-and-simulator.md)). Holding space just repeats
`Press`.

| Flag | Effect |
| --- | --- |
| `--keyboard` | keep the panel but take input from stdin, so the device is drivable over SSH |
| `--simulate` | force the terminal display even on the Pi |
| `--verbose` | trace the input path event by event |
| `--test-pattern` | show the diagnostic frame without starting the shell |
| `--fake-wifi` | drive the whole setup flow from invented networks, without a radio |
| `--port N` | serve the web pages on N instead of 80 (which needs root) |

### Trying the setup flow without a device

```bash
MATRIXOS_STATE_DIR=/tmp/matrixos-dev ./build/bin/MatrixOS --fake-wifi --port 8080
```

The panel shows the setup screen, and `http://localhost:8080/` serves the same page a phone
would get in a captive portal. Pick a network, submit, and the panel walks through connecting
and connected — after which the same address serves the React configuration page instead.
`curl -X POST localhost:8080/api/reset` puts it back.

Everything the device remembers — the high score, the settings, the last active app — lives in one
directory ([ADR-0011](docs/adr/0011-state-store-format.md)). On a development machine that is
`$XDG_STATE_HOME/matrixos`, and `MATRIXOS_STATE_DIR` overrides it:

```bash
MATRIXOS_STATE_DIR=/tmp/matrixos-scratch ./build/bin/MatrixOS   # a device with factory defaults
cat /tmp/matrixos-scratch/*.conf                                # plain text, on purpose
```

On the device that directory is its own partition, so the read-only root filesystem cannot take
it away, and it has to belong to `daemon` — the matrix library drops privileges. Both are
handled by `provision.sh`; the reasoning is in [docs/device-setup.md](docs/device-setup.md).
Without a usable directory MatrixOS runs fine and remembers nothing.

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

On a provisioned device the root filesystem is read-only, so a deploy needs the overlay off
first — a collision named in [ADR-0008](docs/adr/0008-power-loss-resilience.md) and accepted as
the price of an appliance:

```bash
sudo raspi-config nonint disable_overlayfs && sudo reboot
sudo MATRIXOS_DEST=/opt/matrixos ~/MatrixOS/pi-deployment/deploy.sh
sudo raspi-config nonint enable_overlayfs && sudo reboot
```

## Provisioning a device

Two scripts, because one of them cannot run on the device — an ext4 filesystem can only be
shrunk while it is unmounted, and the Pi expands the root partition the first time it boots.

```bash
# on the development machine, card in a reader, before the first boot
sudo pi-deployment/prepare-card.sh /dev/sdX

# on the device
sudo pi-deployment/provision.sh
```

`provision.sh` **is** the specification of a device (NFR-21): if a setting is not in it, it does
not exist on a shipped unit. It is idempotent, so rerunning it after a change is the normal way
to use it. Building the shipped image from a provisioned device — including the scrub that
keeps maintainer WiFi profiles and SSH host keys out of it — is
[docs/image-build.md](docs/image-build.md).

## License

**GPL-2.0** — see [LICENSE](LICENSE).

MatrixOS statically links rpi-rgb-led-matrix, whose source headers place it under the GNU
GPL "version 2" with no "or later" clause. A distributed build is therefore a derived work,
which rules out GPLv3 and makes GPL-2.0 the licence that works.
