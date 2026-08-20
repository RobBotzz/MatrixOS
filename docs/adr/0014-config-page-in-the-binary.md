# ADR-0014: Two web pages, built differently, both compiled into the binary

- **Status:** Accepted
- **Date:** 2026-07-29

## Context

v0.4 puts two pages on the device, and they look like one problem until you look at where each one
is opened.

**The setup portal** is opened inside the *captive-portal browser* — the cut-down WebView that
Android and iOS pop up when they detect a network with no internet access. That environment is not
a browser: JavaScript support varies, storage APIs are often unavailable, third-party requests are
blocked, and the window closes without warning as soon as the OS decides the network is fine. It is
also the single point where setup can fail irrecoverably: a page that does not render there leaves
a device nobody can put into service (NFR-23).

**The configuration page** is opened at `matrixos-xxxx.local` in a real browser on a phone or a
laptop, by someone whose device is already working. It shows the version (FR-41), the network
state, the factory reset (FR-42), and from v0.7 the account linking (FR-37). It is the face of the
product, and the owner wants it to look like one.

Two further constraints come from elsewhere in the project: the root filesystem is read-only
([ADR-0008](0008-power-loss-resilience.md)), so there is no natural place to put files that the
server reads; and CI cross-compiles for aarch64 on a runner that has no reason to grow a JavaScript
toolchain.

## Decision

**Build the two pages differently, and compile both into the binary.**

- **The setup portal is plain HTML**, generated server-side in C++, with inline CSS and no
  JavaScript at all. The network list is a `<select>` filled from the scan, the password is an
  `<input type="password">`, and submitting is a plain form `POST`. It works in a WebView from 2015.
- **The configuration page is a React app**, built with Vite in `web/`, configured to emit a
  **single self-contained `index.html`** with CSS and JS inlined. One asset, no request waterfall,
  no code splitting.
- **Both are turned into a generated C++ header by `tools/bundle_to_header.py`**, and the generated
  header is **checked in** — exactly the arrangement `tools/bdf_to_header.py` and `font5x7_data.h`
  already use for the font. The generator is in the repository so the numbers are verifiable rather
  than magic, and the output is in the repository so no build needs Node.
- **`npm run build` is a developer step, not a build step.** Changing the page means running the
  build, running the generator and committing both. A CI check that the generated header matches
  the sources is possible later; it is not worth a Node install in the aarch64 job today.

## Consequences

- **The setup path depends on nothing.** The one page that must not fail has no JavaScript, no
  fonts, no external requests, and no build tooling between the source and the device. This is the
  point of splitting them.
- **CI and the Pi build never see Node.** The cross build compiles a header full of bytes, exactly
  as it does for the font.
- **A read-only root is a non-issue**, because nothing is read from disk. It also removes an entire
  class of path-traversal bug from the server: there is no filesystem behind the routes.
- **The generated header is a build artifact in version control**, with the honest downside that a
  diff on it is unreadable and it will be stale if someone edits `web/` and forgets the generator.
  The mitigation is that the page is one file and the generator is one command, both documented in
  `web/README.md`. This is a conscious repeat of a pattern that has already worked once here.
- **Inlining everything makes the page one request** and therefore also sidesteps a weakness of our
  own server — parallel asset connections ([ADR-0012](0012-own-http-server.md)) — though the server
  handles them properly anyway.
- **The binary grows by the size of the bundle**, on the order of 150 KB gzipped for a small React
  app. Against 512 MB of RAM and NFR-4's 64 MB budget that is noise; if it ever stops being noise,
  Preact is a drop-in that costs a tenth of it.
- **Two UIs to keep visually consistent.** Accepted, and the setup portal is deliberately the
  plainer of the two — it is seen once, for ninety seconds, by someone who wants it to be over.

## Alternatives considered

- **React for both pages** — one code base, one look. Rejected on the captive-portal WebView: the
  risk is not that it usually works, it is that when it does not, the device cannot be set up at all
  and the user has no way to tell why. Trading that risk for visual consistency on a page seen once
  is a bad deal.
- **Plain HTML for both** — no Node in the project, everything server-rendered, and the fastest
  route to a working v0.4. Rejected because the configuration page is where the appliance is judged,
  and because it is the page that grows: account linking, upload management, whatever v0.6 and v0.7
  add. Hand-written DOM manipulation is the wrong foundation for that.
- **Serving assets from a directory on the card** — the normal way to ship a web app, and it fights
  both the read-only root and the golden-image workflow: an extra directory to provision, to scrub
  and to keep in step with the binary. Compiling them in makes binary and UI one artifact, which is
  what "the running version" (FR-41) should mean.
- **Building the bundle in CMake as part of the build** — reproducible and self-updating, at the
  price of Node in the aarch64 CI job and in anyone's clone. Given that the page changes rarely and
  the font precedent exists, the generator-plus-checked-in-output trade is the cheaper one.
- **A cloud-hosted configuration page talking to the device** — rejected for the same reason
  ADR-0007 rejected a maintainer-hosted callback service: something to run, monitor and pay for,
  and it would stop working the day it is switched off, on devices that have no update channel.
