#!/bin/bash
#
# Makes room on a freshly flashed card for the writable state partition.
#
# Runs on the DEVELOPMENT MACHINE, with the card in a reader and NOT booted yet.
# That is not a convenience: shrinking an ext4 filesystem is only supported while
# it is unmounted, and once the Pi has booted once it has already expanded the
# root partition across the whole card.
#
#   sudo pi-deployment/prepare-card.sh /dev/sdX
#
# Afterwards: boot the card in the Pi and run provision.sh there.
#
# Why a separate partition at all: v0.4 makes the root filesystem read-only
# through an overlay, so everything written to it lives in RAM and is gone at the
# next reboot (ADR-0008). Device state — WiFi, settings, scores — has to sit
# somewhere the overlay does not cover (FR-39).
#
# WHAT HAPPENS TO THE REST OF THE CARD: nothing, and that is deliberate. The
# layout is fixed and small — around 512 MB of boot, the root filesystem plus
# 1 GB of headroom, and 256 MB of state — and whatever a larger card offers
# beyond that stays unpartitioned. Three reasons, in the order they matter:
#
#   - The golden image ends at the last partition, so it stays around 4 GB
#     instead of growing to the size of whichever card it was built on. It
#     flashes faster and fits any card at least that large (image-build.md).
#   - The root filesystem is read-only on a finished device and never grows, so
#     capacity there buys nothing at all.
#   - Blocks that are never written stay available to the card's own wear
#     levelling. On a device with no update channel that runs for years, spare
#     flash is worth more than mounted flash.
#
# The cost is worth weighing rather than discovering: raising the state size
# later means repartitioning, and for a device in someone else's flat that means
# fetching it back. 256 MB holds tens of thousands of 64x32 images, so the limit
# is real but a long way off. To choose a different one:
#
#   sudo MATRIXOS_STATE_MB=2048 pi-deployment/prepare-card.sh /dev/sdX

set -euo pipefail

STATE_LABEL="matrixos-state"

# 256 MB for a handful of key=value files is already absurd headroom; see the
# note above for why the rest of the card is left alone rather than added here.
STATE_SIZE_MB="${MATRIXOS_STATE_MB:-256}"

die() {
  echo "❌ $*" >&2
  exit 1
}

[ "$(id -u)" -eq 0 ] || die "run this with sudo — it repartitions a disk"
[ $# -eq 1 ] || die "usage: $0 /dev/sdX   (the whole card, not a partition)"

DISK="$1"
[ -b "$DISK" ] || die "$DISK is not a block device"

for tool in parted e2fsck resize2fs tune2fs mkfs.ext4 lsblk partprobe; do
  command -v "$tool" >/dev/null || die "'$tool' is not installed"
done

# Refuse anything that is mounted. Repartitioning a mounted disk is how a
# development machine loses its root filesystem.
if lsblk -no MOUNTPOINT "$DISK" | grep -q .; then
  die "$DISK has mounted partitions — unmount them first"
fi

echo "About to repartition $DISK:"
lsblk -o NAME,SIZE,FSTYPE,LABEL "$DISK"
echo
echo "The root filesystem will be shrunk and a ${STATE_SIZE_MB} MB partition"
echo "labelled '$STATE_LABEL' created after it. Everything else is left alone."
read -r -p "Type the device path again to confirm: " CONFIRM
[ "$CONFIRM" = "$DISK" ] || die "not confirmed"

# Partition naming differs between /dev/sdb2 and /dev/mmcblk0p2.
if [[ "$DISK" =~ [0-9]$ ]]; then
  ROOT_PART="${DISK}p2"
  STATE_PART="${DISK}p3"
else
  ROOT_PART="${DISK}2"
  STATE_PART="${DISK}3"
fi

[ -b "$ROOT_PART" ] || die "$ROOT_PART does not exist — is this a Raspberry Pi OS card?"

if [ -b "$STATE_PART" ]; then
  die "$STATE_PART already exists; nothing to do (or delete it first)"
fi

echo "🔍 Checking the root filesystem..."
e2fsck -fy "$ROOT_PART"

# Shrink the filesystem to its used size plus headroom, then the partition to
# match, then create the state partition in what is left. The order matters:
# a partition smaller than its filesystem is a destroyed filesystem.
echo "📏 Shrinking the root filesystem..."
resize2fs -M "$ROOT_PART"

BLOCK_SIZE=$(tune2fs -l "$ROOT_PART" | awk -F: '/Block size/ {print $2}' | tr -d ' ')
BLOCKS=$(tune2fs -l "$ROOT_PART" | awk -F: '/Block count/ {print $2}' | tr -d ' ')
USED_MB=$(( BLOCKS * BLOCK_SIZE / 1024 / 1024 ))

# Headroom for the packages provision.sh installs and for a future build.
ROOT_MB=$(( USED_MB + 1024 ))
resize2fs "$ROOT_PART" "${ROOT_MB}M"

ROOT_START_S=$(parted -ms "$DISK" unit s print | awk -F: '/^2:/ {print $2}' | tr -d 's')
ROOT_END_S=$(( ROOT_START_S + ROOT_MB * 1024 * 1024 / 512 ))

echo "📐 Resizing the root partition..."
parted -s "$DISK" unit s resizepart 2 "${ROOT_END_S}s"

STATE_START_S=$(( ROOT_END_S + 2048 ))
STATE_END_S=$(( STATE_START_S + STATE_SIZE_MB * 1024 * 1024 / 512 ))

echo "➕ Creating the state partition..."
parted -s "$DISK" unit s mkpart primary ext4 "${STATE_START_S}s" "${STATE_END_S}s"
partprobe "$DISK"
sleep 1

mkfs.ext4 -q -L "$STATE_LABEL" "$STATE_PART"

# The root filesystem must not be expanded again on first boot, or it would eat
# the partition we just created. Raspberry Pi OS does that from cmdline.txt.
BOOT_MOUNT=$(mktemp -d)
if [[ "$DISK" =~ [0-9]$ ]]; then BOOT_PART="${DISK}p1"; else BOOT_PART="${DISK}1"; fi

mount "$BOOT_PART" "$BOOT_MOUNT"
if grep -q 'init=/usr/lib/raspberrypi-sys-mods/firstboot' "$BOOT_MOUNT/cmdline.txt" 2>/dev/null; then
  echo "🚫 Disabling the first-boot root expansion..."
  sed -i 's| init=/usr/lib/raspberrypi-sys-mods/firstboot||' "$BOOT_MOUNT/cmdline.txt"
fi
umount "$BOOT_MOUNT"
rmdir "$BOOT_MOUNT"

echo
echo "✅ Done."
lsblk -o NAME,SIZE,FSTYPE,LABEL "$DISK"

# Stated rather than left to be discovered later with `lsblk`: on a large card
# this is most of it, and it is a decision (see the note at the top), not an
# oversight.
DISK_SECTORS=$(parted -ms "$DISK" unit s print | awk -F: 'NR == 2 {print $2}' | tr -d 's')
FREE_MB=$(((DISK_SECTORS - STATE_END_S) * 512 / 1024 / 1024))

echo
awk -v mb="$FREE_MB" -v state="$STATE_SIZE_MB" 'BEGIN {
  printf "State partition: %d MB.  Unpartitioned: %d MB (%.1f GB) — reserve, on purpose.\n",
         state, mb, mb / 1024
}'
echo "Set MATRIXOS_STATE_MB to change the split; the header of this script says why."
echo
echo "Next: boot the card in the Pi and run"
echo "    sudo pi-deployment/provision.sh"
