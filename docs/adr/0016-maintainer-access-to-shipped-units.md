# ADR-0016: Maintainer access to shipped units — SSH by key only, cloud-init off

- **Status:** Accepted
- **Date:** 2026-10-25

## Context

Provisioning the development Pi from a blank card on 2026-10-06 left three questions in the image
scrub ([image-build.md](../image-build.md)) that the checklist could not answer by itself:

- **The maintainer account.** Raspberry Pi Imager creates it on first boot with a password and
  sudo rights. Cloned as it is, every shipped unit carries the same password, and every unit
  sits on a network the maintainer does not control. Even with password login off, the hash in
  `/etc/shadow` ships with each card and can be attacked offline. NFR-22 forbids maintainer
  secrets in the image.
- **The SSH host keys.** The scrub deletes them so that no two units share an identity, but
  nothing in `provision.sh` made new ones. Under the read-only overlay
  ([ADR-0008](0008-power-loss-resilience.md)), keys generated into `/etc/ssh` would land in RAM
  and change on every boot.
- **cloud-init.** On Trixie the Imager writes `user-data`, `network-config` and `meta-data` onto
  the boot partition, and cloud-init applies them on first boot. They contain the account and
  the WiFi password in cleartext. What cloud-init does on a clone that has its state from the
  build device but not its seed files was not known — and whatever it does, it is a second
  source of device configuration next to `provision.sh`, which NFR-21 and
  [ADR-0007](0007-appliance-provisioning.md) do not allow. Checked on the development device on
  2026-10-25, it is worse than a second source: the Imager's configuration sets
  `preserve_hostname: false` and `manage_etc_hosts: true`, so cloud-init sets the hostname from
  its seed and rewrites `/etc/hosts` on every boot — and under the overlay every boot — in a race
  with `matrixos-identity` (FR-32). With `ssh_deletekeys: false` it would not reliably have made
  new host keys on a clone either.

Two facts limit what the answer has to cover. Shipped units get no updates and a defect means
reflashing ([ADR-0005](0005-deployment-model.md), addendum), so access is for diagnosis, not for
maintenance. And the journal is volatile (ADR-0008): a card taken out of a misbehaving unit
carries no logs, so seeing what a unit did requires reaching it while it runs.

## Decision

**The maintainer reaches a shipped unit over SSH with a key, and in no other way.** The account
keeps its public key and loses its password; sshd accepts only keys and only that account. Each
unit **generates its own host key on its first boot, onto the state partition**. cloud-init is
**disabled** and its seed removed once its first-boot work is done.

All of it is step 8 of `provision.sh`, so the development device runs the same configuration as a
shipped unit.

## Consequences

- `provision.sh` refuses to start without a key in the invoking user's `authorized_keys`. It
  turns password login off, and checking first is cheaper than recovering a locked-out device
  through a card reader.
- The password hash is replaced by `*`, and the account gets passwordless sudo, written before
  the hash goes — otherwise sudo would ask for a password that no longer exists.
- `sshd_config.d/10-matrixos.conf` must sort before anything cloud-init or the Imager may
  have left there — a `50-cloud-init.conf` with `PasswordAuthentication yes` is the usual one.
  sshd keeps the first value it reads.
- The host key lives at `/var/lib/matrixos/ssh`, root-owned. Its fingerprint stays stable across
  reboots and survives the factory reset, which forgets only WiFi and tokens. Generating it is
  the one write to the card a unit makes without being asked to remember something (NFR-20), and
  it happens once.
- The scrub has to delete that key **after** the final run of `provision.sh`, because the script
  makes sure one exists. [image-build.md](../image-build.md) carries the order.
- A leaked maintainer private key opens every shipped unit that is reachable. A key used only for
  shipped units limits that to the units themselves; that is a habit, not something the script
  enforces.
- No console login: with no password, a keyboard and a monitor do not get anyone in. Physical
  access to the card remains the last resort, as it was.
- Checked on the development device on 2026-10-25, before the first run: `sshd_config.d` is
  empty, so password login comes from sshd's default alone; `UsePAM yes`; `ssh.service` is
  enabled and `ssh.socket` is not. After the first run of step 8: key login works for an account
  whose password field is `*` — Debian's sshd with `UsePAM yes` does not treat it as locked — and
  a login without a key is refused with no password prompt.

## Alternatives considered

- **No login at all** — SSH off, the account locked. Smallest attack surface and the least code,
  and consistent with "a defect means reflashing". It lost because a unit could then only be
  diagnosed by reproducing the problem elsewhere: with a volatile journal, the card carries no
  record of what went wrong.
- **Keep the Imager's password** — rejected: one secret shared by every unit, on networks the
  maintainer does not control. This is exactly what NFR-22 rules out.
- **Host keys regenerated into `/etc/ssh` on every boot** — what the overlay would have produced
  without further work. Rejected because a fingerprint that changes on every restart trains the
  maintainer to ignore the warning that exists to catch an impostor.
- **Leave cloud-init running and only delete its seed** — rejected: its behaviour on a clone
  would have had to be investigated and then kept in mind on every OS release, for a component
  that has no job left after the first boot of the build device.
