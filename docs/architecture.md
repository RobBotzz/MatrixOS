# Architecture

Status: current as of 2026-07-29 (v0.4).

This describes the structure and the reasoning behind the boundaries. Code sketches are
illustrative — they show the shape of an interface, not its final signature.

---

## 1. Mental model

MatrixOS is a **shell around one active app**, with hardware behind a thin abstraction:

```
                  ┌──────────────────────────────────────────┐
                  │  apps/     Plasma, Pomodoro, Snake, ...  │
                  └────────────────────┬─────────────────────┘
                                       │ App interface
                  ┌────────────────────┴─────────────────────┐
                  │  os/       shell: tick loop, app registry, │
                  │            launcher, lifecycle, logging    │
                  └───────┬───────────────────────┬──────────┘
                          │                       │
          ┌───────────────┴──────┐   ┌────────────┴───────────────┐
          │  gfx/  Surface,      │   │  hal/  Display, Input      │
          │        font, drawing │   │        (interfaces)        │
          └──────────────────────┘   └────────────┬───────────────┘
                                                  │
                              ┌───────────────────┴───────────────────┐
                              │                                       │
                         hal/pi/ (Pi only)                     hal/sim/ (host)
                     rpi-rgb-led-matrix                 terminal output + keyboard
```

**Dependency rule:** arrows only point downward. `apps/` knows `gfx/` and the `App`
interface, nothing else. `os/` knows the HAL _interfaces_, never a concrete backend. Only
`main.cpp` — the composition root — knows which backend exists, and it is the only place
that changes when a backend is added.

This is the whole architecture. It is deliberately four directories, not a framework.

### 1.1 "OS" is a metaphor

The name is aspirational, not technical. There is no scheduler, no process model, no
isolation between apps. Treating the name literally is the main way this project could
drift into building infrastructure nobody needs — see NG1/NG2 in
[requirements.md](requirements.md).

---

## 2. The rendering path

Apps draw into a plain RGB buffer that MatrixOS owns; the backend decides what happens to
it.

```cpp
// gfx/surface.h — a plain, portable pixel buffer. No hardware, no library types.
class Surface {
public:
    Surface(int width, int height);

    int  width()  const;
    int  height() const;

    void clear(Color c = Color::black());
    void setPixel(int x, int y, Color c);       // silently ignores out-of-bounds
    Color pixel(int x, int y) const;

    std::span<const uint8_t> bytes() const;     // RGB24, row-major
};
```

```cpp
// hal/display.h
class Display {
public:
    virtual ~Display() = default;

    virtual int width()  const = 0;
    virtual int height() const = 0;

    // Present the finished frame. Returns when the frame is committed.
    virtual void present(const Surface& frame) = 0;

    virtual void clear() = 0;                   // used on shutdown (FR-4)

    virtual void setBrightness(int percent) {}  // from v0.3 (FR-6); apps never call it
};
```

Why a copy instead of drawing straight into the library's canvas:

- 64x32 is 2048 pixels — the copy is irrelevant next to the panel's own refresh work.
- `Surface` is plain memory, so a rendered frame can be compared byte-for-byte in a test
  (NFR-12). A hardware canvas cannot.
- Apps never see a library type, which is what FR-2 asks for.
- The backend cannot run on x86 — it needs `/dev/mem`, real GPIO and root — so the host
  build simply never includes it. (It does compile for x86; running is the obstacle. See the
  addendum in [ADR-0002](adr/0002-display-abstraction-and-simulator.md).)

The matrix backend maps `present()` onto `FrameCanvas` + `SwapOnVSync()`, which gives the
atomic frame swap required by FR-3. The simulator backend redraws the terminal.

### 2.1 Text

v0.1 needs text only for the launcher. That is one embedded bitmap font (4x6 or 5x7) in
`gfx/`, not a font engine. The library ships BDF fonts in `external/rpi-rgb-led-matrix/fonts/`
and they could be parsed later, but a BDF parser is not justified by one menu — and pulling
the library's font code into `gfx/` would break the portability of the host build.

