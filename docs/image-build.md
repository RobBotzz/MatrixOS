# Building the shipped image

How a finished, flashable `.img` is made from a provisioned device
([ADR-0007](adr/0007-appliance-provisioning.md), FR-31). This is the `dd` + PiShrink route;
pi-gen stays deferred, and the trigger for revisiting that is in the ADR.

The order below is not decorative. **The scrub happens before the image is taken**, and the
build device is never joined to the maintainer's own WiFi — which removes the largest item of
NFR-22 structurally instead of by checklist.

---

## 1. Build the device

Flash Raspberry Pi OS Lite 64-bit (Trixie) with Raspberry Pi Imager, user name **`admin`** —
generic because it ships in every unit, and the only account sshd will accept (ADR-0016). Then,
on the development machine, with the freshly flashed card in a reader:

```bash
sudo pi-deployment/prepare-card.sh /dev/sdX
```

This shrinks the root filesystem, creates the 256 MB `matrixos-state` partition, and disables
the first-boot expansion that would otherwise swallow it. It must happen **before the card is
booted for the first time**: an ext4 filesystem can only be shrunk while unmounted, and the Pi
expands the root partition across the whole card the first time it starts.

The resulting layout is fixed and small, and the remainder of the card is left unpartitioned on
purpose — the script's header gives the three reasons, and it prints how much that is:

|                            |                                                      |
| -------------------------- | ---------------------------------------------------- |
| 1 boot (FAT)               | ~512 MB, read-only on a finished device              |
| 2 root (ext4)              | used + 1 GB of headroom, read-only under the overlay |
| 3`matrixos-state` (ext4) | 256 MB, the only thing that changes                  |
| —                         | the rest: unpartitioned reserve                      |

**This is what keeps the image small.** `dd` copies the whole card, but PiShrink cuts the file
at the end of the last partition, so the artifact is around 4 GB regardless of whether it was
built on a 32 GB card or a 128 GB one — and it flashes onto any card at least that large.
`MATRIXOS_STATE_MB=2048` changes the split if a future app ever needs the room.

Then boot the card in the Pi, copy your public key and the repository's `pi-deployment/` across,
and provision:

```bash
# development machine — provision.sh refuses to run without a key, because it turns
# password login off (ADR-0016)
ssh-copy-id admin@<hostname>.local

# on the device
sudo pi-deployment/provision.sh
```

That is the entire device configuration (NFR-21). If something about a shipped unit is not in
that script, it is not on the unit.

Finally, put a build in place — **before the reboot** `provision.sh` asks for, while the root is
still writable:

```bash
sudo MATRIXOS_DEST=/opt/matrixos ~/MatrixOS/pi-deployment/deploy.sh
sudo reboot
```

On a device that is already read-only, the overlay has to come off first and go back on
afterwards. `raspi-config nonint disable_overlayfs` does **not** work for this: `provision.sh`
sets `overlayroot=tmpfs:recurse=0` (see ADR-0008), which raspi-config does not recognise.

```bash
# off
sudo mount -o remount,rw /boot/firmware
sudo sed -i 's/overlayroot=tmpfs[^ ]* //' /boot/firmware/cmdline.txt && sudo reboot
# on again — provision.sh is idempotent and only redoes step 9
sudo ~/MatrixOS/pi-deployment/provision.sh && sudo reboot
```

Confirm the panel comes up, the setup screen appears, and a phone can complete the flow.

---

## 2. Scrub (NFR-22)

Everything below is a thing that must not be cloned into somebody's living room. Run it on the
device, with the overlay **disabled** so the deletions actually reach the card.

```bash
sudo mount -o remount,rw /boot/firmware
sudo sed -i 's/overlayroot=tmpfs[^ ]* //' /boot/firmware/cmdline.txt && sudo reboot
```

