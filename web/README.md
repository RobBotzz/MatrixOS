# The configuration page

The page the device serves at `matrixos-xxxx.local` once it is online: network,
version, factory reset, and from v0.7 account linking. A React app, built by Vite into a
**single self-contained `index.html`** and then compiled into the MatrixOS binary
([ADR-0014](../docs/adr/0014-config-page-in-the-binary.md)).

The **setup portal** — the page inside the captive-portal WebView — is deliberately *not*
here. It is plain server-rendered HTML in [`src/net/portal.cpp`](../src/net/portal.cpp),
because that WebView is not a browser and a page that fails there leaves a device nobody can
put into service. Same ADR, and it is the more important half of it.

## Changing the page

```bash
cd web
npm install       # once
npm run embed     # vite build + tools/bundle_to_header.py
```

`npm run embed` regenerates [`src/net/web_assets.h`](../src/net/web_assets.h), which is
**checked in**. That is what keeps Node out of CI and out of the aarch64 cross build — the
same arrangement `tools/bdf_to_header.py` and `font5x7_data.h` have had since v0.1.

Commit the generated header together with the source change. Nothing in the build checks that
the two agree, so forgetting it means a device serving the previous page.

## Working on it live

```bash
npm run dev
```

Vite serves the page on port 5173 with hot reload. It needs a device to talk to, so run
MatrixOS beside it with a simulated radio and let Vite proxy the API across:

```bash
MATRIXOS_STATE_DIR=/tmp/matrixos-dev ./build/bin/MatrixOS --fake-wifi --port 8080
```

The dev server has no proxy configured by default — add one to `vite.config.js` while you
work, or simply open `http://localhost:8080/` once the device is "online" and reload after
`npm run embed`. The second route is slower and needs no configuration to go stale.

To reach the interesting states without hardware, `--fake-wifi` gives the whole provisioning
flow: it scans three invented networks, joins them, fails on a wrong password, and resets.

## Constraints worth knowing before you add anything

- **No external requests.** A strict rule, not a preference: a device on a network with no
  internet must not wait for a CDN that will never answer. No web fonts, no icon packs, no
  analytics. Everything is inlined into the one file.
- **The bundle ends up in RAM on a 512 MB device** (NFR-4). At the time of writing it is
  ~200 KB, which is noise; if it stops being noise, `preact/compat` is a drop-in that costs a
  tenth of it.
- **The API is form-encoded on the way in, JSON on the way out.** The device parses no JSON —
  see [ADR-0012](../docs/adr/0012-own-http-server.md).

| Call | Purpose |
| --- | --- |
| `GET /api/status` | state, SSID, access-point name, version, commit, hostname, scan results |
| `POST /connect` | `ssid`, `psk`, `source=config` — 204 on accepted, 503 when busy |
| `POST /api/scan` | rescan; results arrive through the next `GET /api/status` |
| `POST /api/reset` | factory reset (FR-42) |