---

## 3. The input path

The HAL turns hardware into events; gesture recognition is pure logic and therefore
testable.

```cpp
// hal/input.h
enum class InputType { Rotate, Press, DoublePress, LongPress, Home };

struct InputEvent {
    InputType type;
    int       delta = 0;    // Rotate only: signed detents, usually ±1
};

class Input {
public:
    virtual ~Input() = default;
    // Non-blocking: returns everything that happened since the last call.
    virtual std::vector<InputEvent> poll() = 0;
};
```

The gesture recognizer (short vs. double vs. long press) is a separate, hardware-free
component that takes **timestamps as arguments** rather than reading a clock itself. That
single decision is what makes press timing unit-testable — feed it a synthetic sequence and
assert the events.

`LongPress` fires the moment the 600 ms threshold is crossed, while the button is still down,
and suppresses the `Press` that the following release would otherwise produce (FR-10). There
is no cancellation logic and no gesture reserved for the shell, because the shell has its own
button ([ADR-0009](adr/0009-dedicated-home-button.md)).

`Home` comes from the dedicated home button and is the one event the shell consumes itself —
it is never forwarded to an app (FR-16). The encoder's own button belongs entirely to the
active app.

Below the event stream sit two hardware-free components and one thin layer that is not:

| | |
| --- | --- |
| `hal/quadrature` | the two phase-shifted encoder signals to detents. Bounce cancels itself out because a line flickering back and forth adds and subtracts from the same accumulator. |
| `hal/gestures` | button transitions plus timestamps to `Press` and `LongPress`. |
| `hal/pi/encoder_input` | the GPIO character device. Not testable without hardware, which is why so little lives here. |

Q-1 settled the pins — encoder on GPIO 5/6/13, home button on 19 — and Q-4 settled the reading
strategy: **kernel edge events, not polled levels**. The kernel timestamps every transition and
buffers them, so reading the descriptor once per frame cannot lose a detent however fast the
knob is turned, and no extra thread is needed. Polling levels at 60 Hz would miss transitions
and break FR-9.

`DoublePress` exists in the vocabulary but the recognizer does not produce it by default.
Confirming a single press means waiting out the window for a second one, which would delay
every press by ~300 ms — a cost no current app justifies. Enabling it is one field in
`GestureRecognizer::Timing`, and the behaviour is already tested both ways.

---

## 4. The app model

```cpp
// os/app.h
class App {
public:
    virtual ~App() = default;

    virtual std::string_view name() const = 0;

    virtual void onEnter() {}                        // becoming active
    virtual void onExit()  {}                        // leaving; release resources
    virtual void onInput(const InputEvent&) {}
    virtual void update(std::chrono::duration<float> dt) {}
    virtual void render(Surface&) = 0;
};
```

Five methods, all but one optional. An app is a normal C++ object owned by the shell:
constructed at registration, entered and exited as the user navigates, ticked only while
active (FR-14).

`update(dt)` receives measured elapsed time so behaviour does not depend on the achieved
frame rate (NFR-2). A fixed-timestep accumulator is _not_ introduced now; it becomes worth
it only if a game needs deterministic physics, and it can be added inside that app.

### 4.1 Apps do no I/O

An app has no file access, no sockets, no clock reading of its own. Anything from outside
arrives through an object passed in at construction. The rule exists from day one because it is
what makes the network-runtime decision ([ADR-0004](adr/0004-network-app-runtime.md))
postponable without cost, and because it keeps apps testable.

This is a convention, not a layer. There is no `DataProvider` base class until there is a
second provider to justify it (NFR-17).

From v0.3 two apps have such an object: Snake and the settings app hold a `StateSection` from the
store (§9.3). From v0.4 there are two more of the same shape: the clock holds a `TimeProvider`
(it may not read a clock either — [ADR-0015](adr/0015-time-provider-and-unknown-time.md)) and
the setup app holds a `const Provisioning&` it only ever reads a snapshot from. That is the rule working rather than an exception to it — neither app opens a file,
and a test hands them an in-memory store. One thing follows that the app authors must know:
**saving blocks.** An `fsync` on an SD card can cost more than a frame, so a save belongs on a
rare, user-visible event — a record beaten, an app switched, the settings left — and never on the
per-frame path.

