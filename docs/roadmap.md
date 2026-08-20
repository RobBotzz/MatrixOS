# Roadmap

Status: v0.1 to v0.3 complete; v0.4 built, its setup flow verified on the development Pi, its
provisioning not yet tested. Last revised 2026-08-20.

Releases are ordered by **which platform capability they unlock**, not by which app sounds
most fun. Each milestone is picked so that at most one hard new capability is introduced at
a time, and every milestone ends with something visibly working on the panel.

---

## Capability map

This is the reason for the ordering. Each app in the backlog needs certain platform
capabilities; building the app is how that capability gets built and proven.

| Capability                                 | First unlocked by     | Hard part                                                                                     |
| ------------------------------------------ | --------------------- | --------------------------------------------------------------------------------------------- |
| Render loop, display HAL, simulator        | Plasma                | Frame pacing, flicker-free timing on hardware                                                 |
| Input events, gesture recognition          | Launcher              | Debouncing rotation and two buttons, press/double-press/hold timing                           |
| Full gesture vocabulary + state machines   | Pomodoro              | Modal UI driven by one encoder                                                                |
| Responsive game loop                       | Snake                 | Input latency, collision, difficulty pacing                                                   |
| Persistence                                | Settings, high scores | Store format ([ADR-0011](adr/0011-state-store-format.md)), crash safety                        |
| Wall-clock time & time zones               | Clock                 | NTP dependency, DST, no real-time clock in the device                                         |
| Bootable image, reproducible provisioning  | Appliance             | Clone hygiene, unique identity per unit                                                       |
| WiFi onboarding without keyboard or screen | Appliance             | Single radio: access point and client mode are sequential                                     |
| Embedded HTTP server                       | Appliance             | Platform infrastructure: setup portal, config page, OAuth callback — later reused for uploads |
| Power-loss tolerance, flash longevity      | Appliance             | RAM budget replaces card wear as the binding limit                                            |
| HTTP client, JSON, TLS cross-compilation   | Weather               | Cross-compiling curl + TLS with a Pi sysroot                                                  |
| Async data with graceful staleness         | Weather               | Not blocking the render loop                                                                  |
| Image/GIF decoding & scaling to 64x32      | GIF & Text            | Dependency choice, quality at tiny resolution                                                 |
| Upload handling & storage limits           | GIF & Text            | How many uploads, eviction policy                                                             |
| OAuth token lifecycle, secret storage      | Spotify               | No browser on the device; static HTTPS redirect page                                          |

---

## v0.1 — Platform + one animation app ✅

**Complete, 2026-07-27.** All eight acceptance criteria met and verified on the device.

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