```bash
# Otherwise this very session writes its history back when it ends.
unset HISTFILE

# WiFi: every client profile the build device ever joined. The setup profile stays —
# it is the way in for the recipient.
sudo nmcli -t -f NAME,TYPE connection show \
  | awk -F: '$2 == "802-11-wireless" && $1 != "matrixos-setup" {print $1}' \
  | xargs -r -n1 sudo nmcli connection delete

# Profiles made by hand or by Raspberry Pi Imager live in netplan, not in
# NetworkManager's own store, and carry their passwords in cleartext.
sudo rm -f /etc/netplan/*.yaml

# The deploy token. It would otherwise ship in every unit.
rm -f ~/MatrixOS/pi-deployment/.env

# Device state: high scores, settings, the last active app, the remembered network.
# Through sh -c: the directory is daemon's and mode 0700, so a glob expanded by your
# own shell matches nothing and rm -f says nothing.
sudo sh -c 'rm -f /var/lib/matrixos/*.conf /var/lib/matrixos/network/*.nmconnection'

# SSH host keys — otherwise every shipped unit shares one identity. The ones in
# /etc/ssh are unused since ADR-0016, but would still ship.
sudo rm -f /etc/ssh/ssh_host_*

# Everything in ~/.ssh except the maintainer's public key: private keys made on the
# device (a GitHub deploy key, say) and known_hosts.
sudo find /home/*/.ssh /root/.ssh -type f ! -name authorized_keys -delete 2>/dev/null
sudo rm -f /home/*/.git-credentials /root/.git-credentials

# Shell history and logs.
sudo rm -f /home/*/.bash_history /root/.bash_history
sudo journalctl --rotate && sudo journalctl --vacuum-time=1s
sudo rm -rf /var/log/*.gz /var/log/*.1 /var/tmp/*

# The machine ID. An empty file makes systemd generate a fresh one on first boot.
sudo truncate -s 0 /etc/machine-id

# Left by Windows when the card was in a Windows reader.
sudo rm -rf '/boot/firmware/System Volume Information'
```

Two things are not on this list because `provision.sh` already took care of them (ADR-0016): the
cloud-init seed the Imager writes onto the boot partition — the maintainer's account and the WiFi
password in cleartext — and the maintainer's password hash. Check what is left before going
further:

```bash
sudo nmcli -f NAME,FILENAME connection show   # only matrixos-setup and lo
sudo ls -laR /var/lib/matrixos
ls -la /etc/ssh /etc/netplan ~/.ssh ~/MatrixOS/pi-deployment
ls /boot/firmware/{user-data,network-config,meta-data}   # all three: No such file
sudo getent shadow admin | cut -d: -f2                    # *
```

Then re-enable the overlay, delete the host key **that this run of `provision.sh` just
generated**, and shut down:

```bash
sudo ~/MatrixOS/pi-deployment/provision.sh
sudo sh -c 'rm -f /var/lib/matrixos/ssh/ssh_host_*'
sudo shutdown -h now
```

The order of the last three lines matters. `provision.sh` makes sure the build device has a host
key, so deleting it before the script runs would only have it regenerated — and then cloned onto
every unit.

> **SSH host keys on a shipped unit.** Each unit generates its own on its first boot, onto the
> state partition, so the fingerprint stays the same across reboots and a factory reset. That
> one key is the only thing the device writes to the card without being asked to remember
> something (NFR-20).

---

## 3. Take the image

On the development machine, with the card in a reader:

```bash
sudo dd if=/dev/sdX of=matrixos-0.4.0.img bs=4M status=progress conv=fsync
sudo pishrink.sh -Z matrixos-0.4.0.img          # shrinks and gzips
```

[PiShrink](https://github.com/Drewsif/PiShrink) shrinks the root partition to its contents and
adds a first-boot expansion hook. **That hook has to go**, or the first boot of every shipped
unit will expand the root filesystem over the state partition:

```bash
# Verify before shipping, on a test flash of the image:
grep -c 'init=/usr/lib/raspberrypi-sys-mods/firstboot\|resize2fs_once' /boot/firmware/cmdline.txt
lsblk -o NAME,SIZE,LABEL          # matrixos-state must still be there after two boots
```

If PiShrink has added an expansion hook, remove it from `cmdline.txt` in the image before
distributing it, exactly as `prepare-card.sh` does for a fresh card.

---

## 4. Verify the image before shipping it

Flash the shrunk image onto a second card and check, in this order:

1. It boots and the panel shows the setup screen with a **different** four-character suffix
   than the build device (FR-32).
2. A phone finds `MatrixOS-xxxx`, lands on the setup page without typing an address, and the
   join succeeds (FR-33, FR-34).
3. `matrixos-xxxx.local` serves the configuration page and shows the version (FR-36, FR-41).
4. `nmcli connection show` lists no network the maintainer has ever joined (NFR-22).
   `ssh-keyscan -t ed25519 matrixos-xxxx.local` shows a different key than the build device
   did, and `ssh -o PubkeyAuthentication=no admin@matrixos-xxxx.local` is refused without a
   password prompt (ADR-0016).
5. Two reboots later, `lsblk` still shows `matrixos-state`, and the WiFi is still remembered.
6. Pulling the plug during operation, ten times, leaves a device that boots (NFR-19).

Steps 1 and 4 are the two that a checklist catches and a habit does not.
