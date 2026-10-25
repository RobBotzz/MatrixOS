#!/bin/bash
#
# Turns a fresh Raspberry Pi OS install into a MatrixOS device (NFR-21).
#
#   sudo pi-deployment/provision.sh                 # everything, including read-only root
#   sudo pi-deployment/provision.sh --no-readonly   # leave the card writable (dev device)
#   sudo pi-deployment/provision.sh --keep-hostname # do not rename this unit
#
# This script IS the specification of a device (ADR-0007): if a setting is not in
# here, it does not exist on a shipped unit. It is idempotent — running it twice
# changes nothing the second time — because that is what makes it safe to extend
# and rerun rather than to reason about.
#
# Prerequisite: the writable state partition, created by prepare-card.sh on the
# development machine before the card was ever booted. Without it the script
# still provisions a working device, but leaves the root filesystem writable and
# says so, because a read-only root without somewhere to put state is a device
# that forgets everything (ADR-0008).
#
# Second prerequisite: your SSH key in ~/.ssh/authorized_keys. Step 8 turns
# password login off (ADR-0016), so the script checks this before anything else.

set -euo pipefail

STATE_LABEL="matrixos-state"
STATE_DIR="/var/lib/matrixos"
NM_CONNECTIONS="/etc/NetworkManager/system-connections"
BINARY="${MATRIXOS_BIN:-/opt/matrixos/MatrixOS}"
AP_PROFILE="matrixos-setup"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WANT_READONLY=1
KEEP_HOSTNAME=0
NEEDS_REBOOT=0

for argument in "$@"; do
  case "$argument" in
    --no-readonly) WANT_READONLY=0 ;;
    --keep-hostname) KEEP_HOSTNAME=1 ;;
    -h | --help)
      sed -n '2,25p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      echo "❌ unknown option: $argument" >&2
      exit 1
      ;;
  esac
done

step() { echo -e "\n\033[1m▶ $*\033[0m"; }
note() { echo "  $*"; }
skip() { echo "  ✓ already done: $*"; }
warn() { echo -e "  \033[33m⚠ $*\033[0m"; }
die() {
  echo -e "\033[31m❌ $*\033[0m" >&2
  exit 1
}

[ "$(id -u)" -eq 0 ] || die "run this with sudo"
[ "$(uname -m)" = "aarch64" ] || die "this is not a 64-bit Raspberry Pi OS (C-1)"
[ -f /boot/firmware/config.txt ] || die "/boot/firmware/config.txt is missing — not Raspberry Pi OS Bookworm or newer"

MAINTAINER="${SUDO_USER:-}"
[ -n "$MAINTAINER" ] && [ "$MAINTAINER" != root ] ||
  die "run this with sudo from the maintainer account, not as root"
MAINTAINER_KEYS="$(getent passwd "$MAINTAINER" | cut -d: -f6)/.ssh/authorized_keys"
grep -qE '(ssh|ecdsa|sk)-[^ ]+ AAAA' "$MAINTAINER_KEYS" 2>/dev/null ||
  die "no SSH key in $MAINTAINER_KEYS — step 8 turns password login off. From your machine: ssh-copy-id $MAINTAINER@$(hostname).local"

SERIAL_SUFFIX=$(awk -F': ' '/^Serial/ {print tolower(substr($2, length($2) - 3))}' /proc/cpuinfo)
[ -n "$SERIAL_SUFFIX" ] || SERIAL_SUFFIX=0000
EXPECTED_HOSTNAME="matrixos-$SERIAL_SUFFIX"

# Said before anything is changed, because this is the one step here that can cut
# the connection you are running the script over — and because aborting is free
# right now and expensive in three minutes.
if [ "$KEEP_HOSTNAME" -eq 0 ] && [ "$(hostname)" != "$EXPECTED_HOSTNAME" ]; then
  echo
  warn "This unit will be renamed:  $(hostname)  ->  $EXPECTED_HOSTNAME"
  echo
  echo "  Every unit is flashed from the same image, so the name is derived from the"
  echo "  CPU serial instead of being baked in (FR-32). Two consequences right now:"
  echo
  echo "    - An SSH session opened as '$(hostname).local' will not reconnect under"
  echo "      that name. Afterwards, use:  ssh \$USER@$EXPECTED_HOSTNAME.local"
  echo "    - The setup access point will be called MatrixOS-$SERIAL_SUFFIX."
  echo
  echo "  Rerun with --keep-hostname to leave the name alone; everything else is"
  echo "  unaffected by that choice."
  echo
