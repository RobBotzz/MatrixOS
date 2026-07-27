# Requirements

This document defines _what_ MatrixOS must do and the constraints it operates under.
_How_ it is structured is [architecture.md](architecture.md); _when_ things get built is
[roadmap.md](roadmap.md).

Priorities use MoSCoW: **M**ust (blocker for the milestone that owns it), **S**hould (needed soon after),
**C**ould (nice, unscheduled), **W**on't (explicitly out of scope for now).

---

## 1. Product definition

MatrixOS turns a 64x32 LED matrix panel into a small ambient information and play device.
It is operated by a rotary encoder and a single home button. At any moment exactly one app is
on screen; the home button takes the user back to the launcher to pick another one.

### 1.1 Goals

- **G1** — A pleasant always-on desk object: readable at a glance, no visible flicker.
- **G2** — Adding a new app is cheap: implement one interface, register it, done.
- **G3** — The project is developable without the hardware attached.
- **G4** — Every design decision is recorded, so the codebase stays explainable as it grows.

### 1.2 Non-goals

Naming a non-goal is how this project stays fast. These are not "later maybe" — they are
things we deliberately do not build until a concrete, present need forces the issue.

- **NG1** — Not a real operating system. "OS" is a metaphor for the app host and shell.
  No scheduler, no processes per app, no kernel work.
- **NG2** — No multitasking. One app runs; others do not tick in the background.
- **NG3** — No widget/layout framework. Apps draw pixels and text directly until at least
  three apps duplicate the same layout logic (rule of three).
- **NG4** — No plugin system, no dynamic app loading, no scripting language for apps.
  Apps are compiled in.
- **NG5** — No multi-panel or chained-panel support. Panel geometry is a configuration
  value, but only 64x32/chain=1 is supported and tested.
- **NG6** — No multi-user or remote-control notions beyond what a single named app needs.
- **NG7** — No content-dense apps. Chess puzzles and Wordle are **out of scope**: a
  chessboard on 64x32 leaves roughly 4x4 pixels per square, and stepping through 64 squares
  or 26 letters with a single encoder is tedious rather than fun. Decided up front instead
  of discovered mid-implementation. Revisit only if the panel changes.

---

## 2. Constraints