Done when the acceptance criteria in [requirements.md §5.1](requirements.md#51-v01--platform-and-one-animation-app)
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
      can be a pull-request build. It now resolves the newest _successful_ run on `main`,
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

Done, 2026-07-27 (second half of the day):

- [x] `gfx/font` — one embedded 5x7 bitmap font, generated from the public-domain BDF shipped
      with rpi-rgb-led-matrix by `tools/bdf_to_header.py`. The generator is checked in so the
      numbers in `font5x7_data.h` are verifiable rather than magic.
- [x] `os/launcher` — the app menu, itself an `App` (FR-15): rotating moves the selection,
      pressing starts the entry, the selection stays on screen however long the list gets.
      It knows the shell only through a callback.
- [x] Shell: `Home` toggles between the launcher and the app it came from (FR-16). Switching
      is deferred by one frame on purpose, so a request made from inside the launcher's own
      `onInput` cannot exit an app that is still running.
- [x] **FR-17 completed**: a dropped app now lands the user in the launcher instead of at a
      black screen.
- [x] `apps/testpattern` — the diagnostic frame became a normal app, so a panel can be checked
      from the launcher and not only through a startup flag (ADR-0007). `--test-pattern` still
      works and now shares the same code.
- [x] 39 tests, all green. The launcher's scroll invariant, the font's bit order (an `L`
      compared against ASCII art, which a mirrored font would fail) and the Home toggle are
      each covered.

Done, 2026-07-27 (the encoder):

- [x] `hal/quadrature` — the two encoder signals to detents, via a transition table over the
      Gray-code ring. Bounce cancels itself out and physically impossible transitions are
      discarded. Hardware-free, 10 tests.
- [x] `hal/gestures` — button transitions plus timestamps to `Press` and `LongPress`, with
      debouncing. Takes the time as an argument rather than reading a clock, so 11 tests feed
      synthetic instants and none of them waits or flakes.
- [x] `hal/pi/encoder_input` — the GPIO v2 character device with kernel edge events. Answers
      Q-4, and needs no third-party library at all.
- [x] `hal/matrix/` renamed to `hal/pi/`, target `matrixos_hal_pi`: there are now two Pi
      backends and only one of them has anything to do with the LED library.
- [x] `--keyboard` keeps the panel but takes input from stdin, so the device stays drivable
      over SSH — useful for telling a GPIO fault from a fault above it.
- [x] 59 tests, all green.

`DoublePress` is deliberately not produced by default; the reasoning is with Q-4 in
[requirements.md](requirements.md).

**Not verified on hardware** at the time this was written. The decoding and timing logic is
tested, but the ioctl layer cannot be exercised without an encoder wired to GPIO 5/6/13 and a
button on 19. That was the last open item of v0.1 — acceptance criteria 3 and 4, both closed on
the device the same day; see the milestone header above.

---

## v0.2 — Interaction depth ✅

**Complete, 2026-07-27.**

**Unlocks:** the full gesture vocabulary and modal app UIs.

Delivered:

- **Pomodoro** — the first modal app, and the proof of the milestone's goal. Every gesture the
  encoder backend actually produces now has a consumer: `Rotate` sets the durations, `Press`
  advances the cycle, `LongPress` resets from anywhere, and `Home` is handled a level up by the
  shell. `DoublePress` stays deliberately unproduced (the note on FR-8 in
  [requirements.md](requirements.md) says why), so the vocabulary is complete in the sense that
  matters: nothing the hardware emits is unused.
- **`gfx/font` gained a `scale` parameter**, which is what made 10x14 digits possible.
- 17 Pomodoro tests, 81 in the suite, host and aarch64 builds clean, formatting enforced.

Two items left this milestone rather than finishing inside it, both recorded where they went:

- **The clock** moved to v0.4 — see below. It was here for a platform capability nothing needs
  yet, which is the one thing this project is built to avoid.
- **Launcher refinement** was the open question, and real usage answered it: the current launcher
  meets FR-15 and nothing about it is broken, but it should eventually become a card carousel.
  That is polish, not a gap, so it went to the backlog under _Platform and UI_ — with four of its
  five design questions already settled there.

**Honest note on closure.** Unlike v0.1, this milestone had no acceptance criteria written up
front — see the reasoning attached to
[requirements.md §5](requirements.md#5-acceptance-criteria), which is exactly the
value that was missing here. Everything above is verified by the test suite and, for the
layout, frame by frame in the simulator; the panel itself is the maintainer's confirmation.
v0.3 gets its criteria before its code.

### Pomodoro — done, 2026-07-27

A focus/break **cycle**, not a single countdown: focus runs down, alarms, and a press starts the
break; when the break alarms, a press starts the next focus. A long press returns to setting
from anywhere.

- [x] `gfx/font` gained a `scale` parameter: each font pixel becomes a block, so scale 2 gives
      10x14 glyphs. The clock will want the same.
- [x] `apps/pomodoro` — the project's first modal interface, and its first cycling one.
- [x] 17 tests, including the render layout, all green.

**The state machine is two dimensions, not eight phases.** `Mode` is Focus or Break, `State` is
Setting, Running, Paused or Alarm. Every screen and colour follows from the mode, so the
setting screens look like the running ones — you see what you are configuring. Eight named
phases would have needed eight render paths.

**Two durations, set in sequence.** Focus first, press moves to break, press starts. One
encoder, two values, no extra gesture.

**One layout, two skins.** Both modes place the icon, the word, the digits and the bar at exactly
the same coordinates; only the icon, the word and the colours differ. Focus: a tomato, leaves
included, that empties from the top as time runs down, `FOCUS` and the bar in `#FF4326`, digits in
white. Break: a coffee cup in `#2AE070` filling the tomato's box pixel for pixel, its steam drawn
at 65% brightness, digits in a lighter green. Two `static_assert`s tie the cup bitmap to the
tomato's bounding box, so resizing one and forgetting the other fails the build rather than the
panel.

Three details that cost more than they look. The digits are placed **individually**, with the
colon drawn as two blocks rather than taken from the font — beside the icon only 44 pixels remain,
and the font's own advance plus colon glyph need 46. The bar draws its **spent part dimmed**
rather than not at all, so the full span reads as a scale instead of a shrinking line. And the
colon **blinks with the second** in the accent colour, driven by the fractional part of the
remaining time rather than by a timer of its own: it appears in the same frame the digits change
and cannot drift against them. Where the digits stand still — setting, paused, alarm — it stays
lit, because a blink without a digit change is flicker, not a second hand.

**No status labels.** The ticking digits and the blinking colon are what distinguish setting from
running; blinking digits mark paused. Nothing else is on screen.

**It counts down from the frame delta, not from wall-clock time.** That keeps it free of C-9
(no real-time clock, so wall time needs the network) and makes it fully testable — a test
advances a synthetic 61 seconds and asserts the state.

**The alarm stops flashing after 30 seconds** and settles into a static screen. A device left
strobing in an empty room is no use to anyone, and an alarm that gives up entirely is no alarm.

### The clock moved to v0.4 — 2026-07-27

It was in this milestone for the wall-clock capability, not because anything needs it yet. Per
FR-26 the app may not read a clock itself, so it would have forced a time provider into the
platform — an interface with no consumer, which is the one thing this project is built to avoid.
The roadmap's own rule applies: building the app is how the capability gets built.

v0.4 is where it belongs, and not only as a parking space. Wall time depends on NTP, NTP depends
on WiFi, and WiFi provisioning is exactly what v0.4 delivers — before that a clock on a device
someone else switched on cannot know the time at all (C-9). It is also the natural default app
for a fresh unit, which is what v0.4's acceptance criterion already assumes.

Pull it forward at any time; the only cost is deciding the time provider earlier than necessary.

---

## v0.3 — Games and persistence ✅

**Complete, 2026-07-29.** All ten acceptance criteria met. Criterion 1 — Snake playable — confirmed
on the device by the maintainer; the rest are covered by the suite and by end-to-end runs against a
real state directory, listed under _How the criteria were checked_ below.

**Unlocks:** responsive input under load, and storing state.

- **Snake** — rotate to turn, press to start.
- Persistence for high scores and settings (FR-22, FR-23).
- **Settings** app: brightness and default app (FR-25). The **time zone** travels with the clock
  to v0.4 — a setting nothing reads is worse than a missing one, because it looks as though it
  works.

**One game, and it is Snake — 2026-07-27.** Two games would prove the same capability twice.
Snake is the cheaper of the two for three reasons, one of which is about our hardware rather
than about the games:

- **The encoder is incremental.** It reports _turned by one step_, never _is at position N_.
  Snake's mapping — rotate to turn relative to the current heading — is exactly that signal.
  Pong wants an absolute paddle position, so it would have to accumulate and clamp, and a fast
  rally would mean spinning a small knob quickly.
- **No opponent.** With one encoder the second paddle has to be an AI, and its difficulty is the
  whole game: perfect play is unbeatable, random misses feel cheap. That is tuning work with no
  test that can tell you when it is right.
- **Discrete and deterministic.** Snake advances a grid by one cell per tick, in integers, so a
  test can drive an exact number of ticks and assert the outcome. Pong needs sub-pixel ball
  positions and a bounce rule chosen to avoid degenerate flat rallies.

Pong moves to the backlog, where the game loop this milestone builds is waiting for it.

Deferring persistence to here is intentional: by this point three concrete consumers exist —
the high score, the settings, and FR-19's _restore the last active app_, which the shell
currently fakes with a hard-coded first app — so the store can be designed against real needs
instead of guesses (Q-7).

One thing must be right the first time: state goes into a **single writable location**,
separate from the binary (FR-39). v0.4 makes the root filesystem read-only, and retrofitting
that is far more work than getting the path right now. Writes are atomic from the start
(FR-40) — it is ten lines and it is what protects the store when the plug is pulled.

Done when the acceptance criteria in
[requirements.md §5.2](requirements.md#52-v03--games-and-persistence) are met. This time they
were written first, which is what v0.2's closing note promised.

### Decided before the code — 2026-07-28

Four questions the milestone would otherwise have answered by accident, halfway through.

**Q-7 is closed: one `key=value` file per namespace**, replaced atomically, in one writable root
— [ADR-0011](adr/0011-state-store-format.md). The deciding argument is not the format itself but
what a file boundary buys: a namespace becomes a failure domain, so Snake writing a high score
cannot damage the settings, and in v0.4 it cannot damage WiFi credentials or tokens. JSON would
have made every write a rewrite of the entire device state, for nesting that has nothing to nest
at this size.

**Snake gets a visible wall.** A one-pixel frame, 2x2 pixel cells, 31x15 cells of play area. The
alternative was the full 32x16 panel, which is a slightly larger field whose lethal edge is
invisible — and the panel's own rim is hard to judge in a dark room. A score bar across the top
was the other option and costs a quarter of the field; the score goes on the idle and game-over
screens instead, which are the two moments anyone reads it.

**Startup is a setting, not a precedence rule.** FR-19 (restore the last active app) and FR-25 (a
default app) collide at boot, and the tidy way out is to stop treating it as a collision: the
settings app offers `Start` = [Last app, Plasma, Pomodoro, Snake, …] and defaults to _Last app_.
Both requirements are met, neither has to lose, and v0.4 gets what it already assumes — the
ability to make the clock the factory default of a fresh unit.

The consequence worth naming: **the last app is remembered by name, not by index.** An index would
silently restore a different app the first time one is registered ahead of it. The price is that
two apps must not share a name. `Shell::add` warned about that for a while; the warning was
removed on 2026-07-29 because only a developer editing `main.cpp` can cause a clash, the warning
prevented nothing, and ADR-0008 makes the journal volatile — so it was unobservable on exactly the
device it was meant to protect. `startupApp()` still reports a stored app that no longer exists,
which is the case that actually occurs after a rename.

**Brightness belongs to the shell.** `Display` gains `setBrightness()` with the two
implementations it already has by design — the panel's own PWM, and colour scaling in the
simulator. The settings app never touches the HAL: it writes a number into the store, and the
shell applies changes it sees there. That gives a live preview while the knob turns, and makes
the boot path and the adjustment path the same one line of code. Scaling the finished `Surface`
before `present()` would have avoided touching the HAL at all, but it throws away the panel's
11-bit PWM resolution and shows as colour banding exactly where it hurts — at low brightness.

### Delivered — 2026-07-28

- **`os/state`** — `StateStore` over one writable root, `StateSection` per namespace, every save a
  temp file, `fsync`, `rename`, `fsync` of the directory. 18 tests against a real temporary
  directory, because faking the filesystem would have tested the fake: the atomicity lives in
  `rename`, not in our code.
- **Snake** — 31x15 cells, walls kill, the tail vacates its cell in the same step so following your
  own tail is legal, and the pace rises with the length from 5 to at most 14 cells per second.
  16 tests, including one that plays the game: a helper steers towards the food through the real
  turning path rather than reaching into the app.
- **Settings** — brightness and startup app, modal like the Pomodoro. 9 tests.
- **FR-19 in the shell**, plus the launcher highlight following the active app. 6 new shell tests.
- **`Display::setBrightness`** in both backends. 130 tests in the suite, host and aarch64 clean.

Three things that only became clear while building, each of which changed the code:

- **The store must be opened _after_ the display, not before.** The matrix library drops
  privileges to `daemon` while creating the panel, and every state write happens after that.
  Opening the store first would have checked whether *root* can write to the directory and then
  failed on every save — a device that looks healthy and forgets everything. `main.cpp` now says
  so at the call site, and [device-setup.md](device-setup.md) has the one provisioning line that
  makes the directory `daemon`-owned.
- **The high score is banked when it is beaten, not when the game ends.** The first version wrote
  it in the death handler, which reads naturally and quietly fails acceptance criterion 3: a power
  cut during a record game would lose the record. It now writes on the food that sets it.
- **A turn queue, not a pending direction.** Two detents between two grid steps are two quarter
  turns, one per step. Applying both at once would be an instant reversal into the snake's own
  neck — the bug that makes home-made Snake feel broken, and the reason criterion 2 is worded the
  way it is.

**And one that only playing it found — 2026-07-29.** The game-over screen reported a score one
lower than the high score it had just set. `score()` was derived from the length of the body, but
a fatal step pops the tail *before* the collision check — deliberately, so following your own tail
is legal — which leaves the deque one cell short of the truth at exactly the moment of death. Only
self-collision was affected; a wall kills earlier in the step. The score is now counted where it
is earned instead of inferred from a container that passes through an intermediate state. No test
would have found this from the rules alone, because the rules were never wrong; the regression test
now drives the snake into its own body and checks that the two numbers agree.

### How the criteria were checked — 2026-07-29

Criterion 1 is a judgement about feel — input latency, whether the speed curve is fun, whether 2x2
cells read at a glance — and was confirmed by playing on the device. The rest were verified
mechanically, several of them by driving the real binary rather than by unit test alone:

| #   | Verified by                                                                                                      |
| --- | ---------------------------------------------------------------------------------------------------------------- |
| 1 Playable on the panel | Played on the device                                                                         |
| 2 No turn is lost | Tests over an exact number of ticks, including the reversal-into-the-neck guard                     |
| 3 High score survives | Real run: `snake.conf` appeared **while the game was still running**, then survived `kill -9`   |
| 4 One writable root | Real run: only the three `.conf` files under the root, working directory untouched               |
| 5 Every write atomic | `write` → `fsync` → `rename` → `fsync` of the directory, plus two tests over leftover temp files |
| 6 Unusable root does not stop the device | Real run against a `chmod 500` directory: one warning, device runs, exit 0   |
| 7 Brightness live | Real run: brightest pixel 267 → 765 while turning; level restored after restart                     |
| 8 Startup follows the settings | Real run: a fixed `startup=Plasma` beat `last_app=Settings`                            |
| 9 Home consistent with the restore | Real run with `startup=Snake`: boots into Snake, `Home` highlights row 3           |
| 10 CI green, suite covers the contracts | Locally the same three checks CI runs, all clean; CI confirms on push        |

Criterion 9 was first checked with Plasma, which proves nothing: Plasma is index 0, so the
highlight would sit there with no restore at all. Repeated with Snake.

**Still open, and deliberately small:** the minimum brightness of 10 % in `os/settings.h` is a
guess about where the panel stops being useful. It costs one number to correct, once someone has
turned it all the way down on the hardware.

Anyone setting up a fresh device applies [device-setup.md §6](device-setup.md) first — without the
state directory the device runs fine and remembers nothing, which is by design but not what you
want to test.

---

## v0.4 — Appliance 🔨

**Built 2026-07-29, not yet verified on a device.** The code, the provisioning scripts and the
image procedure are in place; the acceptance criteria that need a radio, a phone and a power
cut are open. See _Delivered_ below.

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
- **Clock**, moved here from v0.2 — the default app for a fresh unit. Needs a time provider in the
  platform (FR-26, fake for tests per NFR-17), an explicit "time unknown" state until
  `systemd-timesyncd` reports a sync (C-9), the time zone setting deferred from v0.3, and a sync
  interval chosen against the panel flicker noted with the resolved Q-2.
- Read-only root via the overlay filesystem, separate writable state partition, atomic state
  writes.
- `journald` volatile with a 16 MB cap, swap disabled, `noatime`.
- Factory reset without a terminal — forget WiFi and tokens.

Explicitly **not** in scope: updates for shipped devices (see the addendum in
[ADR-0005](adr/0005-deployment-model.md)), and the OAuth flow — that arrives with the first
app that needs it, in v0.7.

**Done when** someone who has never seen the device can take it, a power supply, and their
phone, and reach a working clock on the panel — and when pulling the plug at any moment
leaves the device intact. The acceptance criteria are in
[requirements.md §5.3](requirements.md#53-v04--appliance), written before the code as v0.3
established.

### Decided before the code — 2026-07-29

Four ADRs, because four decisions had real alternatives and would otherwise have been made by
accident halfway through.

**[ADR-0012](adr/0012-own-http-server.md) — the HTTP server is ours**, about 600 lines on its
own thread with a `poll()` loop over several connections. Two facts decided it against
cpp-httplib, and both arrived during the discussion rather than before it: uploads left the
roadmap for an external storage service, which took the library's best argument (multipart)
with it; and the configuration page became a React app, which stresses exactly the part a
naive implementation gets wrong — six parallel connections for one page. Building that
properly is 60 lines. The record names the trigger for revisiting: the day the device has to
*receive* a file.

**[ADR-0013](adr/0013-wifi-provisioning-via-networkmanager.md) — `nmcli` as a child process.**
The image already runs NetworkManager, the four operations we need are one command each, and
the state machine above it is hardware-free and therefore testable. comitup and wifi-connect
lost on a structural point rather than a technical one: each brings its own web server and UI,
and our configuration page has to exist anyway for the version, the reset and v0.7's account
linking — so they add a second device UI instead of removing work.

**[ADR-0014](adr/0014-config-page-in-the-binary.md) — two pages, built differently.** The
setup portal is plain server-rendered HTML with no JavaScript at all, because it is rendered by
the captive-portal WebView, which is not a browser; the configuration page is React, because it
is opened in a real one and it is where the product is judged. Both are compiled into the
binary through a checked-in generated header — the arrangement `tools/bdf_to_header.py` has had
since v0.1, and the reason CI and the aarch64 build never see Node.

**[ADR-0015](adr/0015-time-provider-and-unknown-time.md) — the clock is allowed to say it does
not know.** `systemd-timesyncd` owns NTP; MatrixOS reads one file,
`/run/systemd/timesync/synchronized`, at most once every two seconds. Until that file exists
the panel shows `--:--`, because the alternative is a `fake-hwclock` value from three days ago
displayed with total confidence (C-9).

**Q-9 is closed: no QR code.** A WiFi-join payload needs a version-2 symbol, which with its
mandatory quiet zone is 33x33 pixels on a 32-pixel panel. Shrinking the quiet zone would fit
but trades away the one margin a camera needs, on an emissive low-resolution source. The panel
shows `JOIN WIFI` / `MatrixOS` / the four-character serial suffix in double-height type — the
suffix is the only part that differs between units, so it gets the size. The full arithmetic is
with Q-9 in [requirements.md](requirements.md).

**No authentication in v0.4, deliberately.** Anyone on the home network can open the
configuration page and trigger a factory reset. Nothing worth stealing is on the device yet —
credentials never leave it and the page never displays them — and the trigger for adding it is
named: v0.7, when OAuth tokens arrive. The honest limitation recorded with it is that without
TLS a password protects against a housemate, not against someone watching the LAN.

### Delivered — 2026-07-29

- **`net/http_server`** — HTTP/1.1 on its own thread, `poll()` over up to eight connections,
  route table, hard limits on headers, body, connection count and time. 19 tests that speak
  real HTTP over a real loopback socket, because faking the socket would test the fake.
- **`net/wifi` + `net/nmcli_wifi`** — scan, join, access point, forget, all through `nmcli`
  with an explicit argument vector and never a shell, so an SSID containing a quote is a string
  rather than a command. Terse output is split honouring nmcli's own escaping.
- **`os/provisioning`** — the state machine both threads read:
  `Waiting → AccessPoint → Connecting → Connected`, with `Failed` bringing the access point
  back. Requests return immediately and the work runs on a worker thread, because the browser
  is waiting at the other end of the POST.
- **`net/portal`** — the setup page, the captive-portal probes for Android, iOS, Windows,
  Firefox and Ubuntu, the JSON status, and the factory reset.
- **`web/`** — the React configuration page, 205 KB inlined into one file.
- **`os/clock` + `apps/clock`** — three faces, a colon that follows the second, and the
  unknown-time screen.
- **`apps/setup`** — the four panel states, with walking dots that distinguish a busy device
  from a stalled one.
- **`os/identity`** — hostname and access-point name from the CPU serial (FR-32), derived on
  every boot rather than stored, which is what makes it work on a read-only root.
- **Shell**: shows the setup app while the device needs the user and steps aside when it does
  not, and applies the time zone the way it already applied brightness.
- **`pi-deployment/provision.sh` and `prepare-card.sh`**, plus
  [image-build.md](image-build.md) for the golden image and its scrub.
- 213 tests, host and aarch64 builds clean, formatting enforced.

Four things that only became clear while building, each of which changed the code or the
scripts:

- **Port 80 has to be claimed before the panel exists.** The matrix library drops privileges to
  `daemon` while creating the panel, and a socket on port 80 can only be bound as root — but
  the state store must be opened *after* the drop, and the portal needs the store. `claimPort()`
  is now separate from `start()`, which is the same shape the encoder already had for its GPIO
  lines.
- **`nmcli` runs as `daemon` too, and polkit says no.** Without a rule granting that user the
  NetworkManager actions, every scan returns empty and every join fails — on a device whose
  panel, logs and web page all look healthy. This is written down in three places on purpose.
- **NetworkManager's stored connections do not survive a read-only root.** They live in `/etc`,
  which the overlay puts in RAM, so a device would forget its WiFi on every reboot while
  remembering its high score. `provision.sh` bind-mounts that directory onto the state
  partition.
- **The setup app must never be recorded as the last active app.** FR-19 would otherwise restore
  it after a reboot and put a perfectly connected device back on a setup screen. One `if`, and
  a test that would have failed the first time somebody rebooted a finished unit.

**And one that reading the code for the first real device found — 2026-07-29.** The boot job ran
its grace period only when `wifi.conf` already held a network name. On the first start of v0.4
that file does not exist yet, while NetworkManager has had the network configured for months — so
a long-configured device could declare itself unconfigured and open its access point, taking the
radio away from a connection that was seconds from succeeding (C-8). The other half of the cause
is in the unit file: ordering after `NetworkManager.service` means the *service* has started, not
that it has associated, and association takes five to twenty seconds.

**It is a race, not a certainty**, and the distinction is worth keeping straight: it fires only
when MatrixOS asks inside that window. On the development device it did not fire — that unit came
up on its network normally after provisioning, which is how the theory got tested and corrected
rather than assumed.

**And the one the hardware found on the first real setup attempt — 2026-07-29.** Every join
failed with `TRY AGAIN` on the panel, and the journal said why:

```
Error: 802-11-wireless-security.key-mgmt: property is missing.
```

Not a wrong password. The cause was a **pre-existing connection profile whose name differed from
the SSID**: the development device had a hand-made profile called `Standort MUC` for the network
`FRITZ!Box 7682 KV` (device-setup.md §7). `nmcli device wifi connect` finds a profile by SSID and
reuses it, and in that state it produced a connection with a password and no key-management
setting. Deleting the profile fixed it immediately and the setup flow completed on the next try.

Two things follow. First, **a shipped unit cannot hit this** — it has no hand-made profiles, and
the factory reset removes every client profile it ever creates. The development device was an
unrealistic test bed precisely because it had history. Second, the failure was
**indistinguishable from a wrong password** on the panel, which is the worst possible way for it
to fail: the user retypes a correct password and it fails again.

Two changes came out of it anyway, both defensive rather than causal, and it is worth keeping
that distinction: `NmcliWifi::connect()` now rescans until the network is visible before asking
to join it — an empty scan cache after leaving access-point mode would produce the same symptom —
and the failure path no longer deletes a profile that existed before the attempt. The second one
matters: on a hand-configured device that cleanup would have deleted somebody else's profile.

This is the class of defect the milestone's honesty note predicted. No fake reproduces it,
because a fake has no notion of a NetworkManager profile store with history in it.

The earlier fix is one line of intent: wait when *either* our own file or NetworkManager knows a network,
which is what `WifiControl::savedNetwork()` now answers. Three tests cover it, including the one
that matters most — a network that associates inside the grace period is never interrupted. What
makes it worth recording is that `--fake-wifi` could not have produced it: the fake modelled a
device as either unconfigured or already connected, never as *configured but not yet
associated*, which is the state every real device is in for the first seconds after a boot.

**Partly verified on hardware — 2026-08-20.** On the development Pi the setup flow works end to
end on a real radio and a real phone: the panel asks for help, the phone's captive-portal WebView
lands on the setup page without an address being typed, and the join goes through. That is
criteria 3 and 4. Snake and the Morse trainer run on the panel in the same build, so the encoder
path carries the apps written since v0.3 as well.

**What that run cannot answer is the other half of the milestone**, and the reason is the device
itself: the development Pi was configured by hand over weeks, not by `provision.sh` from a blank
card. It is therefore evidence about the *software* and none at all about the *unit*. Everything
that distinguishes a shipped device from a maintainer's Pi is still open — provisioning from a
blank card, two units not colliding, the read-only overlay, the scrubbed image, and pulling the
plug. Criteria 1, 2, 5, 6, 9, 10, 11 and 12 in
[requirements.md §5.3](requirements.md#53-v04--appliance) are the list to work through once a
card and a device are at hand.

The rest remains covered by the suite and by end-to-end runs of the real binary against
`--fake-wifi`, including the whole portal flow over HTTP.

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

## v0.6 — Content from outside

**Unlocks:** image decoding and upload handling on top of v0.4's HTTP server.

- **GIF & Text** — send an image or GIF plus a short caption to the device from a phone or
  browser; the device serves the upload page itself.
- Image/GIF decode and downscale to 64x32.
- Storage management (how many uploads, eviction policy).

**Swapped ahead of authenticated services — 2026-07-27.** By preference, and it costs nothing:
the two milestones share no dependency, and both sit on infrastructure that v0.4 and v0.5
already deliver. It even removes a forward reference — Spotify's cover art needs exactly the
downscaler this milestone builds, so the dependency now points backwards instead of forwards.

---

## v0.7 — Authenticated services

**Unlocks:** OAuth token lifecycle and secret handling.

- **Spotify** — now-playing with cover art, press to pause/resume, double-press to skip,
  rotate for volume.
- **Strava** — friends' statistics, rotate to change view.
- Token storage and refresh without re-deploying (FR-29), secrets hygiene (FR-24).

Spotify's album art reuses the 64x32 downscaler from v0.6.

The OAuth mechanics are described in [ADR-0007](adr/0007-appliance-provisioning.md): the
device's own configuration page starts the flow, a static HTTPS page redirects the
authorization code back to the device on the LAN, and pasting the code by hand is the
fallback that depends on nothing. Verify Q-10 first — if either service supports the device
authorization grant, the static page is unnecessary.

---

## Backlog — unscheduled

Wanted but not yet placed in a milestone. Ordering within this list is not meaningful.

### Apps that fit the hardware well

| App                                                                       | Interaction                                 | Needs                                                                                                                  |
| ------------------------------------------------------------------------- | ------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| GIF & Time                                                                | Press to change the animation               | Nothing beyond v0.2                                                                                                    |
| Plasma / Game of Life / Lissajous / fire / rain / DVD-bounce screensavers | Rotate to switch, press to change palette   | Nothing; these are Plasma variants                                                                                     |
| Pong                                                                      | Rotate to move the paddle                   | v0.3 game loop, plus an AI opponent and absolute paddle positioning from an incremental encoder — see the note in v0.3 |
| Breakout / Arkanoid                                                       | Rotate to move the paddle                   | v0.3 game loop; shares Pong's paddle problem                                                                           |
| Memory                                                                    | Rotate over cards, press to reveal          | v0.3 persistence for best times                                                                                        |
| 2048                                                                      | Rotate to pick a direction, press to commit | Input mapping design — four directions on one encoder                                                                  |
| Collatz visualiser, infinite terrain                                      | Rotate to change the seed                   | Nothing                                                                                                                |
| ASCII art                                                                 | Rotate to browse                            | Font work                                                                                                              |
| Day/night world map                                                       | Rotate to scrub time, press for now         | A 64x32 coastline mask; nothing else — v0.4 already knows the time                                                     |
| Live sport scores                                                         | Rotate through fixtures                     | v0.5 network, plus polling on a far shorter period than Weather                                                        |
| Flag quiz                                                                 | Rotate through answers, press to commit     | The quiz shell below, a curated flag set, v0.3 persistence                                                             |
| Who's that Pokémon                                                        | Rotate through answers, press to commit     | The quiz shell below, plus sprite data we almost certainly may not ship — see below                                    |
| Transit departures                                                        | Rotate through departures                   | v0.5 network and per-app configuration                                                                                 |
| Tennis Leistungsklasse                                                    | Rotate through recent matches               | v0.5 network, per-app configuration, and a data source that may not exist                                              |
| Stock, ETF and commodity ticker                                           | Rotate to change symbol, press for chart    | v0.5 network and per-app configuration                                                                                 |
| Instagram followers                                                       | Press to switch count and QR code           | v0.7 OAuth, per-app configuration, a QR generator — and it barely fits, see below                                      |

**Morse trainer — built 2026-07-30, ahead of every milestone.** It jumped the queue precisely
because it unlocks nothing: one encoder, one font, the v0.3 store, no network and no new
platform work, so nothing downstream waits on it.

The dot/dash split is not the app's invention — a tap arrives as `Press` on release, a hold
produces `LongPress` at 600 ms while the button is still down, so the gesture recognizer had
already drawn the line the operator has to learn.

**Four modes. Three are keyed by the same button and differ only in when the answer is visible
and what a miss costs** — which is why one flag rather than three sets of screens does the work
— **and the fourth runs the exercise backwards.**

- **STUDY** walks the alphabet with the code on screen throughout, loops a silent demonstration
  on the character itself, and advances only once the letter has actually been sent. A wrong
  symbol costs nothing but another go.
- **QUIZ** asks from memory like CODE, but a miss hands the answer over and leaves it up until
  the letter has been keyed — so a blank is never a dead end and the run never ends. No clock,
  no lives, no score. Its pool widens only on **unaided** recall: finishing a letter after it
  was given to you is practice, not progress.
- **GUESS** inverts the exercise: the code is shown and four letters are offered, the encoder
  moves the cursor and a press commits. Timed, three lives, its own record. Distractors are
  drawn from letters whose code is the **same length** wherever enough exist, because random
  ones would let most questions be settled by counting symbols instead of reading them.
- **CODE** keeps the answer hidden, runs a clock, and charges one of three lives per miss.

The first design had STUDY play letters back on a timer while the operator watched; it was
replaced the same day because watching is not practising. QUIZ was added straight after, as the
step between reading the answer and being timed on it, and GUESS after that. The menu labels
went through one revision: LEARN, READ and GAME became STUDY, GUESS and CODE, because with four
entries "GAME" no longer said which of the two games it meant.

**GUESS is also the exception to the input problem below**, and worth noticing as a pattern: it
never touches the key, so `Press` and `LongPress` are free, and the hold can simply mean "back
to the menu" without any pause menu at all. An app that spends both button gestures needs the
`Rotate` escape; an app that spends neither does not. Sending and reading also keep separate
records — `highscore` and `readscore` — because they are two skills and one number would flatter
whichever was practised last.

**That change exhausted the input vocabulary, which is the part worth recording.** Once both
modes key, `Press` and `LongPress` are both spoken for, `DoublePress` is not produced at the
default timing (Q-4), and the shell consumes `Home` before an app sees it (ADR-0009). `Rotate`
is the only input left, so it carries the way out: turning opens a pause menu — `RESUME` and
`MENU`, the same two everywhere — with `RESUME` preselected, so a knock against the encoder
costs one press rather than a run. An earlier draft offered `SKIP` as well; it was dropped once
it was clear that no mode can actually strand the operator. The pause also stops the game
clock, which is what makes it safe to reach for. **Any future app that keys, draws or otherwise
wants both button gestures will hit this same wall**, so the pattern is likely to outlive this
app; if a second one needs it, it wants an ADR rather than a copy.

**The threshold that makes the keyer possible also made half the table unkeyable.** A dash does
not exist until the button has been held for the full 600 ms, so `0` — five dashes — costs three
seconds of pure holding before any thinking, against a game limit that falls to two and a half.
Not hard: impossible. The game now hands that time straight back, refunding the threshold each
time a dash lands, so the clock measures recall rather than mechanics. The awkward half is that
an app cannot see a hold in progress — the recognizer says nothing until it fires — so a bare
`remaining <= 0` would still cut the operator off mid-dash; the timeout therefore allows one
dash-length of grace before it is believed. Both are covered by a test that keys `0` at the
floor limit with realistic hold timings, and that test fails on the previous build.

One piece of chrome worth keeping: an early draft put a row of repeat pips directly under the
code, and short bright bars beneath dots and dashes are the one thing a Morse trainer must
never draw. The top bar carries progress instead — through the alphabet in Learn, through the
pool in Quiz — and the code row doubles as the operator's own progress wherever the answer is
public: sent symbols white, the symbol due next in the accent, the rest waiting in the dark.
Where it is not public, the row shows only what has been sent, over a dim rail so an empty row
does not read as a broken one.

**Centring turned out to be a platform bug, not a Morse one, and it moved to `gfx/font`.**
`textWidth` reports the advance box — a full five-pixel cell per glyph — but almost no glyph
lights its last column. Centring on that number leaves text a pixel left of true centre, which
on a 64-wide panel shows up as **two more pixels of margin on the right than on the left**, in
every app that centred anything. `centredTextX` and `rightAlignedTextX` now measure the pixels
the glyphs actually set, skipping blank runs at either end, and Clock, Snake, Settings, Setup and
Morse all defer to them; where an ink width cannot be halved the spare pixel goes left.

A third round found the layer under that one: the source font **indents its narrow glyphs**.
`T`, `Y`, `I`, `1` and `0` light columns 1 to 3 of their five-column box rather than 0 to 3, and
the converter cannot compensate — `tools/bdf_to_header.py` requires a single bounding box for
every glyph and refuses per-glyph offsets, so the indent is baked into the bitmaps. Inside a
six-pixel advance it left three pixels of space before such a letter and two after it, which
reads as the letter leaning right inside its own word. `drawText` now trims each glyph's own
bearing, so every glyph starts at the left edge of its cell; the advance is untouched, so lines
stay monospaced and digits stop jittering sideways as they change. What cannot be removed is the
extra pixel itself — a three-wide glyph in a six-wide advance has to leave three pixels of gap
somewhere — but it is now consistently *after* the narrow glyph rather than before it.

The same function does a second job in GUESS, where each answer sits in its own ten-pixel cell
marked by an underline. `centredTextX` takes a width rather than a surface precisely so a cell
can be centred in like a small panel — needed because `T`, `I`, `1` and `0` light only three of
their glyph's five columns and a fixed inset pushed them against the right edge of their box. Two tests
in `test_font.cpp` hold the rule, and they earned their keep immediately by catching a sentinel
bug in the first refactor of the ink-bounds helper.

#### What the eight apps added on 2026-07-30 actually ask for

The last eight entries were requested together, and they turn out to need four things rather
than eight. That is the only reason they are worth a note instead of eight separate ones.

**Per-app configuration is the real gap.** Transit needs a stop, the ticker needs symbols,
Leistungsklasse needs a player, Instagram needs a handle. None of them can ask on the panel:
entering free text with one encoder is precisely the interaction FR-37 exists to prevent. The
configuration page from v0.4 is the obvious home, but today it serves a fixed set of fields the
platform itself owns and nothing lets an app contribute one. Half the new list is blocked on
this, which makes it the first thing to build of the four — and per NFR-16 it wants an ADR
first, because there are two defensible shapes: apps declare their settings and the page renders
them, or the page keeps its own table and apps read keys out of the v0.3 state store. The first
keeps the knowledge in the app, the second keeps `net/` from having to know what an app is.

**Flags and Pokémon are one app.** Show a picture, offer four answers, rotate to move, press to
commit, keep a streak in the state store. Only the dataset differs. Building either alone would
be building it wrong, so whichever is scheduled first carries the shell.

**Compiled-in datasets return, at a size the font never reached.** The world map is the cheapest
thing on the list: a 64x32 one-bit land mask is 256 bytes, the colour comes from code, and the
terminator follows from the date and time v0.4 already has. Flags need roughly 12 KB if the
curated set is stored two bits per pixel with a per-flag palette, which most national flags fit.
Pokémon silhouettes would be about 19 KB at 32x32 and one bit. All three are far below the 200 KB
the configuration page already occupies as a string literal (ADR-0014), so the established
pattern holds: a checked-in generator producing a header, as `tools/bdf_to_header.py` does for
the font. This contradicts a sentence under **Dropped**, which is corrected there.

**Live scores break the staleness model.** Weather sets the pattern in v0.5 — fetch rarely,
tolerate age, show it (FR-28). A score is the opposite: a stale score is not old, it is wrong,
and a device that silently shows 2-1 after the equaliser is worse than one showing nothing. That
is a different contract, not a shorter interval, and it needs its own answer for what the panel
shows when the feed is unreachable.

The world map is the only one of the eight that could be built today. It needs no network, no
configuration and no new platform work, which also means it unlocks no capability — so by this
roadmap's own ordering rule it belongs here rather than in a milestone, and it is the obvious
filler whenever a milestone finishes early.

#### Two of the eight have a blocker that is not technical

**Who's that Pokémon almost certainly cannot ship.** The silhouettes, names and designs are
Nintendo and Game Freak property, and the format is itself a recognisable element of the show.
Fine on a device that never leaves the flat; not fine on anything sold, given the direction
recorded in the commercialisation notes. The flag quiz is the same app without the problem —
national flags are not protected this way — so the shell gets built either way and Pokémon stays
here as a personal-build-only entry rather than moving to **Dropped**, because nothing about the
panel rules it out.

**The Instagram QR code only just fits, and only for short handles.** A version-1 QR is 21x21
modules and needs a four-module quiet zone, so 29x29 pixels — three to spare in the panel's
32-pixel height, with the follower count alongside it in the remaining 35 columns. The cost is
capacity: version 1 at error-correction level L holds 25 characters, and only in alphanumeric
mode, which is uppercase-only. `INSTAGRAM.COM/` spends 14 of those, leaving **11 characters for
the handle**. Longer handles need version 2 (25x25 plus quiet zone is 33 pixels, one too tall),
so the fallbacks are a shortener domain, trimming the quiet zone to two modules and accepting
that some scanners refuse, or scrolling. Worth settling before the app is written, because it
decides whether the QR code is a feature or a disappointment.

Both remaining data sources need checking before they are scheduled, for the same reason the
Spotify note in v0.7 does: the NHL feed and the amateur-tennis ratings (DTB / nuLiga) are read
through interfaces that are either undocumented or scraped, and sports data in particular is
aggressively licensed. Leistungsklasse has a second problem the others do not — an audience of
one. As a personal app that is the point; on a shipped device it is a menu entry nobody else can
use, which is an argument for per-app configuration being able to hide apps entirely.

### Platform and UI

#### Carousel launcher

The launcher becomes a rotating card rack instead of a list. The card of the app you just came
from sits in the centre, its neighbours peek in from the left and right, turning the encoder
spins the rack, and a press starts whatever is in the middle. The cards carry an icon and a
short looping animation of the app — **pre-recorded frames, not a live one** (decided
2026-07-27).

The current launcher already satisfies FR-15, so this is polish, not a gap — which is why it
sits here and not in a milestone. Five things it will run into, recorded now so they are not
discovered halfway through:

- **No names on the cards — decided 2026-07-27.** They were the whole width problem: at 5x7 with
  scale 1, `Pomodoro` alone is 47 of the 64 pixels, which left the neighbours nothing. Without
  them a card is icon plus preview and the layout falls out easily. The consequence to accept is
  that recognition now rests entirely on the picture — with a handful of apps that is fine, and if
  it ever is not, the centre card is where a name would go back.
- **The icons need an owner — still open.** `App` requires only `name()`; everything else has a
  default. Three candidates: an optional accessor on `App`, a table inside the launcher keyed by
  name, or the card handed in at registration time in `main.cpp`. Note that `Launcher` is _not_
  in the shell's `apps_` list, so whichever wins, the launcher never needs a card of itself.
  FR-15 does not decide this and all three are defensible — so per NFR-16 it needs an ADR before
  code.
- **The previews are recorded, which is what keeps them cheap.** A live preview would mean
  ticking a second app, and [ADR-0003](adr/0003-single-process-app-model.md) runs exactly one.
  Pre-recorded frames sidestep that entirely: a 32x24 card at a dozen frames is about 27 KB of
  RGB24 per app, nothing next to 512 MB of RAM. Two open ends, both small: where the frames come
  from — a `--record` flag on the app itself is the obvious source and keeps them honest — and
  that they belong in the binary through a checked-in generator, the way
  `tools/bdf_to_header.py` produces the font, rather than as pasted numbers.
- **The zoom on `Home` is the expensive half, and it splits cleanly.** Animating the cards
  themselves is free: the launcher gets `update(dt)` like any app and can slide or scale its own
  content over the first frames after `onEnter`, with the shell none the wiser. Zooming the
  _outgoing app's_ frame is the costly part — the launcher may not render another app, so the
  shell would have to grow a transition state, and the shell's one-frame deferred switch exists
  precisely so a request from inside the launcher's `onInput` cannot tear down a running app.
  **Decided 2026-07-27: build the launcher-owned animation, skip the cross-app zoom.** It is
  most of the perceived polish for none of the architectural cost, and it can be revisited
  without undoing anything.
- **The centre card lands on the last app — deliberately, since 2026-07-28.** It used to be an
  accident: `Launcher` has no `onEnter` override and lives for the whole run, so `selected_`
  simply persisted, and at boot it agreed with the running app only because both were index 0.
  FR-19 broke exactly that, as predicted here — a restored app is not index 0. The shell now calls
  `launcher_->select(index)` whenever an app becomes active, so the invariant holds by
  construction, including after a restart and after a crash-dropped app (FR-17).

## Dropped

Not unscheduled — excluded, per NG7 in [requirements.md](requirements.md). Recorded so the
question does not resurface.

| App           | Reason                                                                                                                                                                                                                                      |
| ------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Chess puzzles | A board plus piece glyphs on 64x32 leaves roughly 4x4 pixels per square, and stepping an encoder through 64 squares is tedious. Reshaping it enough to fit (cropped board, mate-in-one only) would leave something that is no longer chess. |
| Wordle        | Five columns of six rows plus a letter picker is not legible at this size, and selecting from 26 letters by rotation is slow.                                                                                                               |

The panel size is the reason, so this only changes if the hardware does. Both would also have
been the only apps needing datasets shipped alongside the binary — **no longer true as of
2026-07-30**, when the world map, the flag quiz and Pokémon arrived in the backlog wanting one
each. It changes nothing here: neither app was dropped over its dataset, and the three that want
one will use the compiled-in generated header the font already established rather than files
sitting next to the binary. The distinction that mattered — nothing to install, nothing to keep
in step with the executable — survives.
