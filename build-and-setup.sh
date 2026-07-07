#!/usr/bin/env bash
#
# scrybe — one-shot build & permanent install for Fedora / KDE Plasma (Wayland)
#
# Installs ALL dependencies (current + future milestones), sets up Wayland paste
# (ydotool), the Intel iGPU compute runtime, OpenVINO GenAI C++ dev files, pulls
# an Ollama model, builds the app, installs it to ~/.local/bin, and wires up the
# global hotkey + autostart. Idempotent — safe to re-run after code changes.
#
# Phases can be skipped via env vars, e.g.:
#   SCRYBE_SKIP_OPENVINO=1 SCRYBE_SKIP_OLLAMA=1 ./build-and-setup.sh
#
# Config (override via env):
#   SCRYBE_SHORTCUT       global hotkey            (default: Meta+Alt+D)
#   SCRYBE_OLLAMA_MODEL   model to pull            (default: qwen2.5:1.5b)
#   SCRYBE_PREFIX         install prefix           (default: ~/.local)
set -euo pipefail

# ---------------------------------------------------------------------------- #
SCRIPT_PATH="${BASH_SOURCE[0]:-$0}"
if [[ -n "$SCRIPT_PATH" && -f "$SCRIPT_PATH" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"
else
    SCRIPT_DIR="${SCRYBE_SRC:-$PWD}"
fi
PREFIX="${SCRYBE_PREFIX:-$HOME/.local}"
SHORTCUT="${SCRYBE_SHORTCUT:-Meta+Alt+D}"
OLLAMA_MODEL="${SCRYBE_OLLAMA_MODEL:-qwen2.5:1.5b}"
OV_PREFIX="/opt/scrybe-openvino"
CONFIG_DIR="$HOME/.config/scrybe"
BUILD_DIR="$SCRIPT_DIR/build"

# shellcheck source=scripts/python-env.sh
source "$SCRIPT_DIR/scripts/python-env.sh"

c_blue=$'\e[1;34m'; c_grn=$'\e[1;32m'; c_yel=$'\e[1;33m'; c_red=$'\e[1;31m'; c_rst=$'\e[0m'
step() { echo; echo "${c_blue}==> $*${c_rst}"; }
ok()   { echo "${c_grn}  ✔ $*${c_rst}"; }
warn() { echo "${c_yel}  ! $*${c_rst}"; }
die()  { echo "${c_red}  x $*${c_rst}" >&2; exit 1; }

[[ $EUID -eq 0 ]] && die "Run as your normal user (the script uses sudo where needed)."

SUDO="sudo"
$SUDO -n true 2>/dev/null || warn "sudo may prompt for your password."

# --- Detect the package manager --------------------------------------------- #
if   command -v dnf     >/dev/null; then PM=dnf
elif command -v pacman  >/dev/null; then PM=pacman
elif command -v apt-get >/dev/null; then PM=apt
elif command -v zypper  >/dev/null; then PM=zypper
else die "Unsupported distro: need dnf, pacman, apt, or zypper."; fi

pkg_install() {  # pkg_install <fatal|optional> <packages...>
    local mode="$1"; shift
    local rc=0
    case "$PM" in
        dnf)    $SUDO dnf install -y "$@" || rc=$? ;;
        pacman) $SUDO pacman -S --needed --noconfirm "$@" || rc=$? ;;
        apt)    $SUDO apt-get update -qq && $SUDO apt-get install -y "$@" || rc=$? ;;
        zypper) $SUDO zypper --non-interactive install -y "$@" || rc=$? ;;
    esac
    [[ $rc -ne 0 && "$mode" == fatal ]] && die "Failed to install: $*"
    return $rc
}

