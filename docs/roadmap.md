# Roadmap

Status: draft, 2026-07-25.

Releases are ordered by **which platform capability they unlock**, not by which app sounds
most fun. Each milestone is picked so that at most one hard new capability is introduced at
a time, and every milestone ends with something visibly working on the panel.

---

## Capability map

This is the reason for the ordering. Each app in the backlog needs certain platform
capabilities; building the app is how that capability gets built and proven.

| Capability | First unlocked by | Hard part |
| --- | --- | --- |
| Render loop, display HAL, simulator | Plasma | Frame pacing, flicker-free timing on hardware |
| Input events, gesture recognition | Launcher | Debouncing rotation and two buttons, press/double-press/hold timing |
| Full gesture vocabulary + state machines | Pomodoro | Modal UI driven by one encoder |
| Responsive game loop | Snake, Pong | Input latency, collision, difficulty pacing |
| Persistence | Settings, high scores | Store format, crash safety |
| Wall-clock time & time zones | Clock | NTP dependency, DST, no real-time clock in the device |
| Bootable image, reproducible provisioning | Appliance | Clone hygiene, unique identity per unit |
| WiFi onboarding without keyboard or screen | Appliance | Single radio: access point and client mode are sequential |
| Embedded HTTP server | Appliance | Platform infrastructure: setup portal, config page, OAuth callback — later reused for uploads |
| Power-loss tolerance, flash longevity | Appliance | RAM budget replaces card wear as the binding limit |
| HTTP client, JSON, TLS cross-compilation | Weather | Cross-compiling curl + TLS with a Pi sysroot |
| Async data with graceful staleness | Weather | Not blocking the render loop |
| OAuth token lifecycle, secret storage | Spotify | No browser on the device; static HTTPS redirect page |
| Image/GIF decoding & scaling to 64x32 | GIF & Text | Dependency choice, quality at tiny resolution |
| Upload handling & storage limits | GIF & Text | How many uploads, eviction policy |

---

## v0.1 — Platform + one animation app

**Goal:** prove the whole vertical slice, from encoder to pixel, on both hardware and host.

Scope:

- Hardware spike answering Q-1 to Q-4 (free GPIOs, GPIO slowdown, scan type, encoder
  reading strategy).
- Display HAL with matrix and terminal-simulator backends.
- Input HAL with an encoder + home-button backend and a keyboard backend, plus hardware-free
  gesture recognition.
- Shell: tick loop, app lifecycle, exception boundary, logging.
- Launcher as an app.
- Plasma (or comparable) animation app; rotation switches variants.
- `systemd` unit, autostart, clean shutdown.
- CI: host build + tests, aarch64 cross build, format check.
- `LICENSE` (GPLv2-compatible, C-4).
- Repository groundwork listed below.

