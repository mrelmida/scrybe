#!/usr/bin/env bash
#
# Uninstall Scrybe. By default removes the app, launcher, autostart, and the
# ydotool helper service, but KEEPS your downloaded models and config.
#
#   scripts/uninstall.sh            # remove app, keep models + config
#   scripts/uninstall.sh --purge    # also remove models, config, cache, and
#                                    # the system OpenVINO install
set -euo pipefail

PREFIX="${SCRYBE_PREFIX:-$HOME/.local}"
PURGE=0
[[ "${1:-}" == "--purge" ]] && PURGE=1

say() { printf '\e[1;34m==>\e[0m %s\n' "$*"; }

say "Stopping Scrybe"
pkill -x scrybe 2>/dev/null || true
rm -f /tmp/scrybe.ipc

say "Removing app, launcher, icon, and autostart"
rm -f "$PREFIX/bin/scrybe"
rm -f "$PREFIX/share/applications/scrybe.desktop"
rm -f "$PREFIX/share/icons/hicolor/scalable/apps/scrybe.svg"
rm -f "$HOME/.config/autostart/scrybe.desktop"
rm -f "$HOME/.local/share/applications/scrybe-toggle.desktop"
update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true

say "Removing ydotool helper service"
systemctl --user disable --now ydotoold.service 2>/dev/null || true
rm -f "$HOME/.config/systemd/user/ydotoold.service"
systemctl --user daemon-reload 2>/dev/null || true

# Best-effort: drop the KDE global shortcut entry.
kwriteconfig6 --file kglobalshortcutsrc --group scrybe --key toggle-dictation --delete 2>/dev/null || true

if [[ $PURGE -eq 1 ]]; then
    say "Purging models, config, cache, and OpenVINO install"
    rm -rf "$HOME/.local/share/scrybe" "$HOME/.config/scrybe" "$HOME/.cache/scrybe"
    sudo rm -rf /opt/scrybe-openvino
    sudo rm -f /etc/ld.so.conf.d/scrybe-openvino.conf
    sudo ldconfig 2>/dev/null || true
    sudo rm -f /etc/udev/rules.d/60-scrybe-uinput.rules
else
    echo "    (kept models/config; run with --purge to remove them)"
fi

say "Scrybe uninstalled."