# --- Detect GPUs so we only install backends that make sense ---------------- #
# Reads PCI vendor ids from sysfs (no external tools): Intel=0x8086,
# NVIDIA=0x10de, AMD=0x1002. Also checks the NVIDIA driver node.
HAS_INTEL_GPU=0; HAS_NVIDIA=0; HAS_AMD=0
[[ -e /proc/driver/nvidia/version ]] && HAS_NVIDIA=1
command -v nvidia-smi >/dev/null 2>&1 && HAS_NVIDIA=1
for v in /sys/class/drm/card[0-9]*/device/vendor; do
    [[ -r "$v" ]] || continue
    case "$(cat "$v" 2>/dev/null)" in
        0x8086) HAS_INTEL_GPU=1 ;;
        0x10de) HAS_NVIDIA=1 ;;
        0x1002) HAS_AMD=1 ;;
    esac
done

# Decide whether to install the OpenVINO (Intel) backend. Auto: only on Intel.
# Force with SCRYBE_WITH_OPENVINO=1/ON, disable with =0/OFF.
case "${SCRYBE_WITH_OPENVINO:-auto}" in
    1|on|ON|yes|true)  WANT_OPENVINO=1 ;;
    0|off|OFF|no|false) WANT_OPENVINO=0 ;;
    *) WANT_OPENVINO=$HAS_INTEL_GPU ;;
esac

step "Detected hardware"
gpus=(); [[ $HAS_NVIDIA -eq 1 ]] && gpus+=("NVIDIA")
[[ $HAS_INTEL_GPU -eq 1 ]] && gpus+=("Intel"); [[ $HAS_AMD -eq 1 ]] && gpus+=("AMD")
echo "     GPUs: ${gpus[*]:-none (CPU only)}"
if [[ $WANT_OPENVINO -eq 1 ]]; then
    ok "Will install the OpenVINO (Intel) backend."
else
    echo "     OpenVINO backend: skipped (no Intel GPU; set SCRYBE_WITH_OPENVINO=1 to force)."
fi
[[ $HAS_NVIDIA -eq 1 ]] && ok "Will install faster-whisper (NVIDIA/CUDA)."
[[ $HAS_AMD -eq 1 && $HAS_NVIDIA -eq 0 && $HAS_INTEL_GPU -eq 0 ]] && \
    echo "     AMD GPU: use the whisper.cpp (Vulkan) backend — scripts/install-backend.sh whispercpp"

# ---------------------------------------------------------------------------- #
# Phase 1 — system packages (build tools + Qt6 + KF6 + Wayland helpers)
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_DEPS:-}" ]]; then
    step "Installing build tools, Qt6, and Wayland helpers ($PM)"
    case "$PM" in
        dnf)    pkg_install fatal gcc-c++ cmake ninja-build pkgconf-pkg-config git python3 python3-pip curl \
                    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel \
                    qt6-qtshadertools-devel layer-shell-qt-devel kf6-kglobalaccel-devel \
                    wl-clipboard ydotool ;;
        pacman) pkg_install fatal base-devel cmake ninja pkgconf git python python-pip curl \
                    qt6-base qt6-declarative qt6-multimedia qt6-shadertools \
                    layer-shell-qt kglobalaccel wl-clipboard ydotool ;;
        apt)    pkg_install fatal build-essential cmake ninja-build pkg-config git python3 python3-pip python3-venv curl \
                    qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-shadertools-dev \
                    liblayershellqtinterface-dev libkf6globalaccel-dev wl-clipboard ydotool ;;
        zypper) pkg_install fatal gcc-c++ cmake ninja pkgconf-pkg-config git python3 python3-pip curl \
                    qt6-base-devel qt6-declarative-devel qt6-multimedia-devel \
                    qt6-shadertools-devel layer-shell-qt6-devel kf6-kglobalaccel-devel \
                    wl-clipboard ydotool ;;
    esac
    ok "Core dependencies installed."
else
    warn "Skipping system packages (SCRYBE_SKIP_DEPS set)."
fi

