# ADR-0008: Power-loss resilience and flash longevity

- **Status:** Accepted
- **Date:** 2026-07-26

## Context

The device is an always-on desk object handed to non-technical users
([ADR-0007](0007-appliance-provisioning.md)). They will switch it off by pulling the plug —
that is the natural way to turn off a lamp, and expecting anything else is unrealistic.

SD card corruption from interrupted writes is the most common way Raspberry Pi appliances
die. Two things make it worse here: there is no update channel, so a broken card means the
maintainer physically retrieves the device; and continuous card writes shorten the card's
life independently of any power failure.

Working against that, the Pi Zero 2 W has 512 MB of RAM, so any strategy that moves writes
into memory spends a scarce resource.

## Decision

Make pulling the plug a **supported way to switch the device off**, through five measures:

**1. Read-only root filesystem** using the overlay option built into `raspi-config`
(Performance Options → Overlay File System), plus a read-only boot partition. The operating
system then never writes to the card.

**2. A separate writable location for device state** — WiFi credentials, OAuth tokens, app
settings, high scores. Everything the device needs to remember lives there and nowhere else.

**3. Atomic state writes.** Write to a temporary file, `fsync`, then `rename` onto the
target. `rename` is atomic on ext4, so an interrupted write leaves either the previous value
or the new one, never a truncated file. This is the measure that protects what the read-only
root cannot: our own data.

**4. Logs in RAM only** — `journald` with `Storage=volatile` and `RuntimeMaxUse=16M`.
Explicitly **not** `Storage=none`: volatile keeps `journalctl` usable for the current boot,
which is what makes a remote "it doesn't work" report diagnosable. Both settings are equally
kind to the card.

**5. No swap** (`dphys-swapfile` disabled and the file removed) and `noatime` on mounts.
A swap file on the card is a larger wear source than logs.

Deliberately **not** adopted: the hardware watchdog. If the system hangs, pulling the power
is an acceptable recovery for this class of device and this audience. The systemd
`Restart=always` policy from NFR-7 stays — it addresses a different failure (the MatrixOS
process dying while the system runs fine) and costs one line.

Recommended alongside, though not a software decision: a high-endurance SD card.

## Consequences

- Pulling the plug at any moment is safe. At worst the most recent state write is lost, and
  the store is never left corrupt.
- In normal operation nothing is written to the card at all. Only explicit state writes
  touch persistent storage.
- **RAM becomes the constrained resource instead of card wear.** With no swap and an overlay
  filesystem in memory, anything that would previously have worn out the card now consumes
  RAM, and an unbounded writer causes an out-of-memory kill rather than slow degradation.
  `RuntimeMaxUse=16M` is therefore a memory bound, not just a wear bound, and the 64 MB
  budget in NFR-4 stops being a rough target and starts being binding.
- Logs vanish on reboot. A crash that happened before the last restart cannot be
  investigated. The way to temporarily enable persistent logging must be documented for
  debugging sessions — this is a real trade accepted in favour of card longevity.
- The overlay filesystem makes `/opt` read-only, which **collides with the symlink-switch
  deployment in [ADR-0005](0005-deployment-model.md)**: installing a build requires
  disabling the overlay and rebooting. That collision is consistent rather than accidental —
  it is the price of choosing a robust appliance over a conveniently updatable one, and it
  is coherent with updates being out of scope for now. If updates come back into scope, both
  ADRs have to be reconciled.
- Every device configuration step is now something `provision.sh` must set, which reinforces
  ADR-0007's rule that no manual configuration goes unrecorded.

## Alternatives considered

- **Rely on ext4 journalling and hope** — rejected: journalling protects filesystem
  metadata, not file contents, and it is the observed failure mode of Pi appliances in the
  field.
- **Hand-rolled read-only root** (mount `/` read-only, tmpfs for `/var/log`, `/tmp`, `/run`)
  — more control and less RAM than a full overlay, but more to get right and to maintain.
  The `raspi-config` overlay is a few clicks and can be revisited if RAM pressure demands
  it.
- **`Storage=none` for logs** — rejected: identical wear benefit, but it makes the device
  undiagnosable. Bad trade for a device the maintainer cannot easily reach.
- **Keep swap for headroom** — rejected: it is the single largest write source on the card,
  and the memory budget in NFR-4 exists precisely so swap is unnecessary.
- **Hardware watchdog with automatic reboot** — rejected by the owner: pulling the power is
  an acceptable recovery path at this scale, and an appliance that reboots itself
  unpredictably is its own kind of confusing.
- **Encrypting the state partition** — rejected: without a TPM the key has to live on the
  same card, so it protects against nothing while complicating recovery. The honest position
  is that a removable card holds the user's tokens in the clear, with restrictive file
  permissions and a documented factory reset (ADR-0007).
