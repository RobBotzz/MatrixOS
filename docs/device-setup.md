# Device setup

An exact record of every step applied to a Pi to make it run MatrixOS, and **why** each one is
needed. NFR-21 requires that a device can be rebuilt from a blank card with no undocumented
steps.

> **Since v0.4 the steps below are automated.** `pi-deployment/provision.sh` performs all of
> them, and [ADR-0007](adr/0007-appliance-provisioning.md) makes that script the specification
> of a device: if a setting is not in it, it does not exist on a shipped unit. This document is
> the reasoning behind the script, not a second copy of it — when the two disagree, the script
> is right and this file has a bug.
>
> Two scripts, because one of them cannot run on the device:
>
> | Script | Runs on | Does |
> | --- | --- | --- |
> | `prepare-card.sh` | the development machine, card in a reader, **before first boot** | shrinks the root filesystem, creates the `matrixos-state` partition |
> | `provision.sh` | the device | everything else |
>
> Turning a provisioned device into a shipped image is [image-build.md](image-build.md).

Status of the development device (`robinsmatrix`), 2026-07-29.

## 1. Operating system

| | |
| --- | --- |
| Image | Raspberry Pi OS Lite, 64-bit, **Trixie** (Debian 13) |
| Hostname | `robinsmatrix` |
| User | `robin` |
| glibc | 2.41 |

The glibc version matters: binaries cross-compiled on Ubuntu require `GLIBC_2.38`, which
Trixie satisfies and Bookworm (2.36) does not. A Bookworm device would need a Pi sysroot for
cross-compilation or a native build.

## 2. Matrix timing prerequisites

These three settings are what C-3 in [requirements.md](requirements.md) demands. Without the
first one the library **refuses to start** — it checks for the sound module and exits.

### Disable on-board sound

The library needs the PWM hardware that the on-board audio driver claims.

```bash
# /boot/firmware/config.txt  (note: not /boot/config.txt since Bookworm)
dtparam=audio=off
```

Plus the module blacklist, as the library's own README prescribes:

```bash
cat <<EOF | sudo tee /etc/modprobe.d/blacklist-rgb-matrix.conf
blacklist snd_bcm2835
EOF

sudo update-initramfs -u
```

Verify after reboot: `lsmod | grep snd_bcm2835` returns nothing.

### Isolate a CPU core

The library pins its refresh thread to core 3 and runs it at realtime priority. Isolating the
core keeps the scheduler from putting anything else there, which is what protects the
microsecond-level timing behind NFR-5.

Appended to the **single line** in `/boot/firmware/cmdline.txt`:

```
isolcpus=3
```

Verified:

```bash
cat /sys/devices/system/cpu/isolated   # → 3
nproc                                  # → 3  (cores available to a normal process)
nproc --all                            # → 4  (cores present)
```

`nproc` dropping to 3 is the confirmation, not a problem: it shows normal processes can no
longer be scheduled on the isolated core.

Once this is in place the library stops printing its `isolcpus=3` suggestion at startup.

## 3. Panel

| | |
| --- | --- |
| Panel | one 64x32 HUB75 |
| Wiring | direct, `hardware_mapping = "regular"` |
| Power | separate 5 V supply for the panel, common ground with the Pi |

Pin assignment for `regular` with a single chain (see the resolved Q-1):

| Signal | GPIO |
| --- | --- |
| Output enable | 18 |
| Clock | 17 |
| Strobe | 4 |
| Address A / B / C / D | 22 / 23 / 24 / 25 |
| R1 / G1 / B1 | 11 / 27 / 7 |
| R2 / G2 / B2 | 8 / 9 / 10 |

### Input wiring

Encoder: **KY-040 module**. Home button: a plain momentary switch.

| Wire | Header pin | GPIO | |
| --- | --- | --- | --- |
| KY-040 `GND` | 30 | — | |
| KY-040 `+` | 17 | 3V3 | **never 5 V** — see below |
| KY-040 `CLK` | 29 | 5 | encoder A |
| KY-040 `DT` | 31 | 6 | encoder B |
| KY-040 `SW` | 33 | 13 | encoder push button |
| Home switch | 35 | 19 | other leg to pin 34 (GND) |

All four input lines are claimed through `/dev/gpiochip0` with the kernel's internal
**pull-up** enabled, so every switch just shorts its line to ground. No external resistors.

Two things worth recording rather than rediscovering:

- **`+` must go to 3V3, not 5 V.** The module carries 10 kΩ pull-ups from `CLK` and `DT` to
  `+`, so a 5 V supply would leave both signal lines idling at 5 V and destroy the GPIO
  inputs. On 3V3 those pull-ups sit in parallel with the Pi's internal ones — roughly 8 kΩ
  together instead of 50 kΩ, which gives steeper edges and better noise margin on long wires.
  Leaving `+` unconnected also works; the internal pull-ups alone are enough.