# ---------------------------------------------------------------------------- #
# Phase 2 — Intel iGPU compute runtime (OpenCL + Level Zero). Non-fatal:
# OpenVINO falls back to CPU, and non-Intel machines use another backend.
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_GPU:-}" && $WANT_OPENVINO -eq 1 ]]; then
    step "Installing Intel GPU compute runtime (for the OpenVINO backend)"
    ok_gpu=1
    case "$PM" in
        dnf)    pkg_install optional intel-compute-runtime intel-opencl oneapi-level-zero intel-gmmlib || ok_gpu=0 ;;
        pacman) pkg_install optional intel-compute-runtime level-zero-loader || ok_gpu=0 ;;
        apt)    pkg_install optional intel-opencl-icd libze-intel-gpu1 libze1 || ok_gpu=0 ;;
        zypper) pkg_install optional intel-compute-runtime level-zero || ok_gpu=0 ;;
    esac
    [[ $ok_gpu -eq 1 ]] && ok "Intel GPU runtime installed." \
        || warn "Intel GPU runtime not installed (fine on non-Intel machines)."
else
    warn "Skipping Intel GPU runtime (non-Intel machine or SCRYBE_SKIP_GPU set)."
fi

# ---------------------------------------------------------------------------- #
# Phase 3 — ydotool (Wayland keystroke injection for Ctrl+V paste)
# Needs access to /dev/uinput; we grant it via the 'input' group + a udev rule,
# and run ydotoold as a user service.
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_YDOTOOL:-}" ]]; then
    step "Configuring ydotool (uinput access + user daemon)"

    echo 'uinput' | $SUDO tee /etc/modules-load.d/uinput.conf >/dev/null
    $SUDO modprobe uinput || warn "could not modprobe uinput now (will load on boot)."

    echo 'KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"' \
        | $SUDO tee /etc/udev/rules.d/60-scrybe-uinput.rules >/dev/null
    $SUDO udevadm control --reload-rules && $SUDO udevadm trigger || true

    if ! id -nG "$USER" | tr ' ' '\n' | grep -qx input; then
        $SUDO usermod -aG input "$USER"
        warn "Added $USER to the 'input' group — LOG OUT/IN for it to take effect."
    fi

    mkdir -p "$HOME/.config/systemd/user"
    cat > "$HOME/.config/systemd/user/ydotoold.service" <<EOF
[Unit]
Description=ydotool daemon (Wayland input injection for scrybe)

[Service]
ExecStart=/usr/bin/ydotoold --socket-path=%t/.ydotool_socket --socket-perm=0600
Restart=always