---

## 5. The shell loop

```
setup:   claim port 80                                        # while still root: §9.1
         build display + input backends (composition root)
         open the state store                                 # after the display: §9.3
         start the web server, begin provisioning             # §9.1, §9.4
         register apps
         activate the startup app                             # FR-19/FR-25, not the launcher

each frame:
         dt = now - last
         for event in input.poll():
             if event is Home: toggle app <-> launcher         # FR-16, shell-only
             else: active.onInput(event)
         show or leave the setup app if provisioning changed   # FR-35, §9.2
         active.update(dt)
         active.render(backBuffer)
         apply brightness if the setting changed               # FR-6
         apply the time zone if the setting changed            # FR-25
         display.present(backBuffer)
         sleep until the frame budget is used up               # NFR-1

on signal:
         store.saveAll(); display.clear(); exit                # FR-4
```

The app tick and render are wrapped so an exception unloads the app, logs it, and drops
back to the launcher instead of killing the process (FR-17). `systemd` restarting the
service is the backstop, not the primary strategy.

The launcher is itself an `App`. It gets no special case in the loop — it is just the app
that is active when no other one is, which means the list-scrolling behaviour is testable
like any other app.

---

## 6. Directory layout

```
src/
  main.cpp                composition root: pick backends, register apps, run
  os/
    app.h                 the App interface
    shell.{h,cpp}         loop, lifecycle, exception boundary
    launcher.{h,cpp}      app-selection menu (an App itself)
    log.h                 leveled logging to stdout/stderr
    state.{h,cpp}         atomic state store        (v0.3, see §9.3)
    settings.h            the keys the shell and the settings app share
    clock.h, clock.cpp    time provider + "time unknown"  (v0.4, ADR-0015)
    identity.{h,cpp}      this unit's name, from the CPU serial (v0.4, FR-32)
    provisioning.{h,cpp}  the setup state machine   (v0.4, see §9.4)
  gfx/
    surface.{h,cpp}
    color.h
    font.{h,cpp}          embedded bitmap font + text drawing
    draw.{h,cpp}          lines, rects, blit
  hal/
    display.h             interface
    input.h               interface + event types
    gestures.{h,cpp}      hardware-free button timing
    quadrature.{h,cpp}    hardware-free encoder decoding
    pi/                   LED panel + encoder        (aarch64 build only)
    sim/                  terminal + keyboard        (host build only)
  net/                    platform infrastructure   (from v0.4, see §9)
    http_server.{h,cpp}   HTTP/1.1 on its own thread          (ADR-0012)
    portal.{h,cpp}        setup page, config API, captive-portal probes
    web_assets.h          the React page, generated and checked in (ADR-0014)
    wifi.h                the WifiControl interface + the no-radio implementation
    nmcli_wifi.{h,cpp}    NetworkManager through nmcli        (ADR-0013)
    fake_wifi.h           a radio made of variables: tests, and --fake-wifi
  apps/
    plasma/               v0.1 animation app
    pomodoro/             v0.2 focus/break cycle
    snake/                v0.3 game
    settings/             v0.3 brightness, startup app, time zone
    testpattern/          panel diagnostics
    clock/                v0.4 wall clock
    setup/                v0.4 provisioning UI
tests/
  ...                     host-only, Catch2
web/                      the React configuration page  (v0.4, ADR-0014)
tools/                    generators whose output is checked in
```

`external/` stays as it is. There is no `mdns` module: `avahi-daemon` is active in the image by
default and answers for the system hostname, so the whole of FR-36 is a hostname derived from
the CPU serial. Writing one would have been an abstraction with nothing behind it (NFR-17).

### 6.1 Build targets

