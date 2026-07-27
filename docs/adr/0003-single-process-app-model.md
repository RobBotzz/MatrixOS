# ADR-0003: One process, one active app, cooperative tick

- **Status:** Accepted
- **Date:** 2026-07-25

## Context

The project is named MatrixOS and the original sketch described an "OS" module that
"manages all the apps". That framing invites a process model, a scheduler, background apps,
and dynamic loading — infrastructure that would dominate the effort and delay the first
working device indefinitely.

What the device actually needs: the user looks at one thing at a time, and switches with a
single encoder. The backlog of ~18 apps contains no case where two apps must be visible or
running simultaneously.

The panel itself imposes a hard constraint: rpi-rgb-led-matrix runs a high-priority updater
thread whose timing determines whether the display flickers. Any additional concurrency is a
risk to that, not a free win.

## Decision

MatrixOS is a **single process** with **one active app** and a **cooperative tick loop**.
Apps are compiled in, registered at startup, and implement one interface
(`onEnter`/`onExit`/`onInput`/`update(dt)`/`render(Surface&)`). Inactive apps receive no
ticks. There is no scheduler, no per-app process, no dynamic loading, no scripting layer.

An app that throws is unloaded and the user is returned to the launcher; the process
survives.

## Consequences

- Adding an app means one class plus one registration line (FR-18, G2).
- No IPC, no serialisation, no lifecycle races — the entire control flow is one readable
  loop.
- Apps are ordinary objects, so their logic is unit-testable without any runtime.
- `update(dt)` uses measured elapsed time, so behaviour is frame-rate independent (NFR-2).
  No fixed-timestep accumulator is introduced; if a game needs deterministic physics, it
  can accumulate internally.
- A misbehaving app can still stall rendering by blocking inside `update()`. Accepted: the
  exception boundary catches crashes, not hangs. `systemd` restarting the service is the
  backstop.
- Anything long-running (a network fetch) must not sit in `update()`. This is why FR-26
  and FR-27 exist, and why apps do no I/O of their own.
- Apps share one address space, so there is no isolation. Accepted for a single-user
  personal device with compiled-in apps.
- "Restore the last app after restart" (FR-19) needs persistence and is therefore not free
  — it waits for v0.3.

## Alternatives considered

- **One process per app, managed by a supervisor** — rejected: real isolation and crash
  containment, but it requires an IPC protocol for the framebuffer and input, a deployment
  story per app, and shared access to the panel. Weeks of infrastructure for a problem the
  exception boundary solves adequately.
- **Cooperative multitasking with background apps** — rejected: nothing in the backlog needs
  it. If a future app needs to keep data warm while inactive, that is a job for a data
  provider owned by the shell, not for ticking hidden apps.
- **Dynamically loaded app plugins (`.so`)** — rejected: solves distribution and hot-reload,
  neither of which is a goal. Compile times on this project are not the bottleneck.
- **Lua or Python scripting for apps** — rejected: attractive for fast iteration, but it
  introduces an embedded runtime, a binding layer, and a second language before a single app
  exists. Reconsider only if writing apps in C++ turns out to be the actual friction.