[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload
    systemctl --user enable --now ydotoold.service 2>/dev/null \
        && ok "ydotoold running." \
        || warn "ydotoold not started yet (needs 'input' group; re-run after re-login)."
else
    warn "Skipping ydotool setup (SCRYBE_SKIP_YDOTOOL set)."
fi

# ---------------------------------------------------------------------------- #
# Phase 4 — OpenVINO GenAI C++ dev files (headers/libs/cmake) for Whisper STT.
# We fetch the exact wheels available for this environment and extract the C++
# artifacts to a system prefix, then record the cmake dirs for the build. The
# app links these .so files directly — no Python runtime dependency.
# Non-fatal: the build will stub STT until these are present (M3).
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_OPENVINO:-}" && $WANT_OPENVINO -eq 1 ]]; then
    step "Installing OpenVINO GenAI C++ runtime to $OV_PREFIX"
    set +e
    tmp="$(mktemp -d)"
    pip3 download --no-deps --only-binary=:all: -d "$tmp" \
        openvino openvino-genai openvino-tokenizers >/dev/null 2>&1
    dl_rc=$?
    if [[ $dl_rc -eq 0 ]]; then
        $SUDO rm -rf "$OV_PREFIX"; $SUDO mkdir -p "$OV_PREFIX"
        for whl in "$tmp"/*.whl; do
            $SUDO python3 -m zipfile -e "$whl" "$OV_PREFIX" >/dev/null 2>&1
        done
        ov_cfg="$(find "$OV_PREFIX" -name OpenVINOConfig.cmake -printf '%h\n' 2>/dev/null | head -1)"
        ov_libs="$(find "$OV_PREFIX" -name 'libopenvino.so*' -printf '%h\n' 2>/dev/null | head -1)"

        # The openvino-genai wheel ships only the runtime .so (no C++ headers or
        # cmake). Fetch the matching-tag headers from GitHub and make the .so
        # linkable so our C++ build can use ov::genai::WhisperPipeline.
        gen_ver="$(ls -d "$OV_PREFIX"/openvino_genai-*.dist-info 2>/dev/null \
                   | sed -E 's#.*openvino_genai-([0-9.]+)\.dist-info#\1#' | head -1)"
        gen_inc="$OV_PREFIX/openvino_genai/include"
        if [[ -n "$gen_ver" && ! -f "$gen_inc/openvino/genai/whisper_pipeline.hpp" ]]; then
            htmp="$(mktemp -d)"
            if curl -sSL --max-time 180 -o "$htmp/g.tgz" \
                "https://github.com/openvinotoolkit/openvino.genai/archive/refs/tags/${gen_ver}.tar.gz"; then
                tar -xzf "$htmp/g.tgz" -C "$htmp" --wildcards '*/src/cpp/include/*' 2>/dev/null || true
                inc="$(find "$htmp" -type d -path '*/src/cpp/include' | head -1)"
                [[ -n "$inc" ]] && $SUDO cp -r "$inc" "$gen_inc"
            fi
            rm -rf "$htmp"
        fi
        genso="$(find "$OV_PREFIX/openvino_genai" -maxdepth 1 -name 'libopenvino_genai.so.*' | head -1)"
        [[ -n "$genso" ]] && $SUDO ln -sf "$(basename "$genso")" \
            "$OV_PREFIX/openvino_genai/libopenvino_genai.so"
        tok_so="$(find "$OV_PREFIX" -name 'libopenvino_tokenizers.so' | head -1)"
        tok_dir="$(dirname "$tok_so")"
        # GenAI loads the tokenizers extension from its OWN directory, so it must
        # sit next to libopenvino_genai.so (the split wheel layout separates them).
        [[ -n "$tok_so" ]] && $SUDO ln -sf "$tok_so" \
            "$OV_PREFIX/openvino_genai/libopenvino_tokenizers.so"

        mkdir -p "$CONFIG_DIR"
        {
            echo "# Written by build-and-setup.sh"
            [[ -n "$ov_cfg"  ]] && echo "export OpenVINO_DIR=\"$ov_cfg\""
        } > "$CONFIG_DIR/openvino.env"

        # Permanent library paths so the installed binary finds OpenVINO,
        # GenAI, and the tokenizers extension at runtime.
        {
            [[ -n "$ov_libs" ]] && echo "$ov_libs"
            echo "$OV_PREFIX/openvino_genai"
            [[ -n "$tok_dir" ]] && echo "$tok_dir"
        } | $SUDO tee /etc/ld.so.conf.d/scrybe-openvino.conf >/dev/null
        $SUDO ldconfig

        if [[ -f "$gen_inc/openvino/genai/whisper_pipeline.hpp" && -n "$genso" ]]; then
            ok "OpenVINO + GenAI C++ ready ($OV_PREFIX)"
        else
            warn "OpenVINO core ready but GenAI headers/lib incomplete — STT build may fail."
        fi
    else
        warn "Could not download OpenVINO wheels (offline?). STT stays stubbed until M3."
    fi
    rm -rf "$tmp"
    set -e
else
    warn "Skipping OpenVINO setup (non-Intel machine or SCRYBE_SKIP_OPENVINO set)."
fi

