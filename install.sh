#!/usr/bin/env bash
# Fetch one revision and build separately before publishing. Override with
# SCRYBE_REPO, SCRYBE_REF (tag/commit), SCRYBE_BRANCH (dev), SCRYBE_SRC or SCRYBE_PREFIX.
set -euo pipefail
REPO="${SCRYBE_REPO:-https://github.com/mrelmida/scrybe}"
DEST="${SCRYBE_SRC:-$HOME/.local/src/scrybe}"
say() { printf '==> %s\n' "$*"; }
die() { printf 'Error: %s\n' "$*" >&2; exit 1; }
[[ $EUID -ne 0 ]] || die "Run as your normal user, not root."
command -v git >/dev/null || die "Install git, then run the installer again."
[[ -z "${SCRYBE_REF:-}" || -z "${SCRYBE_BRANCH:-}" ]] || die "Choose SCRYBE_REF or SCRYBE_BRANCH, not both."
[[ "$REPO" != -* ]] || die "Invalid repository URL."
check_destination() {
    if [[ -e "$DEST" || -L "$DEST" ]]; then
        [[ ! -L "$DEST" && -d "$DEST/.git" ]] || die "Refusing unmanaged source path: $DEST"
        local origin
        origin="$(git -C "$DEST" remote get-url origin)"
        [[ "${origin%.git}" == "${REPO%.git}" ]] || die "Source checkout belongs to another repository: $DEST"
        [[ -z "$(git -C "$DEST" status --porcelain --untracked-files=all)" ]] || die "Source checkout has local changes: $DEST"
    fi
}
check_destination
mkdir -p "$(dirname "$DEST")"
stage="$(mktemp -d "$(dirname "$DEST")/.scrybe-source.XXXXXX")"
trap '[[ -z "${stage:-}" ]] || rm -rf -- "$stage"' EXIT
ref="${SCRYBE_REF:-${SCRYBE_BRANCH:-}}"
if [[ -z "$ref" ]]; then
    tags="$(git ls-remote --tags --refs "$REPO")" || die "Could not discover versions."
    ref="$(printf '%s\n' "$tags" | awk '{print $2}' | sed -n '/^refs\/tags\/v\{0,1\}[0-9][0-9.]*$/p' | sort -V | tail -n 1)"
    ref="${ref:-refs/heads/main}"
fi
[[ "$ref" != -* ]] || die "Invalid source revision."
say "Fetching $ref into an isolated checkout"
git init -q "$stage"
git -C "$stage" remote add origin "$REPO"
git -C "$stage" fetch --depth 1 origin "$ref" || die "Fetch failed; current install is unchanged."
commit="$(git -C "$stage" rev-parse --verify 'FETCH_HEAD^{commit}')"
git -C "$stage" checkout -q --detach "$commit"
[[ -f "$stage/scripts/install-runtime.sh" ]] || die "Revision predates staged installation. Select a newer SCRYBE_REF or SCRYBE_BRANCH."
say "Building pinned commit $commit"
if [[ -t 0 ]]; then
    SCRYBE_COMMIT="$commit" bash "$stage/build-and-setup.sh"
elif [[ -r /dev/tty ]] && ( : </dev/tty ) 2>/dev/null; then
    SCRYBE_COMMIT="$commit" bash "$stage/build-and-setup.sh" </dev/tty
else
    SCRYBE_COMMIT="$commit" bash "$stage/build-and-setup.sh"
fi
# CMake embeds the absolute temporary source path. This is our generated build
# directory, never a caller-provided path; drop it after bundling so a later
# backend rebuild configures cleanly from the published checkout.
rm -rf -- "$stage/build"
# Recheck in case the previous source was edited during the build. Runtime
# bundles are independent of the source location and already complete.
check_destination
if [[ -d "$DEST" ]]; then
    backup="$(mktemp -d "${DEST}.previous.XXXXXX")"
    rmdir "$backup"
    mv -- "$DEST" "$backup"
    if ! mv -- "$stage" "$DEST"; then
        mv -- "$backup" "$DEST"
        die "Could not publish source checkout; previous source restored."
    fi
    say "Previous source retained at $backup"
else
    mv -- "$stage" "$DEST"
fi
stage=""
say "Installed $commit. Restart Scrybe; scrybe-rollback restores the previous runtime."