| ID  | Constraint                                                                            | Consequence                                                                                                                                                |
| --- | ------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| C-1 | Target is a Pi Zero 2 W (4x Cortex-A53 @ 1 GHz, 512 MB RAM), 64-bit Raspberry Pi OS   | Build target is `aarch64`. 32-bit ARM builds are not supported; the toolchain file must say so.                                                            |
| C-2 | rpi-rgb-led-matrix maps `/dev/mem` directly                                           | The binary needs root (or setuid). Deployment must account for this.                                                                                       |
| C-3 | The matrix library runs a high-priority updater thread and competes for stable timing | Onboard sound must be disabled, GPIO slowdown must be tuned, and a CPU core should be isolated. Input handling must not disturb this.                      |
| C-4 | rpi-rgb-led-matrix is GPLv2 and is statically linked                                  | Any distributed build is a derived work. The library is **GPLv2 only** — its headers say "version 2" with no "or later" clause — so GPLv3 is excluded and MatrixOS must be GPL-2.0. A `LICENSE` file is required before publication.                      |
| C-5 | Development happens on x86 Linux (WSL2), where the panel cannot be driven at all      | A non-hardware display backend is mandatory, not optional. See [ADR-0002](adr/0002-display-abstraction-and-simulator.md).                                  |
| C-6 | One rotary encoder with a push button, plus a dedicated home button                        | Every in-app interaction must be expressible through rotate / press / double-press / hold. The home button does nothing but toggle between the active app and the launcher — see [ADR-0009](adr/0009-dedicated-home-button.md).                                                           |
| C-7 | 64x32 pixels                                                                          | Text needs pixel fonts (4x6, 5x7 from the library's `fonts/` directory). Content-dense apps do not fit and are excluded (NG7). |
| C-8 | The Pi Zero 2 W has a single WiFi radio | Access-point mode and client mode are sequential, not simultaneous. Provisioning must switch modes and fall back to the access point on failure. See [ADR-0007](adr/0007-appliance-provisioning.md). |
| C-9 | The Pi has no real-time clock | Wall-clock time depends on NTP and therefore on WiFi. Any app that shows time needs an "unknown time" state. |
| C-10 | Devices are built and flashed by the maintainer in single-digit quantities, handed to non-technical users, with no update channel | Setup must work without a terminal, and every failure must be visible on the panel. A defect means physically reflashing a card, so the image must be rebuildable from scratch. At this scale Spotify's development-mode user allow-list is sufficient. |

---

## 3. Functional requirements

### 3.1 Platform — display

| ID   | Requirement                                                                                                                                                                          | Prio |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---- |
| FR-1 | The system renders to a 64x32 RGB surface at a fixed target frame rate.                                                                                                              | M    |
| FR-2 | Rendering goes through a display abstraction with at least two implementations: the real LED panel and a development simulator. Application code must not reference the LED library. | M    |
| FR-3 | Frames are double-buffered: an app draws into a back buffer that is presented atomically, so no partially drawn frame is ever visible.                                               | M    |
| FR-4 | On shutdown (SIGINT/SIGTERM) the panel is cleared before the process exits.                                                                                                          | M    |
| FR-5 | Panel geometry, brightness, and hardware tuning flags are supplied at startup, not hardcoded.                                                                                        | M    |
| FR-6 | Global brightness can be changed at runtime, including a night mode / dimming schedule.                                                                                              | C    |

### 3.2 Platform — input

| ID    | Requirement                                                                                                                                                                                                                                   | Prio |
| ----- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---- |
| FR-7  | Encoder rotation, the encoder's push button and the home button are translated into a device-independent event stream. Apps consume events, never GPIO.                                                                                                                     | M    |
| FR-8  | The event vocabulary is: `Rotate(delta)`, `Press`, `DoublePress`, `LongPress`, and `Home`.                                                                                                                                           | M    |
| FR-9  | Rotation is debounced and direction-stable: at normal turning speed no detent is lost and no false reversal is reported.                                                                                                                      | M    |
| FR-10 | `LongPress` fires as soon as the hold threshold (600 ms) is reached, while the button is still held, so an app can react immediately. Once it has fired, the following release does not additionally produce `Press`. No gesture is reserved for the shell. | M    |
| FR-11 | The simulator provides a keyboard-driven input backend (arrow keys for rotation, space for the encoder button, `h` for home) so apps are usable without the hardware.                                                                                                | M    |
| FR-12 | Rotation while the button is held is available as a distinct gesture for value adjustment.                                                                                                                                                    | C    |

> **Why `LongPress` is responsive:** a dedicated home button
> ([ADR-0009](adr/0009-dedicated-home-button.md)) means the shell no longer has to claim a
> gesture, so nothing competes with an app's hold. `Home` is delivered to the shell, never to
> an app. If an app ever needs to know how long a hold lasted, `PressDown`/`PressUp` can be
> added without ambiguity — but only then.

### 3.3 Platform — shell and app lifecycle

| ID    | Requirement                                                                                                                                        | Prio |
| ----- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ---- |
| FR-13 | Exactly one app is active at a time. The shell drives it with a fixed tick and a render call.                                                      | M    |
| FR-14 | An app has a lifecycle: it is entered, ticked and rendered while active, and exited. It receives no ticks while inactive.                          | M    |
| FR-15 | A launcher lists all registered apps; rotating scrolls the list, pressing starts the selected app.                                                 | M    |
| FR-16 | The home button toggles: pressed in an app it shows the launcher, pressed in the launcher it returns to the app it came from. If no app has run yet, it stays in the launcher. `Home` is never delivered to an app.                                                                                             | M    |
| FR-17 | An unhandled exception thrown by an app terminates that app, is logged, and returns the user to the launcher — it does not take down the device.   | M    |
| FR-18 | Registering a new app requires touching exactly one registration point; no changes to shell, launcher, or build wiring beyond adding source files. | M    |
| FR-19 | The device restores the last active app after a restart.                                                                                           | C    |
| FR-20 | The launcher shows apps as icons/previews rather than a text list.                                                                                 | C    |

### 3.4 Platform — configuration, persistence, logging

| ID    | Requirement                                                                                               | Prio |
| ----- | --------------------------------------------------------------------------------------------------------- | ---- |
| FR-21 | The shell logs to stdout/stderr in a form that `journalctl` can capture, with severity levels.            | M    |
| FR-22 | Apps can persist and read back small amounts of state (settings, high scores) in a per-app namespace.     | S    |
| FR-23 | Persisted state survives an unclean power loss without corrupting the store beyond the last write.        | S    |
| FR-24 | Secrets (API tokens) are stored outside the repository, are not world-readable, and never appear in logs. | S    |
| FR-25 | A settings app exposes global options (brightness, active app, time zone).                                | C    |

### 3.5 Platform — network foundation (not v0.1)

| ID    | Requirement                                                                                                                                                                                     | Prio |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---- |
| FR-26 | Apps never perform I/O themselves. They read from a data provider handed to them, so the provider's implementation (in-process HTTP vs. separate service) can change without touching app code. | M    |
| FR-27 | No network operation may block the render loop. Fetching happens off the render thread with timeouts.                                                                                           | S    |
| FR-28 | When data is stale or unreachable, an app shows the last known value with a staleness indicator instead of an error screen or a blank panel.                                                    | S    |
| FR-29 | OAuth-based apps can obtain and refresh tokens without re-flashing the device.                                                                                                                  | C    |

> FR-26 is the discipline rule that keeps [ADR-0004](adr/0004-network-app-runtime.md)
> deferrable at zero cost. It applies from the first app, even though no app needs data yet.

### 3.6 Apps

Concrete app requirements live with each app in [roadmap.md](roadmap.md). The only app in
scope for v0.1:

| ID    | Requirement                                                                                        | Prio |
| ----- | -------------------------------------------------------------------------------------------------- | ---- |
| FR-30 | One self-contained animation app with no external dependencies, using rotation to switch variants. | M    |

### 3.7 Appliance and provisioning (v0.4)

The device must be usable by someone who did not build it. Decided in
[ADR-0007](adr/0007-appliance-provisioning.md) and
[ADR-0008](adr/0008-power-loss-resilience.md).

| ID    | Requirement | Prio |
| ----- | ----------- | ---- |
| FR-31 | The device ships as a pre-built, flashable SD card image. The user never installs an operating system. | M |
| FR-32 | On first boot the device derives a unique identity — hostname and setup access-point name — from the CPU serial, so cloned images do not collide on one network. | M |
| FR-33 | With no known WiFi network available, the device opens its own access point and serves a setup page where the user selects a network and enters the password. | M |
| FR-34 | After credentials are entered the device joins the network. On failure it returns to access-point mode and reports the failure. | M |
| FR-35 | The panel shows the current setup state (setup mode, connecting, connected, failed) so the user knows what to do. Setup is implemented as an app, not as a separate mode. | M |
| FR-36 | Once online the device is reachable on the local network under a stable name via mDNS and serves a configuration page. | M |
| FR-37 | Apps that need an external account are linked from that page. The user never edits a file, types a credential into a config, or opens a terminal. | M |
| FR-38 | The platform acquires, stores and refreshes tokens. Apps never see them — this is FR-26 applied to credentials. | M |
| FR-39 | All device state — WiFi credentials, tokens, app settings, scores — lives in a single writable location, separate from the read-only system and from the binary. | M |
| FR-40 | State writes are atomic: write a temporary file, `fsync`, then `rename` onto the target, so an interrupted write leaves either the old or the new value. | M |
| FR-41 | The running version is visible on the configuration page, so a reported problem can be traced to a build. | S |
| FR-42 | The user can reset the device to its unconfigured state — forget WiFi and tokens — without a terminal. | S |

---

## 4. Non-functional requirements

Numbers are targets to validate in the v0.1 hardware spike, not measurements. Where a
target turns out to be wrong, change the number here rather than quietly missing it.

### 4.1 Performance

| ID    | Requirement                                                                                                                               |
| ----- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| NFR-1 | Target 60 FPS for animation apps; 30 FPS is the floor below which the frame rate is considered a defect. Frame budget at 60 FPS: 16.6 ms. |
| NFR-2 | Frame pacing is stable: no visible stutter, and app logic uses an explicit delta time so behaviour is frame-rate independent.             |
| NFR-3 | End-to-end input latency (encoder detent to visible reaction) ≤ 50 ms.                                                                    |
| NFR-4 | Resident memory ≤ 64 MB (of 512 MB total), leaving headroom for network apps and the OS.                                                  |
| NFR-5 | No visible flicker under sustained rendering. This is the acceptance criterion for the timing setup in C-3.                               |

### 4.2 Reliability

| ID    | Requirement                                                                       |
| ----- | --------------------------------------------------------------------------------- |
| NFR-6 | The device runs unattended for ≥ 7 days without manual intervention.              |
| NFR-7 | MatrixOS starts automatically on boot and is restarted automatically if it exits. |
| NFR-8 | Time from power-on to something visible on the panel ≤ 30 s.                      |
| NFR-9 | A failing app never requires a power cycle (see FR-17).                           |

### 4.3 Developer experience

| ID     | Requirement                                                                                            |
| ------ | ------------------------------------------------------------------------------------------------------ |
| NFR-10 | The full project builds and runs on the development machine without the Pi and without root.           |
| NFR-11 | Edit-to-visible-result on the development machine is under 30 s.                                       |
| NFR-12 | App logic is unit-testable without a display; rendering is testable via deterministic frame snapshots. |
| NFR-13 | CI builds both the host and the aarch64 target and runs the test suite on every push and pull request. |
| NFR-14 | A single documented command deploys a build to the device.                                             |

### 4.4 Maintainability

| ID     | Requirement                                                                                                                                                                                                        |
| ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| NFR-15 | Consistent formatting enforced mechanically (`.clang-format`), not by review.                                                                                                                                      |
| NFR-16 | Every architectural decision with a plausible alternative gets an ADR before the code lands.                                                                                                                       |
| NFR-17 | No abstraction is introduced without either a second real implementation or three concrete duplications. The display and input HAL are the acknowledged exception — they have two implementations by design (C-5). |
| NFR-18 | Documentation and code are English. Commit messages are English, imperative mood.                                                                                                                                  |

### 4.5 Appliance and durability

| ID     | Requirement |
| ------ | ----------- |
| NFR-19 | Pulling the power at any moment is a supported way to switch the device off. It must not corrupt the system, and must lose at most the most recent state write. |
| NFR-20 | In normal operation nothing is written to the SD card: no swap, logs in RAM only, read-only root filesystem. Only explicit state writes touch persistent storage. |
| NFR-21 | A device can be rebuilt from a blank card by flashing Raspberry Pi OS and running one provisioning script. No undocumented manual steps exist. |
| NFR-22 | The shipped image contains no maintainer secrets: no WiFi profiles, no shared SSH host keys, no tokens, no shell history. |
| NFR-23 | A non-technical user can complete setup with only a phone and a short printed instruction. Every failure state is visible on the panel rather than only in a log. |

> **Consequence of NFR-20 worth stating plainly:** with swap disabled and the root filesystem
> overlaid in RAM, memory replaces card wear as the binding limit. Anything that would
> previously have worn out the card now consumes RAM, and an unbounded writer causes an
> out-of-memory kill instead of slow degradation. NFR-4's 64 MB budget therefore stops being a
> rough target and becomes a real constraint.

---

## 5. Acceptance criteria for v0.1

v0.1 is done when all of the following hold:

1. The animation app runs on the panel at ≥ 30 FPS with no visible flicker (NFR-1, NFR-5).
2. The same binary source runs in the terminal simulator on the development machine,
   keyboard-controlled (FR-2, FR-11, NFR-10).
3. Rotating the encoder switches animation variants; the home button toggles to the launcher
   and back; the launcher can start the app again (FR-8, FR-15, FR-16).
4. Turning the encoder briskly for 10 detents moves the launcher selection by exactly
   10 positions in the right direction (FR-9).
5. `Ctrl-C` and `systemctl stop` leave the panel dark (FR-4).
6. The service starts on boot and survives a forced kill (NFR-7).
7. CI is green for host and aarch64 builds and runs at least one meaningful test (NFR-13).
8. A `LICENSE` file exists and is GPLv2-compatible (C-4).

---

## 6. Known open questions

| ID  | Question                                                                                                                                                                       | Needed by          |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------ |
| Q-1 | ~~Which GPIO pins remain free for the encoder?~~ **Answered — see Resolved below.** What remains for the spike: confirm the chosen pins behave on the actual board. | v0.1 spike         |
| Q-2 | ~~What `--led-slowdown-gpio` value does this panel need?~~ **Answered — see Resolved below.**                                                                                             | v0.1 spike         |
| Q-3 | ~~What is the panel's multiplex/scan type?~~ **Answered — see Resolved below.**                    | v0.1 spike         |
| Q-4 | ~~Encoder reading strategy: polling or edge events?~~ **Answered — see Resolved below.**                              | v0.1 spike         |
| Q-5 | ~~Which license?~~ **Answered — see Resolved below.** Remaining action: add the `LICENSE` file (acceptance criterion 8). | v0.1 |
| Q-6 | In-process HTTP client vs. separate service for network apps. Deliberately deferred, see [ADR-0004](adr/0004-network-app-runtime.md).                                          | first network app  |
| Q-7 | In what format is app state stored? Only the format is open — the location is decided (FR-39, one writable area separate from the system) because v0.4 makes the root filesystem read-only and retrofitting that is expensive.                                                      | v0.3               |
| Q-8 | ~~Two-tier hold or a dedicated home button?~~ **Answered — see Resolved below.** | — |
| Q-9 | What exactly does the panel show during setup, and is a WiFi-join QR code legible at 64x32? A version-2 code plus quiet zone needs roughly 33x33 modules, so it is marginal — test with real phones before designing around it. | v0.4 |
| Q-10 | Do Spotify or Strava support the OAuth device authorization grant (RFC 8628)? If either does, the static redirect page in [ADR-0007](adr/0007-appliance-provisioning.md) becomes unnecessary for it. | v0.6 |

### Resolved

**Q-1 — free GPIO pins.** With `hardware_mapping = "regular"` and `parallel = 1`, the library
claims only the control, address and chain-0 pins: **18** (OE), **17** (clock), **4** (strobe),
**22/23/24/25** (address A–D), **15** (address E, needed only for 1:32 scan) and
**11/27/7/8/9/10** (RGB). Verified in `lib/hardware-mapping.c`; `Framebuffer::InitGPIO` in
`lib/framebuffer.cc` only ORs the chain-1 and chain-2 bits into `all_used_bits` when
`parallel >= 2` and `>= 3`, so with a single chain those pins stay untouched.

Free and usable: **12, 5, 6, 19, 13, 20** (chain 1) and **14, 2, 3, 26, 16, 21** (chain 2).
Proposed assignment: **GPIO 5, 6, 13** for the encoder (A, B, switch) and **GPIO 19** for the
home button ([ADR-0009](adr/0009-dedicated-home-button.md)), which still leaves 12, 20 and all
of chain 2 spare. Avoid 2 and 3 (fixed I²C pull-ups) and 14/15 (UART). Valid only while
`parallel = 1`, which NG5 guarantees.

**Q-5 — license.** The vendored library is **GPLv2 only**: every source header reads "as
published by the Free Software Foundation version 2" with no "or later" clause. GPLv3 is
therefore not an option, and MatrixOS must be **GPL-2.0**. See C-4.

**ADR-0002 residual risk — does the library compile for x86?** Yes. All 13 translation units
listed in `CMakeLists.txt` compile cleanly with `g++ 15 -std=c++20` on x86-64 Linux. The host
build is therefore not blocked by the library, and the display abstraction rests on its other
justifications (see [ADR-0002](adr/0002-display-abstraction-and-simulator.md)).

**Q-8 — two-tier hold or a home button?** A dedicated home button, decided before any input
code was written. `VeryLongPress` is gone from the vocabulary, `LongPress` fires while the
button is held, and the button's only job is toggling between app and launcher. See
[ADR-0009](adr/0009-dedicated-home-button.md), which supersedes ADR-0006.

**Q-2 — GPIO slowdown.** No flag needed. On a Zero 2 W the library's own default is already
the fastest setting (`gpio_slowdown(GPIO::IsPi4() ? 2 : 1)` in `lib/options-initialize.cc`),
and values 1 and 2 produced no visible difference on this panel — so the panel tolerates the
full speed and there is nothing to compensate for. The mechanism being tuned is a busy-wait
that repeats a no-op register write to stretch each clock pulse (`lib/gpio.h`).

Measured afterwards with `--led-show-refresh`: the refresh rate **is** noticeably better at 1
than at higher values. So the parameter does take effect — it simply has no *visible* effect
on a still image, which is exactly the weakness predicted for a static test pattern. The
conclusion stands and is now backed by a measurement rather than an impression: leave the flag
off and keep the default of 1, because a higher refresh rate means less flicker and more
usable brightness.

Still to confirm once the animation runs on the panel: whether the refresh rate the default
gives is enough for flicker-free rendering (NFR-5). A moving image reveals timing problems a
still one hides.

**Device setup steps** for the matrix timing prerequisites of C-3 — sound disabled, module
blacklisted, `isolcpus=3` — are recorded in [device-setup.md](device-setup.md), which is the
source material for `provision.sh` in v0.4.

**Q-3 — panel scan type.** No override needed. The `--test-pattern` frame rendered correctly
on the first try: border closed on all four sides, corner marker in the top-left, colour bands
in red / green / blue order. That rules out a wrong scan type, a mirrored or rotated panel and
a swapped channel order in one look, so neither `--led-multiplexing` nor
`--led-row-addr-type` nor `--led-rgb-sequence` is required. This is what the test pattern was
designed to answer.

**Q-1, remaining half.** The panel pins are confirmed working by the above. The pins reserved
for the encoder (5 / 6 / 13) and the home button (19) are still unwired and therefore
unverified on real hardware.

**Q-4 — encoder reading strategy.** Kernel **edge events** through the GPIO v2 character
device, read non-blocking once per frame. No polling of pin levels, no extra thread, no
third-party library.

Why this settles the worry behind the question: the kernel timestamps every transition and
buffers them, so a 60 Hz read cannot lose a detent however briskly the knob is turned — which
is what FR-9 demands. Polling levels at 60 Hz would genuinely have missed transitions, because
a fast turn produces them faster than that. A dedicated thread polling at 1 kHz would have
worked too, but it would compete for CPU with the panel's refresh thread (C-3) for no benefit.

No dependency was needed: `linux/gpio.h` ships with the aarch64 cross toolchain and the ioctl
interface is a stable kernel ABI. libgpiod would only wrap it, at the cost of cross-compiling
another library.

The timing constants — 600 ms for a hold, 10 ms of debounce, four signal transitions per
detent — live in `GestureRecognizer::Timing` and `QuadratureDecoder`, and both are covered by
tests that feed synthetic timestamps rather than waiting.

**Note on `DoublePress` (FR-8).** The encoder backend does not produce it. A single press can
only be confirmed once the window for a second one has elapsed, so enabling double-press
detection delays *every* press by that window — roughly 300 ms. No app needs the gesture yet
(Spotify in v0.6 is the first), so the default keeps a press instant. The detection is
implemented and tested; only the default window of zero switches it off. The keyboard backend
still emits it on a separate key for testing.
