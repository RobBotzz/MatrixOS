# Building the shipped image

How a finished, flashable `.img` is made from a provisioned device
([ADR-0007](adr/0007-appliance-provisioning.md), FR-31). This is the `dd` route;
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

**This is what keeps the image small.** The copy in §3 stops at the end of the last
partition, so the artifact is around 4 GB regardless of whether it was
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

Everything a unit must not carry into somebody's living room is removed by
`pi-deployment/scrub.sh`: cloud-init's copy of the Imager seed, the deploy token, device state,
host keys and everything in `~/.ssh` except `authorized_keys`, history, logs, the machine ID,
and every WiFi profile but the setup one. The script header lists the order and why.

It is a script rather than a checklist for a practical reason: **deleting the WiFi profiles
drops the SSH session** the scrub is typed into. So it runs detached, does everything that
does not need the network first, deletes the WiFi last, re-enables the overlay through
`provision.sh`, deletes the host key that run just made sure of, and powers off.

Two things are not in it because `provision.sh` already took care of them
([ADR-0016](adr/0016-maintainer-access-to-shipped-units.md)): the cloud-init seed on the boot
partition and the maintainer's password hash.

On the device, overlay off first, then a build of the commit the image is meant to carry:

```bash
sudo mount -o remount,rw /boot/firmware
sudo sed -i 's/overlayroot=tmpfs[^ ]* //' /boot/firmware/cmdline.txt && sudo reboot
# after the reboot
sudo MATRIXOS_DEST=/opt/matrixos ~/MatrixOS/pi-deployment/deploy.sh
```

Then:

```bash
unset HISTFILE
sudo systemd-run --unit=matrixos-scrub --setenv=SUDO_USER=$USER ~/MatrixOS/pi-deployment/scrub.sh
sleep 2; tail -f /run/matrixos-scrub.log
```

The log stops when the WiFi goes; the panel going dark a minute later is the end of it. If the
panel stays lit, the script stopped — the log is in `/run` and gone with the next boot, so take
the overlay off and run it again in the foreground (`sudo ~/MatrixOS/pi-deployment/scrub.sh`,
then `cat /run/matrixos-scrub.log`) with a keyboard and screen attached.

What is left is checked on the test flash of the image, not here (§4): that is the only place
that shows what would actually ship.

---

## 3. Take the image

On the development machine, with the card in a reader. On WSL the reader is passed through
with `usbipd attach --wsl --busid <busid>` from an administrator PowerShell; `wsl --mount` does
not take USB storage. `dd` stops at the end of the last partition, so it copies around 4 GB
instead of the whole card:

```bash
D=/dev/$(sudo lsblk -nro PKNAME,LABEL | awk '$2=="matrixos-state"{print $1}')
[ "$D" != "/dev/" ] && echo "card: $D" || echo "STOP: no card with a matrixos-state partition"
TOTAL=$(( ($(sudo sfdisk -l -o End $D | tail -1 | tr -d ' ') + 1) * 512 ))
sudo dd if=$D of=$HOME/matrixos-0.4.0.img bs=4M iflag=count_bytes count=$TOTAL status=progress conv=fsync
echo "expected: $TOTAL  got: $(stat -c %s $HOME/matrixos-0.4.0.img)"   # must match
sudo chown $USER: $HOME/matrixos-0.4.0.img
xz -T0 -v $HOME/matrixos-0.4.0.img     # -> .img.xz, flashable as it is; deletes the .img
```

**Compare the two sizes before compressing.** Over `usbipd` the first copy on 2026-10-25 broke
off at 3.7 of 4.4 GB with `Input/output error`, and `dd`'s exit status is easy to miss in the
scroll. The part already copied is good up to a whole 4 MB block, so the rest can be read on
its own rather than starting over:

```bash
HAVE=$(stat -c %s $HOME/matrixos-0.4.0.img)
sudo dd if=$D of=$HOME/matrixos-0.4.0.img bs=4M iflag=skip_bytes,count_bytes oflag=seek_bytes \
  skip=$HAVE seek=$HAVE count=$(( TOTAL - HAVE )) conv=notrunc,fsync status=progress
```

If the error comes back at the same offset, the card itself has a bad spot.

**No PiShrink.** It shrinks the *last* partition to its contents and adds a first-boot
expansion hook. On this layout the last partition is `matrixos-state`, not the root, so it would
shrink the state partition to almost nothing and then grow whatever its hook decides to grow.
The layout from `prepare-card.sh` is already compact; cutting the copy at the last partition is
all that PiShrink would have added. Raspberry Pi Imager flashes `.img.xz` directly.

Verify on a test flash that nothing expands on first boot:

```bash
grep -c 'init=/usr/lib/raspberrypi-sys-mods/firstboot\|resize2fs_once\|resize' /boot/firmware/cmdline.txt   # 0
lsblk -o NAME,SIZE,LABEL          # matrixos-state must still be there, 256M, after two boots
```

---

## 4. Verify the image before shipping it

Flash the image onto a second card and check, in this order:

1. It boots and the panel shows the setup screen with a **different** four-character suffix
   than the build device (FR-32). With only one Pi the suffix is the same, since it comes from
   the CPU serial; that half waits for criterion 2.
2. A phone finds `MatrixOS-xxxx`, lands on the setup page without typing an address, and the
   join succeeds (FR-33, FR-34).
3. `matrixos-xxxx.local` serves the configuration page and shows the version (FR-36, FR-41).
4. `nmcli connection show` lists no network the maintainer has ever joined, and nothing from the
   build is left (NFR-22):
   ```bash
   sudo ls -laR /var/lib/matrixos      # no *.conf, network/ only what the portal just made, ssh/ a new key
   ls /etc/netplan /var/lib/cloud ~/.ssh ~/MatrixOS/pi-deployment   # setup profile only, no cloud, authorized_keys only, no .env
   sudo journalctl --list-boots | head -3                          # this boot only
   ```
   `ssh-keyscan -t ed25519 matrixos-xxxx.local` shows a different key than the build device
   did, and `ssh -o PubkeyAuthentication=no admin@matrixos-xxxx.local` is refused without a
   password prompt (ADR-0016).
5. Two reboots later, `lsblk` still shows `matrixos-state`, and the WiFi is still remembered.
6. Pulling the plug during operation, ten times, leaves a device that boots (NFR-19).

Steps 1 and 4 are the two that a checklist catches and a habit does not.
