# ADR-0005: Versioned releases with a systemd service

- **Status:** Proposed
- **Date:** 2026-07-25

## Context

Deployment today: `pi-deployment/deploy.sh` queries the GitHub Actions API, takes
`artifacts[0]`, downloads the zip with a PAT, and unpacks it into
`/home/robin/MatrixOS/build`. The binary is then started by hand.

Problems with that as a permanent arrangement:

- `artifacts[0]` is the newest artifact of *any* workflow run, including pull-request builds
  — so a deploy can silently install a branch build.
- Nothing identifies which version is installed. There is no way to answer "what is running
  on the device?" and no way back to the previous build.
- Unpacking overwrites the running binary's location in place.
- The service is not started on boot and is not restarted if it exits, so NFR-7 and NFR-8
  are unmet.
- The binary needs root (C-2), which is currently handled by remembering to type `sudo`.
- `$TEMP_ZIP` is used without being defined in the script.

## Decision

Two channels, both keeping the existing PAT-based pull model (the device pulls; no inbound
access to the Pi is required):

**Development channel** — the current artifact pull, fixed: define `TEMP_ZIP`, and filter
the artifact query to successful `main` builds instead of taking index 0.

**Release channel** — git tag triggers a GitHub Release with a versioned tarball. On the
device:

```
/opt/matrixos/releases/<version>/     unpacked release, immutable
/opt/matrixos/current -> releases/<version>    symlink
/etc/systemd/system/matrixos.service  ExecStart=/opt/matrixos/current/MatrixOS ...
```

Deploying = unpack a new release directory, switch the symlink, `systemctl restart`.
Rolling back = point the symlink at the previous directory and restart. The unit runs as
root (C-2), with `Restart=always` (NFR-7) and `WantedBy=multi-user.target` for autostart.
Panel geometry and tuning flags live in the unit's `ExecStart` line, satisfying FR-5 without
needing a config file yet.

## Consequences

- The running version is visible from the symlink target, and rollback is one command
  (NFR-14).
- The running binary is never overwritten in place; a restart is an explicit step.
- `journalctl -u matrixos` becomes the log destination, which is what FR-21 is written for.
- Cost: the CI workflow needs a release job, and the device needs a one-time systemd setup.
  Both are small and only need doing once.
- Running as root remains a real exposure. Acceptable for a LAN device with no inbound
  services; it must be revisited when the upload app opens an HTTP port (v0.6).
- The PAT on the device has read access to Actions for the whole repository. Prefer a
  fine-grained token, scoped to this repository only, and note its expiry somewhere.

## Alternatives considered

- **Keep only the artifact pull** — rejected: no versioning, no rollback, PR builds can leak
  onto the device.
- **Push-based deploy (rsync/ssh from the dev machine or CI)** — rejected: requires an
  inbound path to the Pi or an exposed key in CI. The pull model needs neither.
- **`.deb` package with `apt`** — rejected: proper dependency handling and rollback via the
  package manager, but packaging overhead and a repository to host for a single-device
  project.
- **Docker on the device** — rejected: the container would need `--privileged` and host
  device access for `/dev/mem` anyway, so the isolation benefit largely evaporates, and it
  costs memory on a 512 MB machine.
- **Build on the device from source** — rejected as the primary path (slow, needs a
  toolchain and the submodule on the Pi), but see [ADR-0004](0004-network-app-runtime.md):
  it may return as a way to sidestep cross-compiling TLS.

## Addendum, 2026-07-26 — the appliance path

This record was written for the maintainer's own device. The decision to build a small number
of units for other people ([ADR-0007](0007-appliance-provisioning.md)) constrains it in three
ways, none of which changes what is decided above for the maintainer's own hardware:

1. **Updates for shipped devices are explicitly out of scope for now.** A defect means the
   maintainer reflashes the card. At single-digit quantities among friends this is acceptable,
   and it is why the release channel above stays unbuilt for the time being.
2. **The PAT-based development channel cannot ship.** It requires a GitHub token on the
   device, which cannot be handed to a user. Any future update mechanism for shipped units
   needs unauthenticated public releases plus signature verification — a different design, not
   a tweak of this one.
3. **The overlay filesystem from [ADR-0008](0008-power-loss-resilience.md) makes `/opt`
   read-only.** The symlink-switch install above therefore requires disabling the overlay and
   rebooting. That is a coherent consequence of preferring a power-loss-proof appliance over a
   conveniently updatable one, not an oversight.

When updates become necessary — for a larger batch, or a bug that cannot be reached
physically — this ADR and ADR-0008 must be reconciled and this record superseded. ADR-0007
names balenaOS as the first alternative to re-examine at that point, because it supplies OTA
updates ready-made.
