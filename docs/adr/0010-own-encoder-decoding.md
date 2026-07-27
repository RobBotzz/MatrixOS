# ADR-0010: Decode the encoder ourselves rather than using the kernel driver

- **Status:** Accepted
- **Date:** 2026-07-27

## Context

Quadrature decoding and button-press timing are solved problems, so the question of whether to
use something existing deserved an answer rather than an assumption.

What is actually available:

- **Arduino libraries** (`OneButton`, `AceButton`, `Bounce2`, `RotaryEncoder`) do precisely this
  mapping, and they are what most people mean when they remember such a library. They call
  `digitalRead()` and `millis()` and assume the Arduino runtime, so on Linux they would need a
  compatibility shim larger than the logic they replace.
- **libgpiod** and **libevdev** provide GPIO and input-device access, but no gesture logic at
  all. On Linux that layer is conventionally the application's.
- **The kernel's own `rotary-encoder` driver**, configured through a device tree overlay, does
  the quadrature decoding, with `gpio-keys` handling debounced buttons. Userspace then reads
  `/dev/input/eventN`. This is the only option that genuinely removes work.
- **pigpio** is ruled out separately: rpi-rgb-led-matrix's README names it explicitly as
  something that must not run in parallel, because its sampling thread interferes with the
  panel's timing (C-3).

## Decision

Keep the decoding in `hal/quadrature` and the press timing in `hal/gestures` — roughly 150
lines with 21 tests and no dependencies.

## Consequences

- The build stays free of another cross-compiled dependency, and the pins stay configured in
  code rather than in the device tree.
- Encoder behaviour is unit-testable. A test can feed the exact transition sequence of ten
  detents and assert the result, which no evdev-based approach allows.
- We own the correctness of the state machine. That is a real liability, mitigated only by the
  tests.

## Why the kernel driver's main advantage does not apply

Its strongest argument is that it decodes in an interrupt handler and therefore cannot miss a
transition even when userspace is starved. **We already have that property**, because Q-4 chose
kernel edge events over polled levels: the kernel captures and timestamps every transition and
buffers it, so our reading rate is irrelevant.

Had we polled pin levels, this decision would have gone the other way.

What remains is where the state machine runs — and once events are buffered and timestamped,
that is no longer timing-critical. Sixty tested lines against a device tree dependency is not a
close call.

## Trigger for revisiting

Hardware behaviour we cannot fix in our own decoder: detents lost under load, direction
flipping, or bounce that the accumulator does not absorb.

If that happens, the kernel driver is the better place to solve it, and the switch touches only
`hal/pi/encoder_input.cpp`. The `Input` interface, the gesture recognizer, the shell and every
app stay as they are — the same containment that made the display backend swappable.

## Alternatives considered

- **Kernel `rotary-encoder` + `gpio-keys` overlays** — deferred, see the trigger. It would also
  leave the gesture timing with us, so it replaces the tested half and keeps the untestable
  half.
- **Arduino-style library** — rejected: needs a runtime shim, and would be more work than the
  code it replaces.
- **pigpio** — rejected: conflicts with the panel's timing by design.