| Target                | Contents                                              | Built where                           |
| --------------------- | ----------------------------------------------------- | ------------------------------------- |
| `matrixos_core`       | `os/`, `gfx/`, `apps/`, HAL interfaces, gesture logic, state store | host + Pi                 |
| `matrixos_hal_sim`    | terminal display, keyboard input                      | host (and Pi, for debugging over SSH) |
| `matrixos_hal_pi`     | LED panel and encoder backends                        | Pi only                               |
| `MatrixOS`            | `main.cpp` + the backends available for this target   | both                                  |
| `matrixos_tests`      | `tests/` against `matrixos_core`                      | host only                             |
| `matrixos_net`        | HTTP server, portal, WiFi control (from v0.4)         | host + Pi                             |
| `matrixos_warnings`   | INTERFACE target carrying `-Wall -Wextra -Wpedantic`  | both, our sources only                |

Splitting the backend out of the main library keeps the host build free of a GPLv2 library it
cannot run anyway, and keeps `matrixos_core` — the part under test — independent of it.

---

## 7. Testing strategy

| Level    | What                                                                              | How                                                                                                                              |
| -------- | --------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Unit     | Gesture recognition, launcher navigation, app state machines (timers, game rules) | Catch2, host only. Pure logic, no display, no clock.                                                                             |
| Snapshot | Rendering output                                                                  | Render a known tick into a`Surface`, compare bytes against a stored golden frame. Catches accidental visual regressions cheaply. |
| Manual   | Flicker, timing, encoder feel, brightness                                         | On hardware. Cannot be automated and should not be faked.                                                                        |

Catch2 v3 pulled in via CMake `FetchContent`, host-only, so the Pi cross build stays lean.

What is _not_ tested: the matrix backend itself. It is a thin adapter over a third-party
library and can only be verified by looking at the panel.

---

## 8. Relationship to the original module sketch

The first README proposed **View / HAL / OS / Apps**. The mapping:

| Original                        | Now         | Note                                                                                                                                                     |
| ------------------------------- | ----------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| HAL                             | `hal/`      | Unchanged in spirit, sharpened into two interfaces with two implementations each.                                                                        |
| OS                              | `os/`       | Unchanged.                                                                                                                                               |
| Apps                            | `apps/`     | Unchanged.                                                                                                                                               |
| View — "build views/layouts"    | `gfx/`      | **Reduced.** Drawing primitives and text only. A view/layout system is NG3: it arrives when three apps have duplicated the same layout code, not before. |
| View — "converting images/gifs" | not in v0.1 | Belongs to the app that needs it (the upload app). Image decoding is a dependency question, not a platform one.                                          |

---

## 9. The appliance layer (from v0.4)

The device is meant to be handed to someone who did not build it
([ADR-0007](adr/0007-appliance-provisioning.md)). Four things follow for the structure, and
notably none of them changes the app model — the setup screen is an app, and the clock is an
app holding a provider, exactly like Snake holds its store section.

### 9.1 The HTTP server is platform infrastructure, not an app

It carries the WiFi setup portal, the configuration page and, from v0.7, the OAuth callback.
Platform concerns, all of them — so it lives in `net/` beside `hal/`, owned by `main.cpp`, and
not inside an app.

This is a correction of the original plan, where the server was a detail of the upload app in
the final milestone. It is the one structural consequence of aiming at an appliance.

**It is also the project's second thread**, and the only one our own code owns
([ADR-0012](adr/0012-own-http-server.md)). The reason is FR-27: `accept()` blocks by nature and
a frame has 16.6 ms. The rule that keeps this safe is narrow enough to state in one line —
**the two threads meet at exactly one object, `Provisioning`, which is mutex-protected**
(§9.4). Handlers touch nothing else the render loop touches, and the render loop never touches
a socket.

Two things follow that are easy to get wrong and are written down where they happen:

- **The port is claimed before the panel exists.** Port 80 needs root, and the matrix library
  drops privileges to `daemon` while creating the panel — so `claimPort()` is separate from
  `start()`, exactly as the encoder claims its GPIO lines first.
- **Assets are served from memory, never from disk.** The root filesystem is read-only, and a
  server with no filesystem behind it has no path-traversal bugs to have.

