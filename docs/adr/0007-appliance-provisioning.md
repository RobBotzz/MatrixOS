# ADR-0007: Appliance provisioning — golden image, captive portal, setup as an app

- **Status:** Accepted
- **Date:** 2026-07-26

## Context

MatrixOS is not only a personal device. The goal is a small number of finished units —
**single-digit quantities**, built and flashed by the maintainer — that a non-technical
person can put into service themselves. A few setup steps are acceptable; requiring a
terminal, an SD card writer, or an SSH session is not.

That target changes several assumptions:

- The user never installs an operating system. They receive a device with a card in it.
- There is no keyboard, no monitor, and no browser on the device. WiFi credentials have to
  arrive some other way.
- The Pi Zero 2 W has a **single WiFi radio**, so access-point mode and client mode are
  sequential, not simultaneous.
- Apps that talk to Spotify or Strava need OAuth, which wants a browser and an HTTPS
  redirect — neither of which the device has.
- There is no update channel (see [ADR-0005](0005-deployment-model.md) and the addendum
  there). A defect means the maintainer reflashes the card, so the image must be
  rebuildable from scratch.

At the same quantity, Spotify's API quota is not a constraint: development mode allows
manually allow-listed users, which covers fewer than ten devices.

## Decision

Build the device as an **appliance**, with five parts:

**1. A golden image, produced by a script.** All device configuration lives in a single
`provision.sh` run against a fresh Raspberry Pi OS install. The shipped image is then
`dd` + PiShrink of a provisioned card. Manual, undocumented configuration steps are not
allowed — the script *is* the specification of a device.

**2. Unique identity on first boot.** A first-boot step derives the hostname and the
setup access-point name from the CPU serial (`matrixos-a3f1`), because a cloned image
otherwise gives every unit the same name and they collide on the same network.

**3. WiFi via access point and captive portal.** With no known network available, the device
opens `MatrixOS-Setup`, serves a page where the user picks their network and enters the
password, then switches to client mode. On failure it returns to access-point mode and says
so. Either an existing component (comitup, balena wifi-connect) or NetworkManager's AP mode
plus our own page — that choice is implementation, not architecture.

**4. Setup is an app.** The panel shows the setup state (setup mode, connecting, connected,
failed) as a normal `App` that the shell activates while the device is unconfigured. No
separate mode, no special case in the loop. This is the project's main advantage over
typical headless IoT onboarding, which happens blind behind a blinking LED.

**5. Account linking from the device's own web page.** Once online, the device is reachable
at `matrixos-a3f1.local` (mDNS) and serves a configuration page. OAuth runs from there:
the registered redirect URI points at a **static HTTPS page** hosted by the maintainer
(GitHub Pages is sufficient — no server-side component), which immediately redirects back to
the device on the LAN with the authorization code. A top-level navigation from HTTPS to a
private HTTP address is permitted, where a `fetch()` would be blocked as mixed content.

The platform owns tokens end to end: acquisition, storage, refresh. Apps never see them,
which is the existing FR-26 rule and needs no extension.

## Consequences

- The embedded HTTP server becomes **platform infrastructure**, not a detail of the upload
  app. It carries the setup portal, the configuration page, and the OAuth callback, and is
  later reused for uploads. It therefore moves earlier in the roadmap.
- The setup flow needs a factory reset (forget WiFi and tokens) reachable without a
  terminal, for changed routers or passing the device on.
- The running version must be visible on the configuration page. Without an update channel,
  a reported problem can otherwise not be traced to a build.
- The golden image must be scrubbed before cloning: maintainer WiFi profiles, SSH host keys,
  shell history, logs, and state. Building the image on a Pi that is never joined to the
  maintainer's own network removes the risk structurally rather than by checklist.
- Device state must live in one writable location, which is also what
  [ADR-0008](0008-power-loss-resilience.md) requires.
- The OAuth redirect depends on browser behaviour that may tighten over time (Chrome's
  Private Network Access work currently targets subresource requests, not top-level
  navigations). The fallback needs no browser cooperation at all: the static page displays
  the code and the user pastes it into the device's page. Because the worst case is known
  and merely less elegant, this part can be built when the first OAuth app is built.

## Deferred within this decision

**pi-gen and a CI-built image.** The `dd` + PiShrink route produces the same artifact — a
flashable `.img` — so switching later breaks nothing and existing devices are unaffected.
What makes the switch cheap is `provision.sh`: migrating means transcribing it into pi-gen
stages. Trigger for revisiting: building the image a third time, wanting CI to produce it,
or needing to prove what is on a shipped unit.

## Alternatives considered

- **Configure each device by hand** — rejected: not reproducible, and it stops scaling at
  about two units. It also makes every support question unanswerable, because no two devices
  are alike.
- **pi-gen from the start** — deferred, not rejected: the right tool eventually, but a build
  system before the first device exists. `provision.sh` captures the same knowledge at a
  fraction of the effort.
- **Pre-bake each recipient's WiFi credentials into their image** — rejected as the primary
  path: zero setup for the user, but it breaks on a new router or password, requires
  collecting their credentials in advance, and leaves them in an image file. Fine as a
  convenience on top of the portal, not instead of it.
- **Bluetooth provisioning** — rejected: needs a companion app or a pairing flow that is
  harder to explain than joining a WiFi network.
- **balenaOS / balenaCloud** — rejected for now: it would supply provisioning, OTA updates,
  and fleet management ready-made, and is free at this scale. The cost is Docker on the
  device with a privileged container for `/dev/mem` and a heavier footprint on 512 MB. That
  trade was declined in ADR-0005 and remains declined **only as long as updates stay out of
  scope**. If updates become necessary, this is the first alternative to re-examine.
- **OAuth device authorization grant (RFC 8628)** — would be ideal for a device with a
  display and no keyboard, but Spotify's and Strava's public APIs do not appear to offer it.
  Worth verifying before building the redirect flow (Q-10); if it exists, the static page
  becomes unnecessary.
- **A maintainer-hosted cloud service for callbacks and pairing** — rejected: a static page
  achieves the same for this flow with nothing to run, monitor, or pay for.
