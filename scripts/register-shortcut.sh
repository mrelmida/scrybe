#!/usr/bin/env bash
# Registers a KDE Plasma 6 global shortcut that toggles scrybe dictation.
#
# On Wayland, apps cannot grab global hotkeys directly, so KDE owns the shortcut
# and runs the `scrybe --toggle` launcher, which pokes the running daemon.
#
# KDE stores launcher shortcuts under the [services][<desktop-id>] group of
# kglobalshortcutsrc. kglobalaccel reads that file at startup and rewrites it on
# exit, so we must stop it *before* editing and start it again to register.
#
# Usage: scripts/register-shortcut.sh [/absolute/path/to/scrybe] [SHORTCUT]
#   SHORTCUT default: "Meta+Alt+D"
set -euo pipefail

BIN="${1:-$(command -v scrybe || echo "$HOME/.local/bin/scrybe")}"
SHORTCUT="${2:-Meta+Alt+D}"
DESKTOP_ID="scrybe-toggle.desktop"
NAME="Scrybe Toggle Dictation"

if [[ ! -x "$BIN" ]]; then
    echo "scrybe binary not found/executable at: $BIN" >&2
    echo "Pass the path explicitly: $0 /path/to/scrybe" >&2
    exit 1
fi

# 1. Install the launcher .desktop (must be resolvable by KService/desktop-id).
DESKTOP_DIR="$HOME/.local/share/applications"
mkdir -p "$DESKTOP_DIR"
cat > "$DESKTOP_DIR/$DESKTOP_ID" <<EOF
[Desktop Entry]
Type=Application
Name=$NAME
Exec=$BIN --toggle
NoDisplay=true
EOF
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

# 2. Stop kglobalaccel so it doesn't overwrite our edit on exit.
#    On Plasma-Wayland it's usually session-launched (not the systemd unit), so
#    prefer kquitapp6 which quits it however it was started.
RESTART_CMD=""
if command -v kquitapp6 >/dev/null && kquitapp6 kglobalaccel 2>/dev/null; then
    RESTART_CMD="kstart kglobalaccel"
elif systemctl --user list-unit-files plasma-kglobalaccel.service >/dev/null 2>&1; then
    systemctl --user stop plasma-kglobalaccel.service 2>/dev/null || true
    RESTART_CMD="systemctl --user start plasma-kglobalaccel.service"
fi

# 3. Remove any stale (wrongly-grouped) entry, then write the correct one.
kwriteconfig6 --file kglobalshortcutsrc \
    --group "$DESKTOP_ID" --key "_launch" --delete 2>/dev/null || true
kwriteconfig6 --file kglobalshortcutsrc \
    --group services --group "$DESKTOP_ID" \
    --key "_launch" "$SHORTCUT,none,$NAME"
kwriteconfig6 --file kglobalshortcutsrc \
    --group services --group "$DESKTOP_ID" \
    --key "_k_friendly_name" "$NAME"

# 4. Start kglobalaccel again so it registers the shortcut now.
if [[ -n "$RESTART_CMD" ]]; then
    eval "$RESTART_CMD" || true
fi

echo "Registered '$SHORTCUT' -> $BIN --toggle  (group [services][$DESKTOP_ID])"
echo "If it still doesn't fire, check System Settings > Shortcuts for '$NAME'."
