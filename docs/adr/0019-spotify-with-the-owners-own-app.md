# ADR-0019: Spotify through the owner's own Spotify app

- **Status:** Accepted
- **Date:** 2026-11-01

## Context

v0.7 brings the first app that needs an account. Spotify's Web API still offers what a panel
needs — the currently playing track with its cover, play, pause, skip and volume — but since
February 2026 a Spotify app in Development Mode **needs an owner with Premium and admits at most
five users**, each allow-listed by hand. More users require Extended Quota, which Spotify now
grants only to registered businesses with 250,000 monthly users. Checked on 2026-11-01.

One Spotify app built into MatrixOS would therefore serve five people in total, however many
units exist. The maintainer decided against that: **every owner registers their own Spotify
app**, so nobody runs on the maintainer's quota and the five-user cap applies per household.

Further facts that shape the flow:

- Spotify supports the authorization-code flow with **PKCE**, which needs a Client ID but **no
  client secret** — nothing secret has to be typed into the unit.
- It does not support the device authorization grant (RFC 8628), which answers Q-10.
- Redirect URIs must be HTTPS unless they are a loopback address. The unit's own page is plain
  HTTP on the LAN, so it cannot be the redirect target — but GitHub Pages now serves our static
  pages over HTTPS (ADR-0018), which is the arrangement
  [ADR-0007](0007-appliance-provisioning.md) foresaw.

## Decision

**The owner enters the Client ID of their own Spotify app on the configuration page. The unit runs
the PKCE flow, with a static HTTPS page on GitHub Pages handing the code back to the unit, and
keeps the refresh token on the state partition.**

- **Setup**, guided step by step on the configuration page: create an app at
  developer.spotify.com, add the redirect URI `https://robbotzz.github.io/MatrixOS/callback/` (the
  page shows it with a copy button), tick "Web API", paste the Client ID, press *Connect*.
- **Flow.** The unit draws a code verifier and a state, and sends the browser to Spotify's
  authorize page. Spotify redirects to the static callback page, which forwards `code` and
  `state` to the unit's page at the origin the user started from — carried inside `state` and
  accepted only if it is a `.local` name or a private address, so the page cannot be used to
  redirect anywhere else. Should a browser refuse to navigate from a public page to the local
  network, the callback page shows the code with a copy button, and the configuration page takes
  it pasted.
- **Tokens.** The unit exchanges the code and refreshes the access token through the curl runner
  (ADR-0017). Client ID and refresh token live in `spotify.conf` (FR-24: mode 0600, never
  logged); tokens reach curl through its standard input, not its arguments, so they never show
  in a process list. A factory reset forgets both (FR-42).
- **Polling.** `GET /v1/me/player/currently-playing` every 5 seconds — **only while the Spotify
  app is on the panel**, so an idle unit costs the owner's quota nothing.
- **Cover art on the unit.** Spotify sends JPEGs; the 64-pixel version is decoded with
  **stb_image** (a single public-domain header, vendored) and scaled to 32x32. This is the first
  image decoder on the unit — the pictures of v0.6 arrive already converted.
- **The app.** Cover on the left half, title and artist on the right, scrolling when too long,
  a progress bar along the bottom. Press: play/pause. Double-press: next track. Long press:
  previous track. Rotate: volume, sent once the knob rests. Nothing playing, or no Spotify
  connected: said so on the panel. The unit does not switch to the app when music starts — that
  would mean polling while the app is off screen.

## Consequences

- Each owner needs Premium and five minutes in Spotify's dashboard. Fine for the people who
  will get units; not something to hand to someone who has never seen a developer page.
- Control (play, pause, skip, volume) needs Premium on the listening account too, which is
  Spotify's rule for its player API. Without it the app still shows what is playing.
- One more static page on GitHub Pages, published by the existing workflow.
- stb_image enters `external/`. Only its JPEG path is compiled in.
- The Spotify app is interruptible: a picture arriving takes the panel (ADR-0018).
- Strava, the other half of v0.7 in the roadmap, is deferred. When it comes it shows the owner's
  own statistics only — the API agreement forbids showing an athlete's data to anyone else.

## Alternatives considered

- **One Spotify app for all units** — no setup for owners, but five users in total, on the
  maintainer's quota. Rejected by the maintainer.
- **Client secret instead of PKCE** — would mean typing a secret into the unit and storing it.
  PKCE needs neither.
- **The paste-the-code flow only** — robust, but a needless extra step whenever the redirect
  works. It stays as the fallback.
- **Cover art converted elsewhere** — there is no browser in the loop while music plays, and a
  service of our own is what ADR-0007 rejected. A vendored decoder for one 64-pixel JPEG is the
  smaller thing.

## Addendum, 2026-11-05 — volume readback

Spotify answers the next `GET /me/player` with the old level for a moment after a volume command.
That answer overwrote the level just set, so the number jumped back and the next turn of the knob
started from the wrong value. The level set on the unit now wins over polled levels until Spotify
reports the same value, or until 3 seconds after the command finished. If several levels queue up
behind a slow request, only the latest is sent.

One detent is 2 percent instead of 5, and the panel shows `45%`: `VOL 45` ran off the right edge
and left only the tens digit visible. Some devices round the level to coarser steps of their own,
which the unit cannot change.
