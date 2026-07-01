#!/usr/bin/env bash
#
# Scrybe online installer.
#   curl -fsSL https://mrelmida.dev/scrybe/install.sh | bash
#
# Downloads the Scrybe source and runs build-and-setup.sh (which installs all
# dependencies, builds, and sets up the app + hotkey + autostart).
#
# Override defaults with env vars:
#   SCRYBE_REPO    git/https source repo   (default below)
#   SCRYBE_BRANCH  branch                  (default: main)
#   SCRYBE_SRC     checkout dir            (default: ~/.local/src/scrybe)
set -euo pipefail

REPO="${SCRYBE_REPO:-https://github.com/mrelmida/scrybe}"
BRANCH="${SCRYBE_BRANCH:-main}"
DEST="${SCRYBE_SRC:-$HOME/.local/src/scrybe}"

say() { printf '\e[1;34m==>\e[0m %s\n' "$*"; }
die() { printf '\e[1;31mError:\e[0m %s\n' "$*" >&2; exit 1; }

[[ $EUID -eq 0 ]] && die "Run as your normal user, not root."

say "Fetching Scrybe source into $DEST"
mkdir -p "$(dirname "$DEST")"

if command -v git >/dev/null; then
    if [[ -d "$DEST/.git" ]]; then
        git -C "$DEST" pull --ff-only || die "git pull failed."
    else
        rm -rf "$DEST"
        git clone --depth 1 --branch "$BRANCH" "$REPO" "$DEST" || die "git clone failed."
    fi
elif command -v curl >/dev/null && command -v tar >/dev/null; then
    tmp="$(mktemp -d)"
    curl -fsSL "$REPO/archive/refs/heads/$BRANCH.tar.gz" -o "$tmp/src.tgz" \
        || die "download failed (set SCRYBE_REPO?)."
    rm -rf "$DEST"; mkdir -p "$DEST"
    tar -xzf "$tmp/src.tgz" -C "$DEST" --strip-components=1
    rm -rf "$tmp"
else
    die "Need either 'git' or 'curl'+'tar' to fetch the source."
fi

[[ -f "$DEST/build-and-setup.sh" ]] || die "build-and-setup.sh not found in source."

say "Running build-and-setup.sh"
# Redirect stdin from the terminal so 'sudo' can prompt even under curl | bash.
if [[ -t 0 ]]; then
    bash "$DEST/build-and-setup.sh"
elif [[ -e /dev/tty ]]; then
    bash "$DEST/build-and-setup.sh" </dev/tty
else
    bash "$DEST/build-and-setup.sh"
fi

say "Scrybe installed. Launch it from your app menu or run: scrybe"
