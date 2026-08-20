# ADR-0013: WiFi provisioning through NetworkManager, driven by `nmcli`

- **Status:** Accepted
- **Date:** 2026-07-29

## Context

[ADR-0007](0007-appliance-provisioning.md) decided *that* the device is provisioned over an access
point and a captive portal, and explicitly left *how* open: "either an existing component (comitup,
balena wifi-connect) or NetworkManager's AP mode plus our own page — that choice is implementation,
not architecture." v0.4 is where that has to be settled.

The facts that decide it:

- **The image already runs NetworkManager.** Raspberry Pi OS Trixie configures the network through
  netplan with NetworkManager as the renderer, and the development device's two WiFi profiles were
  created with `nmcli` (device-setup.md §7). There is nothing to install and nothing to replace.
- **One radio, so the modes are sequential** (C-8). Access point and client cannot run at once; a
  failed join has to bring the access point back.
- **The configuration page is ours regardless** (FR-36, FR-41, FR-42, and account linking in v0.7).
  Whatever handles the setup portal is therefore *additional* to a web server we are building
  anyway ([ADR-0012](0012-own-http-server.md)).
- **Every state has to be visible on the panel** (FR-35, NFR-23). Whatever drives the radio must
  report what it is doing to something we render, not only to a log.
- **The device runs as root** for `/dev/mem` (C-2), so it may reconfigure the network. The privilege
  drop to `daemon` happens inside the matrix library after the panel is initialised, which matters
  for *when* things can be done, not whether.

## Decision

**NetworkManager, driven by `nmcli` as a child process**, behind a small `WifiControl` interface in
`net/`.

Four operations, which is the whole surface:

| Operation | Command |
| --- | --- |
| Scan | `nmcli -t -f SSID,SIGNAL,SECURITY device wifi list --rescan yes` |
| Join | `nmcli device wifi connect <ssid> password <psk> ifname wlan0` |
| Access point | `nmcli connection up matrixos-setup` (a profile in `shared` mode, created by `provision.sh`) |
| State | `nmcli -t -f STATE,CONNECTION device status` |

- **`-t` (terse) output is the parsing contract.** It is colon-separated, field-selected and
  stable across versions in a way the human-readable output is not.
- **Credentials are passed as arguments to a child process we spawn ourselves**, via `posix_spawn`
  with an explicit argument vector — never through a shell. An SSID containing `;`, a space or a
  quote is then a string, not a command (device-setup.md already records an SSID containing `!`
  breaking a shell line).
- **The state machine lives above the interface**, in `os/provisioning`, and is hardware-free:
  `Unconfigured → AccessPoint → Connecting → Connected`, with `Failed` returning to `AccessPoint`.
  It is what both the setup app and the portal read, and it is unit-tested against a fake
  `WifiControl` that returns canned results.
- **Calls happen on the HTTP thread, never on the render thread.** A scan takes seconds; the render
  loop has 16.6 ms. The state machine is mutex-protected and the app only ever reads a snapshot of
  it.
- **On the host build `WifiControl` is a stub** that reports "no WiFi hardware managed here". The
  development machine's own network is never touched — a provisioning tool that reconfigures the
  developer's laptop would be a memorable bug.

## Consequences

- **Nothing new is installed on the device.** `provision.sh` creates one connection profile for the
  access point and one dnsmasq snippet for the captive DNS answer; both are configuration, not
  software.
- **We parse the output of a command-line tool**, which is the honest cost. It is mitigated by
  `-t -f`, by never parsing anything but the four commands above, and by treating an unparseable
  line as "no result" rather than as an error — the same fallback rule the state store uses. A
  NetworkManager upgrade that changes terse output would show up as an empty scan list, which is
  visible on the panel rather than silent.
- **The captive portal needs DNS, not just HTTP.** NetworkManager's shared mode starts dnsmasq for
  DHCP; answering every name with the device's own address takes one line
  (`address=/#/10.42.0.1`) in `/etc/NetworkManager/dnsmasq-shared.d/`. That is a provisioning
  detail, and it is what makes "open any page" land on the setup portal instead of an error.
- **A failed join has a defined path back**, because the access-point profile stays on the device:
  bring it up again, and the panel says why. Nothing depends on a network being reachable to
  recover.
- **Spawning a process from a process that renders at 60 FPS is a thing to be careful with.** The
  child is spawned from the HTTP thread, its output is read to completion with a timeout, and it is
  reaped — a zombie accumulating every scan on a device that runs for months is exactly the kind of
  slow failure C-10 makes expensive.
- **Two units, one image, one access-point name** would be a collision, which is why FR-32's
  serial-derived identity is a hard prerequisite rather than a nicety.

## Alternatives considered

- **comitup or balena wifi-connect** — both solve the portal well and are battle-tested. Rejected
  because each brings its own web server and its own UI: the device would have two HTTP servers,
  two visual identities and two failure modes, and our configuration page (FR-36) would still have
  to exist for the version display, the factory reset and v0.7's account linking. The saved work is
  the portal form; the added work is integration and a second thing to keep alive on a device with
  no update channel.
- **NetworkManager over D-Bus (libnm or sd-bus)** — the technically cleaner interface, with typed
  results and change signals instead of text. Rejected for v0.4 on cost: another library to
  cross-compile and a considerably larger amount of code for four operations. Worth revisiting if
  we ever want to *react* to network changes rather than ask about them — signals are the thing
  polling cannot do well.
- **`wpa_supplicant` and `hostapd` directly** — full control, and a rewrite of what NetworkManager
  already does correctly, including the switch between modes on one radio. It would also mean
  taking the network out of NetworkManager's hands on an image that manages it through netplan.
- **Pre-baking each recipient's credentials into their image** — already rejected as the primary
  path in ADR-0007, and this record does not reopen it.
- **Bluetooth or a companion app** — rejected in ADR-0007 as harder to explain than joining a WiFi
  network.
