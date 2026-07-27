# ADR-0002: Display abstraction with a simulator backend from day one

- **Status:** Accepted
- **Date:** 2026-07-25

## Context

Development happens on x86 Linux (WSL2). rpi-rgb-led-matrix drives the panel by mapping
`/dev/mem` and manipulating GPIO registers directly; it needs real Pi hardware and root.
Nothing renders on the development machine.

Without a non-hardware path, the only way to see any change is: commit, wait for CI,
pull the artifact onto the Pi, run it. That is minutes per iteration and it makes automated
tests of rendering impossible.

This project otherwise avoids early abstraction (NFR-17: no interface without a second
implementation). Here the second implementation exists from the first day and is needed
daily — which is exactly the condition that justifies the interface.

## Decision

Introduce a `Display` interface in `hal/` with two implementations:

- **matrix backend** — rpi-rgb-led-matrix, built only for aarch64.
- **simulator backend** — renders to the terminal using ANSI colour, built on the host.

Apps render into a `gfx::Surface` (a plain RGB24 buffer owned by MatrixOS) and never touch a
library type. The backend consumes a finished `Surface` via `present()`.

The same reasoning applies to input, so `Input` gets an encoder backend and a keyboard
backend (FR-11).

## Consequences

- The full application builds and runs on the development machine without root (NFR-10),
  giving a sub-30-second edit-to-result loop (NFR-11).
- A rendered frame is plain memory, so golden-frame snapshot tests become possible
  (NFR-12).
- The host build does not link the GPL-licensed, Pi-specific library at all, so it does not
  matter whether that library compiles for x86. (It does — see the addendum.)
- Cost: one buffer copy per frame in the matrix backend (2048 pixels — negligible next to
  the panel's refresh work) plus one extra indirection.
- The simulator is a development tool, not a product. Colour fidelity, brightness, and
  perceived flicker cannot be judged in a terminal; those remain hardware-only checks.
- `main.cpp` is the only place that knows which backends exist.

## Alternatives considered

- **Develop only on the Pi** — rejected: every iteration pays build-and-deploy latency, and
  rendering stays untestable in CI.
- **Draw directly into `rgb_matrix::FrameCanvas`** — rejected: couples every app to the
  library, blocks host builds, and makes frame snapshots impossible. The saved copy is not
  worth any of that.
- **Make `Surface` inherit from `rgb_matrix::Canvas`** so the library's BDF font routines
  can draw into it — rejected: it drags the Pi-specific library into the portable core,
  which is the one thing this ADR is trying to prevent. An embedded bitmap font in `gfx/`
  covers v0.1's needs.
- **SDL2 window instead of a terminal** — deferred, not rejected: nicer for judging
  animations, but it adds a dependency to the host build. The `Display` interface makes it a
  drop-in addition whenever the terminal becomes limiting.
- **Add the simulator later, once it hurts** — rejected: by then rendering code has grown
  around the library's types and the abstraction becomes a refactor instead of a starting
  point.

## Addendum, 2026-07-26 — the x86 compile question is settled

This record was written without knowing whether rpi-rgb-led-matrix compiles for x86 at all.
It does: all 13 translation units listed in `CMakeLists.txt` compile cleanly with
`g++ 15 -std=c++20` on x86-64 Linux, `gpio.cc` included.

The decision is unaffected, but one of its arguments is now moot and should not be repeated
as if it still applied. What still justifies the abstraction:

- The backend cannot **run** on x86 — it needs `/dev/mem`, real GPIO, and root. Compiling was
  never the obstacle; executing is.
- A rendered frame stays plain memory, which is what makes snapshot tests possible.
- Apps do not depend on a third-party library's types.
- The host build has no reason to link a GPLv2 library it cannot use.