### 9.1.1 Two pages, built differently

The setup portal is plain server-rendered HTML with no JavaScript; the configuration page is a
React app inlined into a single `index.html` and compiled into the binary through a checked-in
generated header. The reason is not taste: the portal is rendered by the captive-portal
WebView, which is not a browser, and a page that fails there leaves a device nobody can put
into service. [ADR-0014](adr/0014-config-page-in-the-binary.md).

### 9.2 Setup is an app

While the device has something for the user to do, the shell activates the setup app. It
renders the states the user needs to see — setup mode, connecting, connected, failed — using
the same `Surface` and the same lifecycle as any other app. No mode switch in the loop, no
second rendering path, and it is snapshot-testable like everything else.

The advantage this buys is worth naming: comparable headless devices do WiFi onboarding blind
behind a blinking LED. Having a panel means every failure can be shown where the user is
already looking.

The shell's rule is one sentence and it acts **only on the transition**: when the device starts
needing setup, show the app; when it stops, leave the app if it is still the one on screen.
Anyone who navigated elsewhere in between meant it. And the setup app is never recorded as the
last active app — FR-19 would otherwise restore it after a reboot and put a connected device
back on a setup screen.

### 9.3 One state store, written atomically — built in v0.3

All persistent state — WiFi credentials, tokens, settings, scores — goes through
`os/state.{h,cpp}` into a single writable location, separate from the read-only system and
from the binary (FR-39). Every write is temp file, `fsync`, `rename`, `fsync` of the directory
(FR-40). The format is one `key=value` file per namespace,
[ADR-0011](adr/0011-state-store-format.md).

It arrived a milestone before the appliance it was written for, because v0.3 produced three real
consumers — the shell's last active app, the settings, and Snake's high score. That was the plan:
the store is designed against needs rather than guesses.

Two consequences of the real thing that the sketch did not anticipate:

- **The store is opened after the display, not before.** The matrix library drops privileges to
  `daemon` while creating the panel, and every state write happens after that point. Checking
  writability as root and then writing as `daemon` would produce a device that looks healthy and
  forgets everything.
- **An unusable state root degrades instead of failing.** The store keeps everything in memory,
  logs the reason once, and the device runs. A provisioning mistake must not brick a unit that
  the maintainer cannot easily reach (C-10).

Two reasons this is a platform component rather than a convention:

- **The root filesystem becomes read-only in v0.4** ([ADR-0008](adr/0008-power-loss-resilience.md)).
  If state writes are scattered, that switch turns into archaeology. One store means one
  path to point somewhere else.
- **Pulling the plug is a supported way to switch off.** Atomicity has to hold for every
  writer, which is only checkable if there is exactly one.

Tokens live here too, and the platform — not the app — acquires and refreshes them. That is
FR-26 already in force: an app receives data, never credentials, and never performs I/O.
The appliance requirements needed no new rule for this, which is the payoff of having written
that one down early.

### 9.4 One provisioning object, read by both threads

`os/provisioning` is the state machine — `Waiting → AccessPoint → Connecting → Connected`, with
`Failed` bringing the access point back — and the only thing that talks to the radio. Three
properties make it work:

- **Requests return immediately.** A browser is waiting at the other end of the POST, and a
  join takes tens of seconds. The work runs on a worker thread the object owns.
- **The panel reads a snapshot**, taken under the lock once per frame. The app never sees a
  half-updated state and never blocks.
- **`waitForIdle()` joins the worker**, which is how shutdown is clean and how the tests are
  deterministic without a single sleep — the same property that made the gesture recognizer
  testable by taking timestamps as arguments.

`WifiControl` is an interface with two real implementations, like the display and input HALs:
`NmcliWifi` on the device and `NoWifi` on a machine with no radio we are allowed to touch
([ADR-0013](adr/0013-wifi-provisioning-via-networkmanager.md)). `FakeWifi` is a third, and it
earns its place in `net/` rather than in the tests by having two users: the suite, and
`--fake-wifi`, which puts the entire setup flow into the terminal simulator.
