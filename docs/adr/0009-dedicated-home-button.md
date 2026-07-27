# ADR-0009: A dedicated home button alongside the encoder

- **Status:** Accepted
- **Supersedes:** [ADR-0006](0006-single-encoder-input.md)
- **Date:** 2026-07-26

## Context

[ADR-0006](0006-single-encoder-input.md) chose a single rotary encoder as the only control and
paid for it with a two-tier hold: apps received `LongPress` on release between 600 ms and 2 s,
while the shell claimed `VeryLongPress` at 2 s to return to the launcher. It named a trigger
for revisiting — the Pomodoro app in v0.2 — and asked for a spare GPIO to keep the option
cheap.

The decision is being taken before that trigger, and before any input code exists. Two things
made waiting pointless:

- **The cost was already known and did not need trying.** An app cannot show feedback while
  the button is held, because `LongPress` only arrives on release. And leaving an app takes a
  two-second wait, every single time. Neither was speculative.
- **Q-1 settled the pin budget.** With `regular` mapping and one chain, twelve GPIOs are free.
  The button costs one of them. There was no scarcity to trade against.

Deciding now is strictly cheaper than deciding in v0.2: no gesture recognizer is written
twice, and no app is designed around a constraint that is about to disappear.

## Decision

The device has a **dedicated home button in addition to the rotary encoder with its push
button**.

The home button does exactly one thing: **toggle between the launcher and the active app.**
Pressed inside an app it shows the launcher; pressed in the launcher it returns to the app it
came from. It selects nothing, configures nothing, and has no second meaning.

App selection stays on the encoder — rotate through the list, press to start.

`VeryLongPress` is removed from the event vocabulary. `LongPress` now fires **when the hold
threshold is reached, while the button is still held**, so an app can react at the moment the
user expects it. Once `LongPress` has fired, the following release does not additionally
produce `Press`. No gesture is reserved for the shell any more.

## Consequences

- **Apps get responsive holds.** "Hold to enter configuration mode" shows feedback
  immediately, which is what Pomodoro and Weather in the backlog wanted and could not have.
- **Leaving an app is instant** rather than a two-second wait — and that is the most repeated
  interaction on the whole device.
- **The gesture recognizer gets simpler:** three timing rules instead of four, and no
  cancellation logic for a pending event that may or may not be delivered.
- The shell's home action now has one dedicated source, and no app needs to be prevented from
  seeing it. Modelling it in ADR-0006 as "an action with one source" rather than as a synonym
  for `VeryLongPress` turns out to have been the right shape — for a change in the opposite
  direction from the one anticipated.
- Toggling *back* means the shell remembers which app it came from. That is one member, not a
  navigation history.
- **Cost:** one more GPIO, one more component to wire and mount, and a second debounced input
  in `hal/`. The containment predicted by ADR-0006 held: this change touches `hal/` and the
  shell's event dispatch. No app, no `gfx/`, no rendering path.
- `PressDown`/`PressUp` become viable without ambiguity if an app ever needs to know how long
  a hold lasted. Not added now — nothing in the backlog needs it (NFR-17).
- 2048 stays awkward. Four directions from a one-dimensional control was never an
  input-hardware problem, and a home button does not fix it.

## Alternatives considered

- **Keep the single encoder** (ADR-0006) — superseded. It is the more elegant object, but the
  two-second exit and the unresponsive hold are costs paid at every single interaction, in
  exchange for one saved GPIO out of twelve free ones.
- **Give the home button more functions** — long-press for shutdown, double-press for
  settings — rejected: the entire value of this button is that it is unambiguous. A button
  that always does the same thing needs no learning and no documentation.
- **Wait for the v0.2 trigger as ADR-0006 specified** — rejected: the trigger existed to
  avoid deciding by guesswork, but nothing about this trade needed measurement. Deciding
  before the code exists is what makes the change free.
