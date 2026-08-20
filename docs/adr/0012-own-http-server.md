# ADR-0012: An HTTP server of our own

- **Status:** Accepted
- **Date:** 2026-07-29

## Context

[ADR-0007](0007-appliance-provisioning.md) makes the HTTP server platform infrastructure rather
than a detail of one app, and v0.4 is where it has to exist. Four consumers are known, spread over
three milestones:

| Milestone | Route | Shape |
| --- | --- | --- |
| v0.4 setup portal | `GET /`, `POST /connect` | scan list, one form, body under 1 KB |
| v0.4 captive probes | `GET /generate_204`, `/hotspot-detect.html`, `/connecttest.txt` | 302 to `/` |
| v0.4 config page | `GET /`, `GET /api/status`, `POST /api/reset` | a small SPA plus a JSON API |
| v0.7 OAuth | `GET /oauth/callback?code=…` | one query parameter |

What the server does **not** have to do is as important as what it does. It is reachable only on
the LAN or on its own access point, it serves one user at a time, all its bodies are tiny, and it
carries no TLS — a self-signed certificate on an appliance produces exactly the browser warning an
appliance must not produce, and a real certificate needs a name we do not own.

Two things changed the calculation while this was being decided, and both are recorded here
because they were the deciding facts rather than background:

- **Uploads left the roadmap.** v0.6 was going to receive images through this server, which is the
  one requirement that genuinely argues for a library — multipart parsing is fiddly and easy to get
  wrong. That content is now planned to come from an external storage service, so the device
  *downloads*, which is an HTTP client and a different decision entirely (Q-6,
  [ADR-0004](0004-network-app-runtime.md)).
- **The configuration page becomes a React app** ([ADR-0014](0014-config-page-in-the-binary.md)),
  which means serving static assets and, more importantly, surviving the six parallel connections a
  browser opens for them.

The constraint underneath everything: the render loop may never wait. NFR-1 gives a frame 16.6 ms,
and `accept()` blocks indefinitely by nature.

## Decision

**Write it ourselves** — roughly 600 lines in `net/http_server.{h,cpp}`, with no third-party
dependency.

- **One dedicated thread**, started and stopped by the shell. It never touches a `Surface` and the
  render loop never touches a socket; the two exchange a small mutex-protected state object
  ([ADR-0013](0013-wifi-provisioning-via-networkmanager.md) owns what is in it). This is the one
  place in MatrixOS with a second thread of our own, and it exists because FR-27 requires it.
- **`poll()` over all open connections**, not one connection at a time. A sequential accept loop is
  simpler and would deadlock the moment a browser opens a second socket and keeps the first alive —
  the failure would look like a page that loads halfway, which is the worst kind of bug to
  diagnose in someone else's flat.
- **A route table**: exact path plus method to a handler returning a status, a content type and a
  body. Handlers are plain functions that see a parsed request; they do not touch sockets.
- **Static assets are served from memory**, compiled into the binary. Nothing is read from disk,
  which suits a read-only root filesystem ([ADR-0008](0008-power-loss-resilience.md)).
- **Hard limits, from the first line of code**: 8 KB of request headers, 64 KB of body, a 5 s
  timeout per connection, 8 connections at once. Anything over a limit is answered with a status
  and the connection is closed. On a device with no swap, an unbounded reader is an out-of-memory
  kill (NFR-4, NFR-20).
- **No authentication in v0.4.** Decided explicitly rather than skipped: see the consequences.

## Consequences

- **The HTTP parsing bugs are ours.** This is the real cost, and the mitigating facts are that the
  parser is small enough to test exhaustively, that it is reachable only from the LAN, and that a
  request it does not understand is answered with 400 rather than interpreted generously. There is
  no proxy in front of it, so the request-smuggling class of bug has nothing to smuggle past.
- **It is testable on the host**, which is why it can be built at all before the hardware exists:
  the tests bind to `127.0.0.1:0` and speak real HTTP over a real socket. Faking the socket would
  have tested the fake — the same reasoning that made the state store's tests use a real directory.
- **No cross-compilation risk.** POSIX sockets and `poll()` need nothing that the aarch64 toolchain
  does not already ship, so the Pi build stays as it is. v0.5 pays for TLS and HTTP *client*
  libraries knowingly; this milestone pays for nothing.
- **v0.4 ships without a password on the configuration page**, and that is a real gap worth naming
  rather than hiding. Anyone on the home network can open the page, see the version, and trigger a
  factory reset. Nothing worth stealing is on the device yet — WiFi credentials do not leave it, and
  the page never displays them. **The trigger for adding authentication is v0.7**, where OAuth
  tokens arrive and the page starts linking accounts. The honest limitation to record now: without
  TLS, a password protects against a housemate, not against someone who can watch the LAN.
- **Sessions, cookies and CSRF are therefore also absent.** `POST /api/reset` is protected by
  nothing but being on the local network. Adding a token to that form is trivial and belongs with
  the authentication work, not before it.
- **Keep-alive is supported, HTTP/2 is not, and range requests are not.** A browser loading a SPA
  from a 64x32 appliance needs neither.

## Alternatives considered

- **cpp-httplib, vendored** — the strongest alternative. Header-only, MIT (compatible with our
  GPL-2.0), cross-compiles without trouble, and it does exactly the part a SPA stresses: static
  files with correct MIME types, keep-alive, and parallel connections. It lost on three counts —
  a 9,000-line header in the tree for four routes; a thread-per-request model that is generous with
  RAM on a device where RAM is the binding limit (NFR-20); and the disappearance of the upload
  requirement, which took its best argument with it. **If the device ever has to receive a file,
  this is the record to revisit** — the change costs `http_server.cpp` alone, because the routes
  sit above it.
- **mongoose** — very small, GPLv2-compatible, handles multiple connections natively. Its
  event-driven design is meant to avoid a thread; we need a thread anyway to keep the render loop
  free, so the main advantage does not apply. The C callback API would sit crosswise to the rest of
  the code base.
- **A separate process serving the pages** (nginx or a small Python service, talking to MatrixOS
  over a socket) — moves the parsing risk out of our binary, at the price of a second thing to
  install, supervise and keep in sync on a device with no update channel (C-10).
- **No server at all: configure over SSH** — rejected by ADR-0007's premise. Requiring a terminal
  is exactly what an appliance may not do (NFR-23).
- **TLS with a self-signed certificate** — rejected: the browser warning it produces teaches the
  user to click through security warnings, on the one page where they enter their WiFi password.
  Plain HTTP on a link-local address is the more honest failure mode.