# ---------------------------------------------------------------------------- #
# Phase 5 — Ollama model (LLM beautifier). Non-fatal.
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_OLLAMA:-}" ]]; then
    step "Pulling Ollama model '$OLLAMA_MODEL' for the LLM beautifier"
    if command -v ollama >/dev/null; then
        systemctl is-active --quiet ollama 2>/dev/null \
            || (systemctl --user is-active --quiet ollama 2>/dev/null || true)
        ollama pull "$OLLAMA_MODEL" && ok "Model '$OLLAMA_MODEL' ready." \
            || warn "Could not pull '$OLLAMA_MODEL' (is 'ollama serve' running?)."
    else
        warn "Ollama not installed — install from https://ollama.com then: ollama pull $OLLAMA_MODEL"
    fi
else
    warn "Skipping Ollama model pull (SCRYBE_SKIP_OLLAMA set)."
fi

# ---------------------------------------------------------------------------- #
# Phase 5b — Whisper STT model. Non-fatal.
# ---------------------------------------------------------------------------- #
STT_MODEL="${SCRYBE_STT_MODEL:-small}"
if [[ -z "${SCRYBE_SKIP_MODEL:-}" && $WANT_OPENVINO -eq 1 ]]; then
    step "Downloading Whisper model '$STT_MODEL' (OpenVINO IR)"
    "$SCRIPT_DIR/scripts/download-model.sh" "$STT_MODEL" \
        && ok "Whisper model '$STT_MODEL' ready." \
        || warn "Model download failed — run scripts/download-model.sh later."
else
    # Other backends fetch their own models on first use (faster-whisper via
    # CTranslate2, whisper.cpp from the server's model dir).
    warn "Skipping OpenVINO model download (backend fetches its own model)."
fi

# ---------------------------------------------------------------------------- #
# Phase 5c — Alternative STT backends (faster-whisper / whisper.cpp)
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_BACKENDS:-}" ]]; then
    step "Installing alternative STT backends"
    mkdir -p "$HOME/.local/share/scrybe/backends"
    install -m644 "$SCRIPT_DIR/scripts/faster_whisper_sidecar.py" \
        "$HOME/.local/share/scrybe/backends/"
    ok "faster-whisper sidecar installed."

    # Install faster-whisper (CUDA/CPU) when it's the chosen/fallback backend:
    # NVIDIA machines, or any machine where OpenVINO isn't being installed (the
    # 'auto' backend then falls back to faster-whisper on CPU).
    if [[ $HAS_NVIDIA -eq 1 || $WANT_OPENVINO -eq 0 || -n "${SCRYBE_WITH_FASTER_WHISPER:-}" ]]; then
        scrybe_pip_install faster-whisper \
            && ok "faster-whisper installed in $SCRYBE_PYTHON_VENV (CUDA/CPU)." \
            || warn "faster-whisper install failed (run: SCRYBE_WITH_FASTER_WHISPER=1 ./build-and-setup.sh)."
    else
        echo "     (faster-whisper skipped; set SCRYBE_WITH_FASTER_WHISPER=1 to install)"
    fi

    cat <<'EOF'
     whisper.cpp (Vulkan / CUDA / CPU) — build the server with your backend:
       git clone https://github.com/ggml-org/whisper.cpp && cd whisper.cpp
       cmake -B build -DGGML_VULKAN=ON        # or -DGGML_CUDA=ON, or plain CPU
       cmake --build build -j --target whisper-server
       ./build/bin/whisper-server -m models/ggml-large-v3-turbo.bin --port 8080
     then set: stt/backend=whispercpp, whispercpp/endpoint=http://127.0.0.1:8080
EOF
else
    warn "Skipping alternative STT backends (SCRYBE_SKIP_BACKENDS set)."
fi

