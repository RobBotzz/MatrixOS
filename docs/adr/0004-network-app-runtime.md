# ADR-0004: How network apps get their data

- **Status:** Deferred
- **Date:** 2026-07-25

## Context

The app backlog splits cleanly in two:

- **Self-contained apps** — animations, timers, games, clock. Pure C++, no dependencies.
- **Network apps** — Weather, Spotify, Strava, the upload app. These need HTTPS, JSON, an
  OAuth token lifecycle, and in one case an embedded HTTP server.

For the second group there are two credible architectures: do it in-process in C++, or run a
companion service in a higher-level language and feed the renderer over IPC.

The decision cannot be made well right now, because the information that decides it does not
exist yet. Specifically, nobody has measured how painful cross-compiling curl and a TLS
library for aarch64 with a Pi sysroot actually is in this CI setup — and that, not the
application code, is the real cost driver. JSON and an HTTP server are header-only
libraries; TLS is the part that hurts.

Deciding now would mean either building IPC infrastructure nobody needs yet, or committing to
a C++ toolchain problem that has not been sized.

## Decision

**Deferred.** v0.1 through v0.4 contain no network app, so the decision has no effect on any
code written before then. (Note that v0.4 does bring an embedded HTTP *server* for
provisioning — see [ADR-0007](0007-appliance-provisioning.md) — which is a separate concern
from the HTTP *client* this record is about.)

What is decided now — the single rule that makes deferring free:

> **Apps perform no I/O.** An app receives whatever it needs from outside as an object passed
> in at construction. It knows nothing about HTTP, sockets, files, or process boundaries.

This is a convention, not an abstraction layer. No `DataProvider` base class is written until
there is a second implementation to justify it (NFR-17). Under this rule, either outcome of
this ADR changes zero lines of app code.

## Trigger

The first app that needs data from the network — **Weather, in v0.5**.

## Criteria that will decide it

1. **Cross-compilation cost.** Time-box a spike: get libcurl + TLS building for aarch64 in
   CI with a Pi sysroot. If that spike succeeds within its box, in-process wins by
   simplicity. If it turns into sysroot archaeology, the companion service gains a lot.
2. **OAuth effort.** Spotify and Strava need authorisation-code flow plus token refresh.
   Compare a C++ implementation against a few dozen lines in a Python library.
3. **Render-loop safety.** Whichever option is chosen must guarantee that a hanging request
   cannot stall a frame (FR-27). In-process means a fetch thread with hard timeouts; a
   separate process means a non-blocking read of the last known value.
4. **Operational cost.** One binary and one systemd unit versus two deployables, two
   restart policies, and an IPC protocol to version.

## Options on the table

- **In-process C++** (libcurl or cpp-httplib + nlohmann/json, fetch thread with timeouts) —
  one process, one deployment, no protocol. Cost: cross-compiling TLS; OAuth by hand.
- **C++ renderer + companion service** (Python/Node over a Unix socket or local HTTP) —
  network work in a language with mature API and OAuth libraries; a crashing fetcher cannot
  affect rendering. Cost: two deployables, an IPC contract, harder end-to-end debugging.
- **Build natively on the Pi instead of cross-compiling** — sidesteps the sysroot problem
  entirely by using the Zero 2 W's own toolchain. Cost: slow builds, and CI can no longer
  produce the artifact without emulation or a self-hosted runner. Not a full alternative,
  but it can neutralise criterion 1.

## Consequences of deferring

- FR-26 must be honoured from the very first app, before any benefit is visible. That is the
  price of keeping this open, and it is one sentence of discipline.
- The v0.5 milestone starts with a spike, not with app code.
- If FR-26 is violated in the meantime, this ADR stops being free and the decision has been
  made by accident.
