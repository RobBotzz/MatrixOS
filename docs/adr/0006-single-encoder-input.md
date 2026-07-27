# ADR-0006: A single rotary encoder is the only control

- **Status:** Superseded by [ADR-0009](0009-dedicated-home-button.md)
- **Date:** 2026-07-25

> The escape hatch named below was taken on 2026-07-26, before any input code was written.
> The record is kept unedited: the reasoning for trying a single control first, and the
> conditions under which it would be abandoned, are exactly what made the change cheap.

## Context

The device is operated by one rotary encoder with an integrated push button. That is a
deliberate aesthetic choice — one knob, nothing else — but it creates a conflict that shows
up immediately in the requirements.

The shell needs a way to leave an app and return to the launcher. Several apps in the
backlog also want a hold gesture of their own: Pomodoro holds to configure the timer,
Weather holds to change location. Both want "hold the button", and there is only one button.

The resolution in FR-10 is a two-tier hold: a release between 600 ms and 2 s is delivered to
the app as `LongPress`; crossing 2 s emits `VeryLongPress`, which the shell consumes and
never forwards, and the pending `LongPress` is cancelled.

That works, but it costs something real:

- An app cannot show feedback *while* the button is held, because `LongPress` only arrives on
  release. A "setting time" mode therefore cannot highlight itself during the hold.
- Leaving an app takes a two-second wait, every time.
- It is the most timing-sensitive and least obvious part of the input layer.

The alternative is a dedicated home button: one more GPIO, a bit more wiring, one more hole
in the enclosure — and the conflict disappears entirely.

## Decision

Ship the single encoder, with the two-tier hold exactly as specified in FR-10. Do **not**
add a home button now.

Keep the option genuinely open rather than nominally open:

- **Reserve at least one free GPIO** when wiring the encoder (Q-1), so a button can be added
  later without rewiring the panel. This is the one thing that would be expensive to fix
  retroactively.
- Treat "return to the launcher" as a **shell action with one source**, not as a synonym for
  `VeryLongPress`. A second source then plugs in without touching the shell.

## Trigger for revisiting

**v0.2, the Pomodoro app.** It is the first app that uses the full gesture vocabulary in a
modal UI, so it is where a bad hold design becomes obvious. Judge it there, on hardware, by
feel — not in the abstract.

Concretely: if configuring the timer needs visible feedback during the hold, or if the
two-second wait to leave an app becomes annoying in daily use, add the button.

## What changes if the button is added

Only `hal/`:

- `VeryLongPress` becomes unnecessary and is removed from the event vocabulary (FR-8).
- The button becomes a second source of the shell's home action (FR-16).
- Apps regain unrestricted use of `LongPress`, and `PressDown`/`PressUp` become viable
  without ambiguity.
- No app, no shell logic, and no rendering code is affected.

This confinement is not luck — it is the reason FR-7 puts a device-independent event stream
between the hardware and everything else, and the reason the gesture recognizer is
hardware-free and unit-tested.

## Consequences

- Every app must be designed for rotate / press / double-press / hold. This is a real
  constraint on app design and mostly a healthy one: it forces simple interaction models on
  a 64x32 screen that could not carry complex ones anyway.
- Some backlog apps stay awkward regardless of a home button. 2048 needs four directions
  from a one-dimensional control; that is an app-level design problem, not an input-hardware
  problem.
- The gesture recognizer is the component most likely to change. It is therefore the
  component with the most direct unit-test coverage.

## Alternatives considered

- **Encoder plus dedicated home button, now** — deferred, not rejected: it is the cleaner
  interaction model, but it adds wiring, a pin, and enclosure work before it is known
  whether the two-tier hold is actually a problem. Reserving a GPIO keeps the cost of
  changing course near zero.
- **Encoder plus several buttons** — rejected: contradicts the one-knob object the device is
  meant to be, and once there are three buttons the encoder stops being the interface.
- **Double-press for the launcher** — rejected: `DoublePress` is already claimed by apps
  (Spotify skips tracks with it), so this trades one collision for another.
- **No shell gesture at all; restart to switch apps** — rejected: makes the launcher
  pointless and the device unusable as a multi-app object.
