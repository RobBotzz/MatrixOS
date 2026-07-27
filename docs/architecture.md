# Architecture

Status: draft, 2026-07-25.

This describes the intended structure for v0.1 and the reasoning behind the boundaries.
Code sketches are illustrative — they show the shape of an interface, not its final
signature.

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
                     hal/matrix/ (Pi only)                     hal/sim/ (host)
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

Which pins are used and whether they are polled or watched via libgpiod is confined entirely
to `hal/`. Q-1 answered the pin question — encoder on GPIO 5/6/13, home button on 19 — and
Q-4 (polling vs. libgpiod edge events) is still open, but answering it later changes nothing
above this line.

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
arrives through an object passed in at construction. Today no app needs this; the rule
exists from day one because it is what makes the network-runtime decision
([ADR-0004](adr/0004-network-app-runtime.md)) postponable without cost, and because it
keeps apps testable.

This is a convention, not a layer. There is no `DataProvider` base class until there is a
second provider to justify it (NFR-17).

---

## 5. The shell loop

```
setup:   build display + input backends (composition root)
         register apps
         activate launcher

each frame:
         dt = now - last
         for event in input.poll():
             if event is Home: toggle app <-> launcher         # FR-16, shell-only
             else: active.onInput(event)
         active.update(dt)
         active.render(backBuffer)
         display.present(backBuffer)
         sleep until the frame budget is used up               # NFR-1

on signal:
         display.clear(); exit                                # FR-4
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
    registry.{h,cpp}      the list of available apps
    launcher.{h,cpp}      app-selection menu (an App itself)
    log.h                 leveled logging to stdout/stderr
    state.{h,cpp}         atomic state store        (from v0.3, see §9)
  gfx/
    surface.{h,cpp}
    color.h
    font.{h,cpp}          embedded bitmap font + text drawing
    draw.{h,cpp}          lines, rects, blit
  hal/
    display.h             interface
    input.h               interface + event types
    gestures.{h,cpp}      hardware-free press/rotation recognition
    matrix/               LED panel backend         (aarch64 build only)
    sim/                  terminal + keyboard backend (host build only)
  net/                    platform infrastructure   (from v0.4, see §9)
    http_server.{h,cpp}   setup portal, config page, OAuth callback, later uploads
    mdns.{h,cpp}          stable name on the local network
  apps/
    plasma/               v0.1 animation app
    setup/                provisioning UI           (from v0.4)
tests/
  ...                     host-only, Catch2
```

`main.cpp` moves from the repository root into `src/`. `external/` stays as it is.

### 6.1 Build targets

| Target                | Contents                                              | Built where                           |
| --------------------- | ----------------------------------------------------- | ------------------------------------- |
| `matrixos_core`       | `os/`, `gfx/`, `apps/`, HAL interfaces, gesture logic | host + Pi                             |
| `matrixos_hal_sim`    | terminal display, keyboard input                      | host (and Pi, for debugging over SSH) |
| `matrixos_hal_matrix` | rpi-rgb-led-matrix backend                            | Pi only                               |
| `MatrixOS`            | `main.cpp` + the backends available for this target   | both                                  |
| `matrixos_tests`      | `tests/` against `matrixos_core`                      | host only                             |
| `matrixos_net`        | embedded HTTP server, mDNS (from v0.4)                | host + Pi                             |

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
([ADR-0007](adr/0007-appliance-provisioning.md)). Three things follow for the structure, and
notably none of them changes the app model.

### 9.1 The HTTP server is platform infrastructure, not an app

It carries the WiFi setup portal, the configuration page, the OAuth callback, and eventually
uploads. Four consumers, three of which are platform concerns — so it lives in `net/` beside
`hal/`, owned by the shell, and not inside the app that happens to need it last.

This is a correction of the original plan, where the server was a detail of the upload app in
the final milestone. It is the one structural consequence of aiming at an appliance.

### 9.2 Setup is an app

While the device is unconfigured, the shell activates the setup app. It renders the states
the user needs to see — setup mode, connecting, connected, failed — using the same `Surface`
and the same lifecycle as any other app. No mode switch in the loop, no second rendering
path, and it is snapshot-testable like everything else.

The advantage this buys is worth naming: comparable headless devices do WiFi onboarding blind
behind a blinking LED. Having a panel means every failure can be shown where the user is
already looking.

### 9.3 One state store, written atomically

All persistent state — WiFi credentials, tokens, settings, scores — goes through
`os/state.{h,cpp}` into a single writable location, separate from the read-only system and
from the binary (FR-39). Every write is temp file, `fsync`, `rename` (FR-40).

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