- Most KY-040 boards populate no pull-up on `SW`. That does not matter here, because the
  internal pull-up covers it either way.

Nothing needs configuring on the device for any of this: the pins are compiled in as defaults
and the GPIO character device needs no kernel parameters or device tree overlay.

**Verified with `--test-pattern`:** border closed on all four sides, corner marker in the
top-left, colour bands in red / green / blue order from top to bottom. Geometry, orientation
and channel order are therefore all correct, and the panel needs no `--led-multiplexing` or
`--led-row-addr-type` override.

### Runtime flags

None. Panel geometry defaults are compiled in, and `--led-slowdown-gpio` is left at its
default of 1 (see the resolved Q-2).

```bash
sudo ~/MatrixOS/build/MatrixOS
```

## 4. Deployment channel

The device pulls finished builds from GitHub Actions. It does **not** hold a clone of the
repository — the repository is private, and the deploy script is self-contained, so the
device needs artifact access only.

```bash
sudo apt install -y jq unzip curl
mkdir -p ~/MatrixOS/pi-deployment
# then, from the development machine:
#   scp pi-deployment/deploy.sh robin@robinsmatrix.local:~/MatrixOS/pi-deployment/
```

`~/MatrixOS/pi-deployment/.env` holds `GITHUB_TOKEN`, mode `600`. The token is a fine-grained
PAT scoped to this repository with **Actions: Read-only** — nothing more. Deliberately no
`Contents` permission, which is why the repository cannot be (and need not be) cloned here.

Deploy: `~/MatrixOS/pi-deployment/deploy.sh`. The binary lands in `~/MatrixOS/build/MatrixOS`.

Artifacts expire after 90 days; after that a fresh CI run is required and the script says so.

## 5. Autostart

The unit lives in the repository at `pi-deployment/matrixos.service` so it is versioned
alongside the code.

```bash
scp pi-deployment/matrixos.service robin@robinsmatrix.local:~/MatrixOS/pi-deployment/
# on the device:
sudo cp ~/MatrixOS/pi-deployment/matrixos.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now matrixos
systemctl status matrixos
```