fi

# =============================================================================
step "1. Packages"
# =============================================================================
# Everything the device needs at runtime, plus what deploy.sh needs to fetch a
# build. Deliberately short: an appliance installs nothing it does not use.

PACKAGES=(network-manager avahi-daemon jq unzip curl)
[ "$WANT_READONLY" -eq 1 ] && PACKAGES+=(overlayroot)
MISSING=()
for package in "${PACKAGES[@]}"; do
  dpkg -s "$package" >/dev/null 2>&1 || MISSING+=("$package")
done

if [ ${#MISSING[@]} -eq 0 ]; then
  skip "${PACKAGES[*]}"
else
  note "installing: ${MISSING[*]}"
  # A fresh image runs its own apt jobs during the first minutes; wait for them.
  APT_WAIT=(-o DPkg::Lock::Timeout=600)
  apt-get "${APT_WAIT[@]}" update -qq
  DEBIAN_FRONTEND=noninteractive apt-get "${APT_WAIT[@]}" install -y -qq "${MISSING[@]}"
fi

# =============================================================================
step "2. Panel timing prerequisites (C-3)"
# =============================================================================
# Without the first of these the matrix library refuses to start: it checks for
# the on-board sound module and exits.

if grep -q '^dtparam=audio=off' /boot/firmware/config.txt; then
  skip "on-board audio disabled"
else
  note "disabling on-board audio"
  sed -i 's/^dtparam=audio=on/dtparam=audio=off/' /boot/firmware/config.txt
  grep -q '^dtparam=audio=off' /boot/firmware/config.txt ||
    echo 'dtparam=audio=off' >>/boot/firmware/config.txt
  NEEDS_REBOOT=1
fi

if [ -f /etc/modprobe.d/blacklist-rgb-matrix.conf ]; then
  skip "snd_bcm2835 blacklisted"
else
  note "blacklisting snd_bcm2835"
  echo 'blacklist snd_bcm2835' >/etc/modprobe.d/blacklist-rgb-matrix.conf
  update-initramfs -u
  NEEDS_REBOOT=1
fi

# The library pins its refresh thread to core 3 at realtime priority; isolating
# the core is what keeps the scheduler off it (NFR-5).
if grep -q 'isolcpus=3' /boot/firmware/cmdline.txt; then
  skip "core 3 isolated"
else
  note "isolating CPU core 3"
  sed -i '1 s/$/ isolcpus=3/' /boot/firmware/cmdline.txt
  NEEDS_REBOOT=1
fi

# =============================================================================
step "3. Writable state (FR-39, ADR-0008)"
# =============================================================================
# One writable location for everything the device remembers, on its own
# partition so the read-only root in step 9 cannot take it away.

STATE_DEVICE="/dev/disk/by-label/$STATE_LABEL"
HAVE_STATE_PARTITION=0

if [ -e "$STATE_DEVICE" ]; then
  HAVE_STATE_PARTITION=1
  STATE_UUID=$(blkid -s UUID -o value "$(readlink -f "$STATE_DEVICE")")

  if grep -q "$STATE_DIR " /etc/fstab; then
    skip "$STATE_DIR is in fstab"
  else
    note "mounting the state partition at $STATE_DIR"
    mkdir -p "$STATE_DIR"
    printf 'UUID=%s  %s  ext4  defaults,noatime,nofail  0  2\n' "$STATE_UUID" "$STATE_DIR" \
      >>/etc/fstab
    systemctl daemon-reload
    mount "$STATE_DIR" || warn "could not mount $STATE_DIR — check /etc/fstab"
  fi
else
  warn "no partition labelled '$STATE_LABEL' — run prepare-card.sh on the card first"
  warn "the device will work, but step 9 (read-only root) will be skipped"
  mkdir -p "$STATE_DIR"
fi

# Owned by daemon, not root: the matrix library drops privileges once the panel
# is up, and every state write happens after that (see the resolved Q-1). Mode
# 0700 because WiFi credentials and OAuth tokens live here (FR-24).
install -d -o daemon -g daemon -m 0700 "$STATE_DIR"

# NetworkManager's stored connections have to survive a reboot too, and with an
# overlay root they would not — /etc lives in RAM. This is what makes "the WiFi
# password is remembered" true on a read-only device.
if [ "$HAVE_STATE_PARTITION" -eq 1 ]; then
  install -d -o root -g root -m 0700 "$STATE_DIR/network"

  if grep -q "$NM_CONNECTIONS" /etc/fstab; then
    skip "NetworkManager connections are on the state partition"
  else
    note "moving NetworkManager connections onto the state partition"
    if [ -d "$NM_CONNECTIONS" ]; then
      cp -a "$NM_CONNECTIONS/." "$STATE_DIR/network/" 2>/dev/null || true
    fi
    mkdir -p "$NM_CONNECTIONS"
    printf '%s  %s  none  bind,nofail  0  0\n' "$STATE_DIR/network" "$NM_CONNECTIONS" \
      >>/etc/fstab
    systemctl daemon-reload
    mount "$NM_CONNECTIONS" || warn "could not bind-mount $NM_CONNECTIONS"
  fi
fi

# =============================================================================
step "4. The service (NFR-7)"
# =============================================================================

install -d -m 0755 "$(dirname "$BINARY")"

if [ -f /etc/systemd/system/matrixos.service ] &&
  cmp -s "$SCRIPT_DIR/matrixos.service" /etc/systemd/system/matrixos.service; then
  skip "matrixos.service is current"
else
  note "installing matrixos.service"
  cp "$SCRIPT_DIR/matrixos.service" /etc/systemd/system/matrixos.service
  systemctl daemon-reload
fi

# The unit ships with the development path in it. A drop-in keeps the versioned
# file honest and the installed path correct at the same time.
install -d -m 0755 /etc/systemd/system/matrixos.service.d
cat >/etc/systemd/system/matrixos.service.d/10-path.conf <<EOF
# Written by provision.sh — the binary location on this device.
[Service]
ExecStart=
ExecStart=$BINARY
EOF
systemctl daemon-reload
systemctl enable matrixos >/dev/null

if [ ! -x "$BINARY" ]; then
  warn "$BINARY does not exist yet — deploy a build before the panel shows anything"
fi

# =============================================================================
step "5. WiFi provisioning (FR-32 to FR-34, ADR-0013)"
# =============================================================================

# MatrixOS runs as 'daemon' after the privilege drop, and nmcli asks polkit
# before it changes anything. Without this rule every scan and every join is
# denied, and the device looks broken in a way no log line explains.
POLKIT_RULE=/etc/polkit-1/rules.d/50-matrixos-networkmanager.rules
if [ -f "$POLKIT_RULE" ]; then
  skip "polkit rule for the daemon user"
else
  note "allowing the daemon user to drive NetworkManager"
  cat >"$POLKIT_RULE" <<'EOF'
// MatrixOS runs as 'daemon' — the matrix library drops privileges from root
// once the panel is initialised — and provisions WiFi through nmcli.
polkit.addRule(function (action, subject) {
    if (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 &&
        subject.user === "daemon") {
        return polkit.Result.YES;
    }
});
EOF
fi

# The captive portal needs DNS as well as HTTP: every name the phone looks up has
# to resolve to us, or "open any page" lands on a connection error instead of the
# setup page. NetworkManager starts dnsmasq for shared mode and reads this.
CAPTIVE_DNS=/etc/NetworkManager/dnsmasq-shared.d/matrixos-captive.conf
if [ -f "$CAPTIVE_DNS" ]; then
  skip "captive DNS"
else
  note "answering every name with the device while in setup mode"
  install -d -m 0755 /etc/NetworkManager/dnsmasq-shared.d
  cat >"$CAPTIVE_DNS" <<'EOF'
# Everything resolves to the device itself, which is what turns "open any page"
# into the setup portal (ADR-0013). Only in effect while the shared-mode access
# point is up.
address=/#/10.42.0.1
EOF
fi

# =============================================================================
step "6. Identity on first boot (FR-32)"
# =============================================================================
# Every unit is flashed from the same image, so the name cannot be baked in. It
# is derived from the CPU serial — the same four characters MatrixOS shows on
# the panel — and re-derived on every boot, which is what makes it work on a
# read-only root where nothing written to /etc survives.

if [ "$KEEP_HOSTNAME" -eq 1 ]; then
  touch /etc/matrixos-keep-hostname
  note "keeping the hostname '$(hostname)' on request"
else
  rm -f /etc/matrixos-keep-hostname
fi

install -d -m 0755 /usr/local/sbin
cat >/usr/local/sbin/matrixos-identity <<EOF
#!/bin/bash
# Written by provision.sh. Derives this unit's name from the CPU serial.
set -euo pipefail

SUFFIX=\$(awk -F': ' '/^Serial/ {print tolower(substr(\$2, length(\$2) - 3))}' /proc/cpuinfo)
[ -n "\$SUFFIX" ] || SUFFIX=0000

HOSTNAME="matrixos-\$SUFFIX"
AP_SSID="MatrixOS-\$SUFFIX"

# provision.sh --keep-hostname leaves this marker. The access point is still
# named from the serial: two units on one network must not collide (FR-32).
if [ ! -e /etc/matrixos-keep-hostname ] && [ "\$(hostname)" != "\$HOSTNAME" ]; then
  hostnamectl set-hostname "\$HOSTNAME"
  sed -i "s/^127.0.1.1.*/127.0.1.1\t\$HOSTNAME/" /etc/hosts
fi

# The setup access point. autoconnect is off: MatrixOS brings it up when it
# decides the device needs setting up, and one radio cannot do both (C-8).
if nmcli -t -f NAME connection show | grep -qx "$AP_PROFILE"; then
  nmcli connection modify "$AP_PROFILE" 802-11-wireless.ssid "\$AP_SSID"
else
  nmcli connection add type wifi ifname wlan0 con-name "$AP_PROFILE" \\
    autoconnect no ssid "\$AP_SSID" \\
    802-11-wireless.mode ap 802-11-wireless.band bg \\
    ipv4.method shared ipv6.method ignore >/dev/null
fi
EOF
chmod 0755 /usr/local/sbin/matrixos-identity

cat >/etc/systemd/system/matrixos-identity.service <<'EOF'
# Written by provision.sh (FR-32).
[Unit]
Description=Derive the MatrixOS device identity from the CPU serial
After=NetworkManager.service
Wants=NetworkManager.service
Before=matrixos.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/matrixos-identity

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable matrixos-identity >/dev/null
systemctl start matrixos-identity || warn "identity service failed — check journalctl -u matrixos-identity"
note "this unit is $(hostname)"

# =============================================================================
step "7. Durability (NFR-19, NFR-20, ADR-0008)"
# =============================================================================

# Volatile, not none: journalctl stays usable for the current boot, which is what
# makes a remote "it doesn't work" diagnosable at all.
install -d -m 0755 /etc/systemd/journald.conf.d
if [ -f /etc/systemd/journald.conf.d/matrixos.conf ]; then
  skip "journal in RAM"
else
  note "moving the journal into RAM, capped at 16 MB"
  cat >/etc/systemd/journald.conf.d/matrixos.conf <<'EOF'
# Written by provision.sh (ADR-0008). RuntimeMaxUse is a memory bound as much as
# a wear bound: with no swap, an unbounded writer is an OOM kill (NFR-4).
[Journal]
Storage=volatile
RuntimeMaxUse=16M
EOF
  systemctl restart systemd-journald
fi

if [ -f /etc/dphys-swapfile ] || systemctl is-enabled dphys-swapfile >/dev/null 2>&1; then
  note "disabling swap — the single largest write source on the card"
  dphys-swapfile swapoff 2>/dev/null || true
  dphys-swapfile uninstall 2>/dev/null || true
  systemctl disable dphys-swapfile >/dev/null 2>&1 || true
  rm -f /var/swap
elif [ -f /etc/rpi/swap.conf ]; then
  # Trixie replaced dphys-swapfile with rpi-swap: zram plus periodic writeback
  # into /var/swap on the card.
  RPI_SWAP_DROPIN=/etc/rpi/swap.conf.d/90-matrixos.conf
  if [ -f "$RPI_SWAP_DROPIN" ]; then
    skip "rpi-swap disabled"
  else
    note "disabling rpi-swap — the single largest write source on the card"
    install -d -m 0755 /etc/rpi/swap.conf.d
    cat >"$RPI_SWAP_DROPIN" <<'EOF'
# Written by provision.sh (ADR-0008, NFR-20).
[Main]
Mechanism=none
EOF
    NEEDS_REBOOT=1
  fi

  if [ -e /var/swap ]; then
    note "removing /var/swap"
    swapoff -a || true
    for loop in $(losetup -j /var/swap -O NAME --noheadings); do
      losetup -d "$loop" || true
    done
    rm -f /var/swap
  fi
else
  skip "swap is off"
fi

if grep -qE '^\S+\s+/\s+\S+\s+[^ ]*noatime' /etc/fstab; then
  skip "noatime on the root filesystem"
else
  note "adding noatime to the root filesystem"
  awk 'BEGIN{OFS="\t"} $2=="/" && $4 !~ /noatime/ {$4=$4",noatime"} {print}' /etc/fstab \
    >/etc/fstab.new && mv /etc/fstab.new /etc/fstab
fi

# =============================================================================
step "8. Maintainer access (NFR-22, ADR-0016)"
# =============================================================================

if [ ! -d /etc/cloud ] || [ -e /etc/cloud/cloud-init.disabled ]; then
  skip "cloud-init disabled"
else
  note "disabling cloud-init — the Imager settings it carried are applied by now"
  touch /etc/cloud/cloud-init.disabled
fi

CLOUD_SEED=()
for seed in /boot/firmware/user-data /boot/firmware/network-config /boot/firmware/meta-data; do
  if [ -e "$seed" ]; then
    CLOUD_SEED+=("$seed")
  fi
done
if [ ${#CLOUD_SEED[@]} -eq 0 ]; then
  skip "no cloud-init seed on the boot partition"
else
  note "removing the cloud-init seed — it holds the account and the WiFi password in cleartext"
  mount -o remount,rw /boot/firmware
  rm -f "${CLOUD_SEED[@]}"
fi

SSH_KEY_DIR="$STATE_DIR/ssh"
HOST_KEY="$SSH_KEY_DIR/ssh_host_ed25519_key"

cat >/etc/systemd/system/matrixos-ssh-hostkey.service <<EOF
# Written by provision.sh (ADR-0016).
[Unit]
Description=Generate this unit's SSH host key on the state partition
RequiresMountsFor=$STATE_DIR
ConditionPathExists=!$HOST_KEY
Before=ssh.service

[Service]
Type=oneshot
ExecStart=/usr/bin/install -d -o root -g root -m 0700 $SSH_KEY_DIR
ExecStart=/usr/bin/ssh-keygen -q -t ed25519 -N "" -C "" -f $HOST_KEY

[Install]
WantedBy=multi-user.target ssh.service
EOF
systemctl daemon-reload
systemctl -q enable matrixos-ssh-hostkey

if [ -f "$HOST_KEY" ]; then
  skip "SSH host key on the state partition"
else
  note "generating this unit's SSH host key"
  systemctl start matrixos-ssh-hostkey
  warn "the host key changes once now. On your machine:  ssh-keygen -R $(hostname).local"
fi

SSHD_DROPIN=/etc/ssh/sshd_config.d/10-matrixos.conf
SSHD_CONFIG="# Written by provision.sh (ADR-0016). sshd keeps the first value it reads, and
# this file has to sort before a 50-cloud-init.conf that turns passwords back on.
HostKey $HOST_KEY
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no
AllowUsers $MAINTAINER"

if [ "$(cat "$SSHD_DROPIN" 2>/dev/null)" = "$SSHD_CONFIG" ]; then
  skip "SSH by key only, for $MAINTAINER"
else
  note "allowing SSH by key only, for $MAINTAINER"
  printf '%s\n' "$SSHD_CONFIG" >"$SSHD_DROPIN"
  if ! sshd -t; then
    rm -f "$SSHD_DROPIN"
    die "sshd rejected the configuration — removed it again"
  fi
fi
systemctl -q enable ssh
systemctl reload-or-restart ssh

# Before the password goes: without it, sudo is the only thing that still asks for one.
SUDOERS=/etc/sudoers.d/010-matrixos-maintainer
if [ -f "$SUDOERS" ]; then
  skip "sudo without a password for $MAINTAINER"
else
  note "allowing sudo without a password for $MAINTAINER"
  SUDOERS_NEW=$(mktemp)
  echo "$MAINTAINER ALL=(ALL) NOPASSWD: ALL" >"$SUDOERS_NEW"
  visudo -cqf "$SUDOERS_NEW" || die "visudo rejected the sudoers rule"
  install -m 0440 "$SUDOERS_NEW" "$SUDOERS"
  rm -f "$SUDOERS_NEW"
fi

if [ "$(getent shadow "$MAINTAINER" | cut -d: -f2)" = "*" ]; then
  skip "no password hash for $MAINTAINER"
else
  note "removing $MAINTAINER's password — its hash would ship on every unit"
  usermod -p '*' "$MAINTAINER"
fi

# =============================================================================
step "9. Read-only root (NFR-20)"
# =============================================================================

if [ "$WANT_READONLY" -eq 0 ]; then
  warn "skipped on request (--no-readonly): pulling the plug can corrupt the card"
elif [ "$HAVE_STATE_PARTITION" -eq 0 ]; then
  warn "skipped: without the state partition the device would forget everything"
elif grep -q 'overlayroot=tmpfs:recurse=0' /boot/firmware/cmdline.txt && dpkg -s overlayroot >/dev/null 2>&1; then
  skip "overlay filesystem is enabled"
else
  note "enabling the overlay filesystem and a read-only boot partition"
  raspi-config nonint enable_overlayfs || die "raspi-config could not enable the overlay"
  raspi-config nonint enable_bootro || warn "boot partition left writable (no enable_bootro in this raspi-config)"

  # recurse=0: by default overlayroot also puts every other ext4 mount in fstab
  # under a RAM overlay — including the state partition, which would then
  # forget everything on reboot.
  mount -o remount,rw /boot/firmware
  sed -i 's/overlayroot=tmpfs /overlayroot=tmpfs:recurse=0 /' /boot/firmware/cmdline.txt

  # raspi-config returns 0 even when its own apt-get failed, which leaves the
  # kernel parameter without the package that acts on it.
  grep -q 'overlayroot=tmpfs:recurse=0' /boot/firmware/cmdline.txt && dpkg -s overlayroot >/dev/null 2>&1 ||
    die "the overlay is not set up — check /boot/firmware/cmdline.txt and the overlayroot package"
  NEEDS_REBOOT=1
  warn "from now on the root filesystem is read-only. To make it writable again:"
  warn "  sudo mount -o remount,rw /boot/firmware"
  warn "  sudo sed -i 's/overlayroot=tmpfs[^ ]* //' /boot/firmware/cmdline.txt && sudo reboot"
  warn "raspi-config's disable_overlayfs does not recognise the recurse=0 parameter."
fi

# =============================================================================
echo
echo "✅ Provisioning complete."
echo
echo "   Device .......... $(hostname)"
echo "   Binary .......... $BINARY"
echo "   State ........... $STATE_DIR $([ "$HAVE_STATE_PARTITION" -eq 1 ] && echo '(own partition)' || echo '(on the root filesystem)')"
AP_SSID=$(nmcli -t -f 802-11-wireless.ssid connection show "$AP_PROFILE" 2>/dev/null | cut -d: -f2)
echo "   Setup network ... ${AP_SSID:-not created}"

if [ "$NEEDS_REBOOT" -eq 1 ]; then
  echo
  echo "   ⟳ Reboot required for the changes above to take effect."
fi
