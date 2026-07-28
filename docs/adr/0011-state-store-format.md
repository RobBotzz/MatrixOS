# ADR-0011: One key-value file per namespace for persisted state

- **Status:** Accepted
- **Date:** 2026-07-28

## Context

v0.3 is the milestone that brings persistence, and it does so because three concrete consumers
now exist — which is exactly why Q-7 was left open until this point:

- the shell's last active app (FR-19), today a hard-coded `activateApp(0)`;
- the settings app: brightness and startup app (FR-25);
- Snake's high score (FR-22).

The **location** was already decided and is not in question here: one writable root, separate
from the binary and from the root filesystem that becomes read-only in v0.4 (FR-39,
[ADR-0008](0008-power-loss-resilience.md)). Only the format was open.

Five facts shape the answer:

- **The values are tiny.** A handful of integers and short strings per consumer — tens of bytes
  in total, and there is no plausible growth path towards records or queries. WiFi credentials
  and OAuth tokens join later; they are more of the same.
- **The plug can be pulled at any moment** (NFR-19). Whatever the format, a write must leave
  either the old value or the new one.
- **Logs are volatile on the appliance** and there is no update channel (C-10). When a device
  misbehaves in someone else's flat, the state on the card is one of the very few things the
  maintainer can inspect after the fact.
- **A dependency costs a cross-compilation.** v0.5 is the milestone that pays that price
  knowingly, for TLS, HTTP and JSON. Paying it here would be paying it a milestone early for a
  much smaller problem.
- **Writes happen after the privilege drop.** The matrix library drops from root to `daemon`
  once the panel is initialised (see the Q-1 resolution in [requirements.md](../requirements.md)),
  and every state write — a high score during a game, a brightness change — happens after that.

## Decision

**One plain-text `key=value` file per namespace**, in a single writable root:

```
<root>/snake.conf      highscore=42
<root>/settings.conf   brightness=60
                       startup=Snake
<root>/shell.conf      last_app=Snake
```

- **A namespace is an explicit lowercase identifier** (`snake`, `settings`, `shell`), passed in
  by the consumer — not derived from an app's display name, which is a UI string and may change.
- **Values are strings.** Typed accessors (`int`, `string` — more when something needs them) sit
  on top and fall back to a caller-supplied default whenever the key is missing *or* unparseable. A malformed value and an
  absent one deliberately behave identically: both mean "we do not know", and neither is worth
  failing a device over.
- **Every save replaces the whole file atomically**: write `<name>.conf.tmp` in the same
  directory, `fsync` it, `rename` it onto the target, then `fsync` the directory (FR-40).
- **The root is resolved in `main.cpp`**, the composition root — `$MATRIXOS_STATE_DIR` if set,
  otherwise `/var/lib/matrixos` on the Pi build and `$XDG_STATE_HOME/matrixos` on the host. The
  store itself takes a path and knows nothing about environments.
- **Files are loaded lazily**, on first access to their namespace, and kept in memory afterwards.

## Consequences

- **A namespace is a failure domain.** Snake writing a high score cannot damage the settings, and
  in v0.4 it cannot damage WiFi credentials or tokens. That is the main thing this format buys
  over one shared document, and it costs one file per consumer.
- **The state is readable with `cat` and repairable with a text editor.** On a device whose logs
  vanish at reboot, this is a diagnostic tool, not a nicety.
- **No new dependency, and the parser is small enough to test exhaustively** — a few dozen lines
  with no schema, no versioning, and no migration story to maintain.
- **The store carries no types.** Nothing stops one consumer writing `brightness=high` and
  another reading it as an integer. With three call sites in one binary that is not a real risk,
  and the fallback makes the failure mode boring.
- **Rewriting a file normalises it**: keys are written sorted, and anything that was not a
  `key=value` line — comments, blank lines, hand-made notes — is gone after the next save. Worth
  knowing before editing a file by hand on the device.
- **The root must be writable by the user the process ends up as**, which is `daemon` on the Pi
  and not root. This is a provisioning step, recorded in
  [device-setup.md](../device-setup.md) and destined for `provision.sh` in v0.4. If the root is
  missing or not writable, MatrixOS logs it once and runs **without persistence** rather than
  refusing to start: a configuration mistake must not turn a device into a brick.
- **Escaping is not solved, because it is not needed.** A key is `[A-Za-z0-9_.-]+`, a value is the
  rest of the line with surrounding whitespace removed — so it cannot begin or end with a space —
  and neither may contain a newline. Trimming happens on write as well as on read, so a value
  reads back identically before and after a restart. If a value ever needs a newline or a leading
  space, that is the moment to revisit this record rather than to invent an escape rule.

## Alternatives considered

- **A single JSON document for everything** — the format one reaches for by reflex. It needs a
  parser, so either a dependency cross-compiled a milestone early or a hand-rolled one that is
  strictly more code than the `key=value` parser it replaces. It also makes every write a rewrite
  of the entire device state, tokens included, and its one real advantage — nesting — has nothing
  to nest at this size.
- **SQLite** — genuinely crash-safe and beyond doubt, and disproportionate: a cross-compiled
  library, a schema and a migration path for a few dozen bytes. This is the obvious escalation if
  state ever becomes *records* instead of settings, and nothing in the format above blocks it.
- **One shared `key=value` file with prefixed keys** (`snake.highscore=42`) — fewer files, but it
  throws away the failure domains, and it makes the code path that writes a high score the same
  one that writes credentials.
- **A binary blob** — compact and fast, neither of which is a problem here, in exchange for state
  that cannot be inspected on a device whose logs do not survive a reboot.
- **Plain overwrite in place, trusting ext4's journal** — already rejected in
  [ADR-0008](0008-power-loss-resilience.md): journalling protects metadata, not file contents.