Key settings and why: `Restart=always` with `StartLimitIntervalSec=0` so the service never
gives up (C-10 — a device in someone's living room has nobody to run `systemctl` on it), no
network ordering because no app needs the network yet, and no `CPUAffinity=` or
`PrivateDevices=` because both would break the library — the first by preventing the refresh
thread from pinning itself to the isolated core, the second by cutting off `/dev/mem`.

### Consequence for manual testing

The service now holds the panel. Two processes driving the same GPIOs produce garbage, so
before running a build by hand:

```bash
sudo systemctl stop matrixos
sudo ~/MatrixOS/build/MatrixOS
# afterwards
sudo systemctl start matrixos
```

And after every `deploy.sh`, the service keeps running the old binary until restarted:

```bash
~/MatrixOS/pi-deployment/deploy.sh && sudo systemctl restart matrixos
```

Logs: `journalctl -u matrixos -f`. Remember that ADR-0008 makes the journal volatile, so
these logs are gone after a reboot.

## 6. State directory

From v0.3 the device remembers things — the last active app, the brightness, high scores. All of
it lives in one writable directory ([ADR-0011](adr/0011-state-store-format.md), FR-39):

```bash
sudo install -d -o daemon -g daemon -m 0700 /var/lib/matrixos
```

**The owner is `daemon`, not `root`, and that is the whole point of writing this down.** MatrixOS
starts as root because the matrix library needs `/dev/mem`, and the library then drops privileges
to `daemon` as soon as the panel is initialised (see the resolved Q-1). Every state write happens
after that moment, so a directory owned by root would leave the device running perfectly and
silently forgetting everything.

Mode `0700` rather than `0755` because WiFi credentials and OAuth tokens move in here later
(FR-24), and it costs nothing to get right now.

If the directory is missing or not writable, MatrixOS logs it once at startup and runs without
persistence rather than refusing to start. To check:

```bash
journalctl -u matrixos | grep -i 'state'
ls -la /var/lib/matrixos          # cat any *.conf to see what the device remembers
```

`MATRIXOS_STATE_DIR` overrides the location, which is how a test run stays out of the real
device's state.

### On its own partition, from v0.4

The overlay filesystem makes everything written to `/` live in RAM and vanish at the next
reboot ([ADR-0008](adr/0008-power-loss-resilience.md)). State therefore needs a partition the
overlay does not cover — created by `prepare-card.sh`, labelled `matrixos-state`, 256 MB by
default (`MATRIXOS_STATE_MB` changes it), and mounted here by `provision.sh`:

```
/dev/disk/by-label/matrixos-state  /var/lib/matrixos  ext4  defaults,noatime,nofail  0  2
/var/lib/matrixos/network  /etc/NetworkManager/system-connections  none  bind,nofail  0  0
```

**The second line is the one that is easy to miss.** NetworkManager keeps WiFi credentials in
`/etc/NetworkManager/system-connections`, which the overlay covers — so without the bind mount
a device would forget its network on every reboot while appearing to remember everything else.
That directory stays `root:root 0700`; only MatrixOS's own `*.conf` files belong to `daemon`.

## 7. Network

Two WiFi profiles for two locations. Raspberry Pi OS Trixie configures the network through
**netplan** with NetworkManager as the renderer, so `nmcli` writes into
`/etc/netplan/90-NM-<uuid>.yaml` rather than into a separate NetworkManager store.

```bash
read -rsp 'WLAN password: ' PSK; echo
sudo nmcli connection add type wifi con-name 'NAME' ifname wlan0 \
  ssid 'EXACT_SSID' wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PSK"
unset PSK
```

Both profiles have `autoconnect yes`, so the device joins whichever network is in range.

`avahi-daemon` is active by default in this image, so the device answers to
`robinsmatrix.local` and can be reached at the second location without knowing its IP. That is
also the mechanism FR-36 will use for the configuration page.

> An SSID containing `!` must be single-quoted — inside double quotes bash performs history
> expansion and the command fails with `event not found`.

For an appliance image this directory is exactly what NFR-22 requires to be scrubbed: the
profiles contain the WiFi passwords in cleartext. [image-build.md](image-build.md) does that
before the image is taken.

### Provisioning a device that has never seen a network — from v0.4

A shipped unit joins nothing on its own. Three pieces of configuration make the setup flow work
([ADR-0013](adr/0013-wifi-provisioning-via-networkmanager.md)); all three are written by
`provision.sh`.

**The access-point profile**, created per unit by the identity service because its SSID carries
the CPU serial (FR-32):

```bash
nmcli connection add type wifi ifname wlan0 con-name matrixos-setup \
  autoconnect no ssid "MatrixOS-a3f1" \
  802-11-wireless.mode ap 802-11-wireless.band bg \
  ipv4.method shared ipv6.method ignore
```

`autoconnect no` is deliberate: MatrixOS decides when the device needs setting up and brings
the profile up itself. With one radio (C-8) an access point that came up on its own would take
the radio away from a client connection that was about to succeed. `ipv4.method shared` is what
puts the device on **10.42.0.1** and starts a DHCP server — the address the captive-portal
redirect points at.

**Captive DNS**, in `/etc/NetworkManager/dnsmasq-shared.d/matrixos-captive.conf`:

```
address=/#/10.42.0.1
```

Without it the phone's connectivity probe fails to resolve and the phone reports "no internet"
instead of opening the setup page. HTTP alone is not enough — the redirect only happens if the
name resolves to us first.

**A polkit rule** in `/etc/polkit-1/rules.d/50-matrixos-networkmanager.rules`, allowing the
`daemon` user the `org.freedesktop.NetworkManager.*` actions.

> **This is the one that will be forgotten and the one that is hardest to diagnose.** MatrixOS
> starts as root, and the matrix library drops it to `daemon` as soon as the panel is up (the
> resolved Q-1). Every `nmcli` call therefore runs as `daemon`, and NetworkManager asks polkit
> before it changes anything. Without the rule every scan comes back empty and every join
> fails, on a device whose panel, logs and web page all look perfectly healthy.

## 8. Status of this device

Sections 1 to 7 are in place and verified. From v0.4 the whole list is `provision.sh`, and the
development device is a device like any other — except that it is deliberately run with
`--no-readonly`, so the card stays writable for development.

What that costs, and it is worth stating plainly: on a device without the read-only root,
pulling the power can still corrupt the card. The protections are on shipped units, where they
belong.

Rebuilding this device from a blank card:

```bash
# development machine, card in a reader, before the first boot
sudo pi-deployment/prepare-card.sh /dev/sdX

# on the device
sudo pi-deployment/provision.sh --no-readonly --keep-hostname
```

`--keep-hostname` is the one worth knowing about for a device you reach by name every day.
Without it the unit renames itself to `matrixos-<suffix>` on the next boot (FR-32) and the name
you have in your shell history stops resolving. The script says so before it changes anything.
The setup access point is named from the serial either way — two units on one network must not
collide, and that is what FR-32 is actually for.
