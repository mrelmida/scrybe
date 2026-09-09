#!/usr/bin/env bash
# Complete app bundles switch together; dependencies/models/config do not roll back.
set -euo pipefail
mode="${1:-}"
die() { printf 'Runtime install: %s\n' "$*" >&2; exit 1; }
if [[ "$mode" == install ]]; then
    source_dir="${2:?source required}"; build_dir="${3:?build required}"; prefix="${4:?prefix required}"
elif [[ "$mode" == rollback ]]; then
    prefix="${2:-$HOME/.local}"
else
    die 'Usage: install-runtime.sh install SOURCE BUILD PREFIX | rollback [PREFIX]'
fi
mkdir -p "$prefix"
prefix="$(cd "$prefix" && pwd)"
runtime="$prefix/share/scrybe/runtime"
[[ ! -L "$runtime" ]] || die 'Runtime directory must not be a symlink.'
if [[ -e "$runtime" ]]; then
    [[ -f "$runtime/.scrybe-managed" ]] || die "Refusing unmanaged directory: $runtime"
elif [[ "$mode" == install ]]; then
    mkdir -p "$runtime/releases"
    printf '1\n' > "$runtime/.scrybe-managed"
else
    die 'No managed runtime to roll back.'
fi
exec 9>"$runtime/.lock"
flock -n 9 || die 'Another installation or rollback is running.'
valid_release() {
    [[ "$1" == releases/* && "$1" != *..* && "${1#releases/}" != */* &&
       -f "$runtime/$1/.complete" && -x "$runtime/$1/bin/scrybe" ]]
}
pointer() {
    local name="$1" target="$2"
    ln -s -- "$target" "$runtime/.$name.$$" &&
        mv -Tf -- "$runtime/.$name.$$" "$runtime/$name"
}
current="$(readlink "$runtime/current" || true)"
previous="$(readlink "$runtime/previous" || true)"
[[ -z "$current" ]] || valid_release "$current" || die 'Current runtime pointer is invalid.'
stage=""
trap '[[ -z "$stage" ]] || rm -rf -- "$stage"; rm -f -- "$runtime/.current.$$" "$runtime/.previous.$$" "$prefix/bin/.scrybe.$$" "$prefix/bin/.scrybe-rollback.$$"' EXIT
activate() {
    local target="$1" fallback="$2"
    # Write rollback metadata before the activation point. If activation fails,
    # restore the prior metadata; the running app never changes.
    if [[ -n "$fallback" ]]; then pointer previous "$fallback"; fi
    if ! pointer current "$target"; then
        if [[ -n "$previous" ]]; then
            pointer previous "$previous"
        else
            rm -f -- "$runtime/previous"
        fi
        die 'Activation failed; current runtime is unchanged.'
    fi
}
if [[ "$mode" == rollback ]]; then
    valid_release "$previous" || die 'No complete previous runtime is available.'
    [[ -n "$current" ]] || die 'No current runtime is available.'
    activate "$previous" "$current"
    printf 'Restored %s. Restart Scrybe to use it.\n' "$previous"
    exit 0
fi
[[ -x "$build_dir/bin/scrybe" ]] || die 'Built scrybe executable is missing.'
[[ -f "$source_dir/scripts/faster_whisper_sidecar.py" ]] || die 'Sidecar is missing.'
if [[ -f "$source_dir/src/paste/CMakeLists.txt" ]] || grep -q 'scrybe-clipboard' "$source_dir/CMakeLists.txt"; then
    [[ -x "$build_dir/bin/scrybe-clipboard" ]] || die 'Built clipboard helper is missing.'
fi
stage="$(mktemp -d "$runtime/releases/.stage.XXXXXX")"
mkdir -p "$stage/bin" "$stage/scripts"
install -m755 "$build_dir/bin/scrybe" "$stage/bin/scrybe"
if [[ -x "$build_dir/bin/scrybe-clipboard" ]]; then
    install -m755 "$build_dir/bin/scrybe-clipboard" "$stage/bin/scrybe-clipboard"
fi
cp -R -- "$source_dir/scripts/." "$stage/scripts/"
commit="${SCRYBE_COMMIT:-$(git -C "$source_dir" rev-parse HEAD 2>/dev/null || printf local)}"
[[ "$commit" =~ ^[a-zA-Z0-9_-]+$ ]] || die 'Invalid runtime revision.'
printf '%s\n' "$commit" > "$stage/REVISION"
touch "$stage/.complete"
release="releases/$commit-${stage##*.stage.}"
mv -- "$stage" "$runtime/$release"
stage=""
mkdir -p "$prefix/bin"
# Preserve a legacy executable before replacing it with a managed launcher.
if [[ -z "$current" && -e "$prefix/bin/scrybe" ]]; then
    [[ -f "$prefix/bin/scrybe" && ! -L "$prefix/bin/scrybe" ]] || die 'Refusing unmanaged executable symlink.'
    stage="$(mktemp -d "$runtime/releases/legacy.XXXXXX")"
    mkdir -p "$stage/bin" "$stage/scripts"
    cp -p -- "$prefix/bin/scrybe" "$stage/bin/scrybe"
    if [[ -x "$prefix/bin/scrybe-clipboard" ]]; then
        cp -p -- "$prefix/bin/scrybe-clipboard" "$stage/bin/scrybe-clipboard"
    fi
    if [[ -f "$prefix/share/scrybe/backends/faster_whisper_sidecar.py" ]]; then
        cp -p -- "$prefix/share/scrybe/backends/faster_whisper_sidecar.py" "$stage/scripts/"
    fi
    cp -- "$source_dir/scripts/install-runtime.sh" "$stage/scripts/"
    touch "$stage/.complete"
    current="releases/${stage##*/}"
    stage=""
    pointer current "$current"
fi
if [[ -L "$prefix/bin/scrybe" ]]; then
    [[ "$(readlink "$prefix/bin/scrybe")" == "$runtime/current/bin/scrybe" ]] || die 'Refusing unmanaged executable symlink.'
fi
ln -s -- "$runtime/current/bin/scrybe" "$prefix/bin/.scrybe.$$"
mv -Tf -- "$prefix/bin/.scrybe.$$" "$prefix/bin/scrybe"
printf '#!/usr/bin/env bash\nexec bash %q rollback %q\n' \
    "$runtime/current/scripts/install-runtime.sh" "$prefix" > "$prefix/bin/.scrybe-rollback.$$"
chmod 755 "$prefix/bin/.scrybe-rollback.$$"
mv -Tf -- "$prefix/bin/.scrybe-rollback.$$" "$prefix/bin/scrybe-rollback"
# This rename is the activation point. Running processes retain their matching
# artifacts because older release directories are never rewritten or deleted.
activate "$release" "$current"
printf 'Activated %s. Run scrybe-rollback to restore the previous runtime.\n' "$commit"
