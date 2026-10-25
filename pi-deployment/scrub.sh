#!/bin/bash
#
# Removes everything from a provisioned build device that must not be cloned into
# somebody's living room (NFR-22), turns the overlay back on and powers off.
#
#   sudo systemd-run --unit=matrixos-scrub --setenv=SUDO_USER=$USER ~/MatrixOS/pi-deployment/scrub.sh
#   tail -f /run/matrixos-scrub.log
#
# Run with the overlay disabled (image-build.md §2). SUDO_USER is for the final
# provision.sh run. Detached through systemd, because deleting the WiFi profiles
# drops the SSH session it was started from; `sudo setsid -f` did not even get it
# started. The log lives in /run, so it does not ship. A panel that goes dark
# means it finished; the proof is the test flash (image-build.md §4).

set -euo pipefail

STATE_DIR="/var/lib/matrixos"
AP_PROFILE="matrixos-setup"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

exec >/run/matrixos-scrub.log 2>&1

step() { echo "▶ $*"; }
die() {
  echo "❌ $*"
  exit 1
}

[ "$(id -u)" -eq 0 ] || die "run this with sudo"
[ -n "${SUDO_USER:-}" ] || die "run this with sudo from the maintainer account"
[ "$(findmnt -no FSTYPE /)" != overlay ] || die "the overlay is on — the deletions would only reach RAM"

step "cloud-init state: a copy of the seed, account and WiFi password included"
rm -rf /var/lib/cloud

step "deploy token"
rm -f /home/*/MatrixOS/pi-deployment/.env

step "device state"
rm -f "$STATE_DIR"/*.conf "$STATE_DIR"/network/*.nmconnection

step "SSH: host keys in /etc/ssh, everything in ~/.ssh except authorized_keys"
rm -f /etc/ssh/ssh_host_*
find /home/*/.ssh /root/.ssh -type f ! -name authorized_keys -delete 2>/dev/null || true
rm -f /home/*/.git-credentials /root/.git-credentials

step "history and logs"
rm -f /home/*/.bash_history /root/.bash_history
journalctl --rotate && journalctl --vacuum-time=1s
rm -rf /var/log/*.gz /var/log/*.1 /var/tmp/*

step "machine ID — empty makes systemd generate one on first boot"
truncate -s 0 /etc/machine-id

step "boot partition leftovers"
mount -o remount,rw /boot/firmware
rm -rf '/boot/firmware/System Volume Information'

# From here on the network is gone.
step "WiFi: every client profile, then whatever netplan still holds"
nmcli -t -f NAME,TYPE connection show |
  awk -F: -v ap="$AP_PROFILE" '$2 == "802-11-wireless" && $1 != ap {print $1}' |
  while read -r profile; do nmcli connection delete "$profile"; done
rm -f /etc/netplan/*.yaml /etc/wpa_supplicant/wpa_supplicant.conf

step "overlay back on"
"$SCRIPT_DIR/provision.sh"

# provision.sh makes sure a host key exists, so this has to come after it.
step "this unit's host key"
rm -f "$STATE_DIR"/ssh/ssh_host_*

step "done — powering off"
sync
poweroff
