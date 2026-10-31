# ADR-0017: HTTP through the `curl` binary, in-process

- **Status:** Accepted
- **Date:** 2026-10-31
- **Resolves:** [ADR-0004](0004-network-app-runtime.md)

## Context

ADR-0004 left open how network apps get their data, with Weather in v0.5 as the trigger. Its
first criterion was the cost of cross-compiling libcurl and a TLS library for aarch64, which
CI has never done: the cross build uses Ubuntu's `gcc-aarch64-linux-gnu` without a Pi sysroot.

Since ADR-0004 was written, v0.4 settled a pattern that ADR-0004 did not consider. WiFi is
driven by running `nmcli` as a child process with an explicit argument vector and a hard
timeout ([ADR-0013](0013-wifi-provisioning-via-networkmanager.md)), from a worker thread that the
render loop never waits on. And `provision.sh` already installs `curl` on every device, for
`deploy.sh`.

## Decision

**Network requests run the device's `curl` binary** the way WiFi runs `nmcli`: same process
runner, explicit arguments, no shell, a hard timeout, from a worker thread. Responses are parsed
with **nlohmann/json**, header-only and fetched at configure time. Weather data comes from
**Open-Meteo**, which needs no key and no account.

## Consequences

- No TLS library is cross-compiled, no sysroot, no change to CI. TLS, certificates and
  redirects are curl's and Debian's problem, and Debian keeps them patched.
- One process per request. At one request every 15 minutes that is nothing; a source that polls
  every few seconds (live scores, in the backlog) would make this worth revisiting.
- The process runner moves out of `nmcli_wifi.cpp` into `os/process`, now that it has a second
  user.
- FR-27 holds by construction: curl's `--max-time` and the runner's own kill-on-timeout bound
  every request, and only the worker waits for it.
- Apps stay free of I/O (FR-26). The weather app reads a snapshot from a service that owns the
  worker; the service takes the fetch as a function, so tests hand it canned JSON instead of a
  network. That needs no interface class.
- OAuth in v0.7 needs POST requests and form bodies, which curl does. Whether a token flow is
  pleasant to write this way is the question for v0.7, not this record.

## Alternatives considered

- **libcurl or cpp-httplib in-process** — a nicer API, at the cost ADR-0004 named: a TLS
  library built for aarch64, either from a sysroot or from source (mbedTLS) in CI. Nothing the
  first network app needs justifies that.
- **A companion service in Python** — mature HTTP and OAuth libraries, isolation from crashes.
  Two deployables, two restart policies and an IPC contract, for a weather request. It stays the
  option to re-examine if OAuth in v0.7 turns out painful.
- **Building natively on the Pi** — neutralises the cross-compile problem but makes builds slow
  and takes the artifact out of CI. Unnecessary once no TLS library is compiled at all.