Done when the acceptance criteria in [requirements.md §5](requirements.md#5-acceptance-criteria-for-v01)
are met.

### Groundwork — done

Inconsistencies that existed before this milestone started, cleared on 2026-07-26:

- [x] `pi-toolchain.cmake` declared `CMAKE_SYSTEM_PROCESSOR arm` while CI cross-compiles for
      `aarch64` — now `aarch64` (C-1).
- [x] `main.cpp` set `rows = 32` with no column count; now `cols = 64` for the actual panel
      (FR-5).
- [x] The `pi-zero-win` preset contained the placeholder `REPLACE WITH ARCH x64 COMPILER`.
      Replaced by a working `pi-zero` preset for local cross-compilation; compilers and
      generator moved into the shared `pi-zero-base`, and `pi-zero-ci` now differs only by
      `ccache`.
- [x] **Found while fixing the above:** the `default` preset used the `MinGW Makefiles`
      generator, a leftover from developing on Windows. It could not have configured on WSL
      at all. Now `Unix Makefiles`.
- [x] `pi-deployment/deploy.sh` used an undefined `$TEMP_ZIP` and took `artifacts[0]`, which
      can be a pull-request build. It now resolves the newest *successful* run on `main`,
      creates a temporary file properly, checks its prerequisites, and restores the
      executable bit that artifact zips drop.
- [x] The CI `test` job did not check out the repository and only echoed a TODO. Removed —
      an always-green check that verifies nothing is worse than no check. In its place the
      build job now asserts that the produced binary really is `ARM aarch64`, which is a
      direct regression test for the first item on this list.
- [x] `README.md` documents building again (host and cross), replacing the notes that were
      removed from tracking in 37d20f4.
- [x] `.clang-format` added, derived from the style already in the repository — 4 spaces,
      Allman braces (NFR-15).

### Progress

Done, 2026-07-26:

- [x] `LICENSE` (GPL-2.0, see the resolved Q-5).
- [x] Source tree moved to `src/`, `CMakeLists.txt` split into the targets from
      [architecture.md §6.1](architecture.md#61-build-targets) with `aarch64` auto-detection.
      `matrixos_core` deliberately does not link the LED library, so the layering is enforced
      by the compiler.
- [x] `Surface` and `Color` — the plain RGB24 buffer apps will draw into (FR-2, FR-3).
- [x] `Display` interface with the terminal simulator backend (ANSI half-blocks) and the LED
      panel backend using `FrameCanvas` + `SwapOnVSync`.
- [x] Catch2 v3 via FetchContent, host-only; 7 tests over `Surface`, all green.
- [x] `main.cpp` as composition root, drawing a diagnostic test pattern — border, corner
      marker and three colour gradients, so wrong geometry, a mirrored panel or a swapped
      channel order are visible at a glance. Verified in the simulator.
- [x] CI in three jobs: formatting (pinned `clang-format` 21.1.8), host build with tests, and
      the aarch64 cross build with an architecture assertion. The host job intentionally does
      not check out the submodule — if it ever needs it, the layering broke.

Done, 2026-07-27:

- [x] `hal/input.h` — the event vocabulary from FR-8 after ADR-0009: `Rotate`, `Press`,
      `DoublePress`, `LongPress`, `Home`. No `VeryLongPress`.
- [x] `os/app.h` — the App interface: `onEnter`, `onExit`, `onInput`, `update(dt)`,
      `render(Surface&)`.
- [x] `os/log` — leveled logging. Everything goes to **stderr**, because the terminal
      simulator owns stdout and one stray log line would corrupt the picture.
- [x] `os/shell` — tick loop with frame pacing (measured: exactly 60 frames per second),
      app lifecycle, and the exception boundary from FR-17. The stop condition is injected,
      which is what lets a test run an exact number of frames.
- [x] `hal/sim/keyboard_input` — arrow keys rotate, space presses, `h` is home. Degrades to
      producing no events when stdin is not a terminal, so it is harmless as a service.
- [x] `apps/plasma` — four animated variants; rotating switches them, pressing freezes the
      animation. Both chosen so the whole input path is visible at a glance.
- [x] The test pattern survives as `--test-pattern`: with several devices to build, every
      panel needs checking once without depending on a running app.
- [x] 21 tests, all green — including "an app that throws is dropped and the shell keeps
      running" and "Home never reaches an app".
- [x] `systemd` unit in `pi-deployment/matrixos.service` (NFR-7), documented in
      [device-setup.md](device-setup.md).

Known intermediate state: when an app is dropped the screen goes black, because FR-17 wants
the user returned to the launcher and there is no launcher yet.

Next, and the last piece of v0.1: the encoder and home-button backend on the pins from the
resolved Q-1, the gesture recognizer (hardware-free and unit-tested), and the launcher.

---

## v0.2 — Interaction depth

**Unlocks:** the full gesture vocabulary and modal app UIs.

- **Pomodoro** — rotate to set duration, press to start/pause, hold to configure, panel
  flashes when time is up. Exercises every gesture and the first non-trivial state machine.
- **Bit clock** or **Clock** — wall-clock time, time zone handling.
- Refinement of the launcher based on the first real usage.

---

## v0.3 — Games and persistence

**Unlocks:** responsive input under load, and storing state.

- **Snake** — rotate to turn, press to start.
- **Pong** — rotate to move the paddle.
- Persistence for high scores and settings (FR-22, FR-23).
- **Settings** app: brightness, default app, time zone (FR-25).

Deferring persistence to here is intentional: by this point three concrete consumers exist,
so the store can be designed against real needs instead of guesses (Q-7).

One thing must be right the first time: state goes into a **single writable location**,
separate from the binary (FR-39). v0.4 makes the root filesystem read-only, and retrofitting
that is far more work than getting the path right now. Writes are atomic from the start
(FR-40) — it is ten lines and it is what protects the store when the plug is pulled.

---

## v0.4 — Appliance

**Unlocks:** the project stops being a program on the maintainer's Pi and becomes a device
someone else can switch on. Decided in
[ADR-0007](adr/0007-appliance-provisioning.md) and
[ADR-0008](adr/0008-power-loss-resilience.md).

- `provision.sh` — one script that turns a fresh Raspberry Pi OS install into a MatrixOS
  device. No manual configuration step outside the script.
- Golden image via `dd` + PiShrink, including the pre-clone scrub: no maintainer WiFi
  profile, no shared SSH host keys, no history, no logs, no state. pi-gen stays deferred.
- First boot: expand the filesystem, derive hostname and setup-AP name from the CPU serial.
- WiFi provisioning: access point plus captive portal, switch to client mode, return to the
  access point on failure and say so.
- Embedded HTTP server as platform infrastructure. Configuration page reachable at
  `matrixos-xxxx.local` via mDNS, showing the running version.
- Setup as an app: panel states for setup mode, connecting, connected, failed (Q-9).
- Read-only root via the overlay filesystem, separate writable state partition, atomic state
  writes.
- `journald` volatile with a 16 MB cap, swap disabled, `noatime`.
- Factory reset without a terminal — forget WiFi and tokens.

Explicitly **not** in scope: updates for shipped devices (see the addendum in
[ADR-0005](adr/0005-deployment-model.md)), and the OAuth flow — that arrives with the first
app that needs it, in v0.6.

**Done when** someone who has never seen the device can take it, a power supply, and their
phone, and reach a working clock on the panel — and when pulling the plug at any moment
leaves the device intact.

---

## v0.5 — Network foundation

**Unlocks:** the hardest infrastructure step in the project.

- Resolve [ADR-0004](adr/0004-network-app-runtime.md): in-process HTTP client vs. separate
  service. This milestone is the trigger point.
- Cross-compile or otherwise provide TLS + HTTP + JSON for aarch64.
- Off-loop fetching with timeouts (FR-27) and graceful staleness (FR-28).
- **Weather** — rotate to change the forecast window, hold to change location.

Weather is the right first network app: a plain public API, no OAuth, and a failure mode
(stale forecast) that is harmless.

---

## v0.6 — Authenticated services

**Unlocks:** OAuth token lifecycle and secret handling.

- **Spotify** — now-playing with cover art, press to pause/resume, double-press to skip,
  rotate for volume.
- **Strava** — friends' statistics, rotate to change view.
- Token storage and refresh without re-deploying (FR-29), secrets hygiene (FR-24).

Spotify also needs album-art scaling to 64x32, which overlaps with v0.7.

The OAuth mechanics are described in [ADR-0007](adr/0007-appliance-provisioning.md): the
device's own configuration page starts the flow, a static HTTPS page redirects the
authorization code back to the device on the LAN, and pasting the code by hand is the
fallback that depends on nothing. Verify Q-10 first — if either service supports the device
authorization grant, the static page is unnecessary.

---

## v0.7 — Content from outside

**Unlocks:** an embedded HTTP server and image decoding.

- **GIF & Text** — send an image or GIF plus a short caption to the device from a phone or
  browser; the device serves the upload page itself.
- Image/GIF decode and downscale to 64x32.
- Storage management (how many uploads, eviction policy).

---

## Backlog — unscheduled

Apps that are wanted but not yet placed in a milestone. Ordering within this list is not
meaningful.

### Fits the hardware well

| App | Interaction | Needs |
| --- | --- | --- |
| GIF & Time | Press to change the animation | Nothing beyond v0.2 |
| Plasma / Game of Life / Lissajous / fire / rain / DVD-bounce screensavers | Rotate to switch, press to change palette | Nothing; these are Plasma variants |
| Breakout / Arkanoid | Rotate to move the paddle | v0.3 game loop |
| Memory | Rotate over cards, press to reveal | v0.3 persistence for best times |
| 2048 | Rotate to pick a direction, press to commit | Input mapping design — four directions on one encoder |
| Morse trainer | Press for dots and dashes | Nothing |
| Stock ticker | Rotate to change symbol | v0.5 network |
| Collatz visualiser, infinite terrain | Rotate to change the seed | Nothing |
| ASCII art | Rotate to browse | Font work |

## Dropped

Not unscheduled — excluded, per NG7 in [requirements.md](requirements.md). Recorded so the
question does not resurface.

| App | Reason |
| --- | --- |
| Chess puzzles | A board plus piece glyphs on 64x32 leaves roughly 4x4 pixels per square, and stepping an encoder through 64 squares is tedious. Reshaping it enough to fit (cropped board, mate-in-one only) would leave something that is no longer chess. |
| Wordle | Five columns of six rows plus a letter picker is not legible at this size, and selecting from 26 letters by rotation is slow. |

The panel size is the reason, so this only changes if the hardware does. Both would also have
been the only apps needing datasets shipped alongside the binary — dropping them removes that
capability requirement entirely.