# ---------------------------------------------------------------------------- #
# Phase 6 — Build
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_BUILD:-}" ]]; then
    step "Building scrybe"
    # A build dir configured with a different generator can't be reused.
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] \
       && ! grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" "$BUILD_DIR/CMakeCache.txt"; then
        warn "Existing build dir uses a different generator — reconfiguring clean."
        rm -rf "$BUILD_DIR"
    fi
    cmake_args=(-S "$SCRIPT_DIR" -B "$BUILD_DIR" -G Ninja
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX="$PREFIX"
                -DSCRYBE_WITH_OPENVINO=$([[ $WANT_OPENVINO -eq 1 ]] && echo ON || echo OFF))
    if [[ $WANT_OPENVINO -eq 1 && -f "$CONFIG_DIR/openvino.env" ]]; then
        # shellcheck source=/dev/null
        source "$CONFIG_DIR/openvino.env"
        [[ -n "${OpenVINO_DIR:-}"      ]] && cmake_args+=(-DOpenVINO_DIR="$OpenVINO_DIR")
        [[ -n "${OpenVINOGenAI_DIR:-}" ]] && cmake_args+=(-DOpenVINOGenAI_DIR="$OpenVINOGenAI_DIR")
    fi
    cmake "${cmake_args[@]}"
    cmake --build "$BUILD_DIR" -j"$(nproc)"
    ok "Build complete: $BUILD_DIR/bin/scrybe"
else
    warn "Skipping build (SCRYBE_SKIP_BUILD set)."
fi

# ---------------------------------------------------------------------------- #
# Phase 7 — Install binary
# ---------------------------------------------------------------------------- #
step "Installing binary to $PREFIX/bin"
mkdir -p "$PREFIX/bin"
install -m755 "$BUILD_DIR/bin/scrybe" "$PREFIX/bin/scrybe"
ok "Installed $PREFIX/bin/scrybe"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) warn "$PREFIX/bin is not on your PATH.";; esac

# ---------------------------------------------------------------------------- #
# Phase 8 — Global hotkey (KDE) + autostart
# ---------------------------------------------------------------------------- #
if [[ -z "${SCRYBE_SKIP_HOTKEY:-}" ]]; then
    step "Global hotkey"
    ok "The app self-registers '$SHORTCUT' with KDE (KGlobalAccel) on launch."
    echo "     Change it later in System Settings > Shortcuts > 'scrybe'."
    echo "     (scripts/register-shortcut.sh remains as a file-based fallback.)"
fi

# --- Desktop integration: app launcher + icon (so it appears in the menu) ---
step "Installing desktop launcher and icon"
ICON_DIR="$PREFIX/share/icons/hicolor/scalable/apps"
APP_DIR="$PREFIX/share/applications"
mkdir -p "$ICON_DIR" "$APP_DIR"
install -m644 "$SCRIPT_DIR/packaging/scrybe.svg" "$ICON_DIR/scrybe.svg"
sed "s|^Exec=scrybe|Exec=$PREFIX/bin/scrybe|" \
    "$SCRIPT_DIR/packaging/scrybe.desktop" > "$APP_DIR/scrybe.desktop"
chmod 644 "$APP_DIR/scrybe.desktop"
update-desktop-database "$APP_DIR" 2>/dev/null || true
gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
ok "Launcher installed (find 'Scrybe' in your app menu)."

if [[ -z "${SCRYBE_SKIP_AUTOSTART:-}" ]]; then
    step "Enabling autostart on login"
    mkdir -p "$HOME/.config/autostart"
    sed "s|^Exec=scrybe|Exec=$PREFIX/bin/scrybe|" \
        "$SCRIPT_DIR/packaging/scrybe.desktop" > "$HOME/.config/autostart/scrybe.desktop"
    ok "Autostart enabled."
fi

# ---------------------------------------------------------------------------- #
step "Done"
echo "  Start now:   $PREFIX/bin/scrybe &"
echo "  Toggle:      $PREFIX/bin/scrybe --toggle   (or press $SHORTCUT)"
id -nG "$USER" | tr ' ' '\n' | grep -qx input || \
    warn "Log out/in once so the 'input' group applies (needed for ydotool paste)."
echo
echo "${c_grn}scrybe installed.${c_rst}"
