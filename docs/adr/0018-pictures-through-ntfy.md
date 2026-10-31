# ADR-0018: Pictures between people, through ntfy.sh

- **Status:** Accepted
- **Date:** 2026-10-31

## Context

v0.6 was planned as "send a picture to the device from a phone". The maintainer's goal is wider:
**people send each other pictures**, from anywhere, as simply as possible. Two facts shape the
answer:

- A unit sits behind somebody's home router. Nothing on the internet can reach it; it can only
  ask outward, as the weather app does ([ADR-0017](0017-http-through-the-curl-binary.md)). So a
  picture has to wait somewhere until the receiving unit collects it — a mailbox.
- [ADR-0007](0007-appliance-provisioning.md) rejected a maintainer-hosted service: nothing to run,
  monitor or pay for. A static page is acceptable; ADR-0007 already relies on one.

[ntfy.sh](https://ntfy.sh) is a free, open-source publish/subscribe service with no account.
Anyone who knows a topic name can publish to it and read from it. Checked on 2026-10-31:
attachments up to 15 MB (100 MB per visitor), **kept for 3 hours**; polling without a long-lived
connection via `GET /<topic>/json?poll=1&since=<id>`; `Access-Control-Allow-Origin: *`, so a
browser page can publish directly.

## Decision

**Each unit has a secret ntfy.sh topic as its address. Pictures are converted to 64x32 in the
sender's browser on a static page and published to that topic as a file; the unit polls the topic
and keeps the last 30.**

- **Address.** On first boot a unit draws `matrixos-` plus 20 random characters from
  `/dev/urandom` and stores it on the state partition. A scrubbed image has none, so every clone
  draws its own. The configuration page shows the unit's send link and can draw a new address,
  which makes the old link useless. A factory reset (FR-42) draws a new address and deletes the
  pictures: a unit passed on must not keep receiving its previous owner's mail.
- **Sending.** A static page on GitHub Pages, `…/send/#<topic>` — the topic is in the fragment,
  so it never reaches GitHub's logs. Anyone with the link can send, with or without a unit of
  their own. The page decodes a still image or a GIF, fits it to 64x32, previews it exactly as the
  panel will show it, and `PUT`s the result to `https://ntfy.sh/<topic>`.
- **Format.** Our own, so the unit needs no image decoder: a short header (magic, 64x32, frame
  count, a delay per frame) followed by raw RGB24 frames. At most 100 frames — about 600 KB.
- **Receiving.** A service like `WeatherService`: a worker polls once a minute through the curl
  runner, downloads each new attachment with a size cap, validates it strictly — it comes from
  anyone who has the link — and writes it atomically into `/var/lib/matrixos/gallery/`. Beyond 30
  pictures the oldest is deleted. The last message ID it has seen is stored so a restart does not
  fetch everything again.
- **Showing.** A Gallery app: newest first, rotate to browse, pictures take turns every 30 seconds
  as the weather windows do, animations play at their own frame delays.
- **A new picture takes the panel** — the shell switches to the Gallery, showing it — unless
  Snake, the Morse trainer or Pomodoro is on the panel, or setup has it. Being dropped out of a
  game or a focus phase by a photo is worse than seeing it a minute later; it then waits until
  that app is left. Apps say whether they may be interrupted; those three say no.

## Consequences

- No server of our own, no account, no cost. The sending page is a file GitHub serves.
- Pictures pass through a third party and anyone holding the link can read them while ntfy keeps
  them. That is the price of the mailbox; the page says so. ntfy can be self-hosted later, which
  changes one base URL on the unit and one in the page.
- A unit that is offline for more than three hours misses what was sent meanwhile: the
  attachments are gone by the time it asks. Acceptable for a picture frame; stated on the page.
- Anyone with the link can send anything, including things the owner does not want on the wall.
  The answer is drawing a new address, not moderation.
- The unit writes to the card when a picture arrives. NFR-20 allows that: it is the device being
  asked to remember something, and nothing is written while nothing arrives.
- [ADR-0012](0012-own-http-server.md)'s premise holds — the unit *downloads*; its own server still
  never receives a file.
- The sending page needs a deploy pipeline: a GitHub Actions job that publishes it to Pages.

## Alternatives considered

- **Upload to the unit over the home network** — simplest, but only someone in the same WLAN can
  send, which misses the point. It would also have been ADR-0012's trigger for a server library.
- **A server of our own** (a Cloudflare Worker with storage, about 100 lines) — full control,
  contact lists, permissions. Rejected for now on ADR-0007's grounds: it is a service to run and
  answer for. If ntfy.sh's limits ever bite, self-hosting ntfy is the smaller step.
- **A shared cloud folder** (Google Drive, Dropbox) — needs OAuth on the unit, which is v0.7's
  problem, and an account for every sender.
- **Decoding on the unit** — PNG, JPEG and GIF decoders in C++ on a Pi Zero, a large GIF as a RAM
  risk, and no preview for the sender. The browser already decodes images.
