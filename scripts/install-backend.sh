#!/usr/bin/env bash
#
# scrybe — install a single STT backend on demand (called from Settings ▸ Hardware).
#
#   scripts/install-backend.sh openvino | faster-whisper | whispercpp
#
# openvino        installs the OpenVINO GenAI C++ runtime + Intel GPU runtime and
#                 rebuilds Scrybe with the backend enabled (needs an Intel GPU to
#                 be useful, but can be forced anywhere).
# faster-whisper  installs the CTranslate2 Python package (NVIDIA CUDA / CPU).
# whispercpp      builds a local whisper.cpp server (Vulkan) + a user service.
set -euo pipefail

BACKEND="${1:-}"
SCRIPT_PATH="${BASH_SOURCE[0]:-$0}"
if [[ -n "$SCRIPT_PATH" && -f "$SCRIPT_PATH" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")/.." && pwd)"
else
    SCRIPT_DIR="${SCRYBE_SRC:-$HOME/.local/src/scrybe}"
fi
BRANCH="${SCRYBE_BRANCH:-main}"

if [[ -f "$SCRIPT_DIR/scripts/python-env.sh" ]]; then
    # shellcheck source=python-env.sh
    source "$SCRIPT_DIR/scripts/python-env.sh"
else
    SCRYBE_PYTHON_VENV="${SCRYBE_PYTHON_VENV:-$HOME/.local/share/scrybe/venv}"
    scrybe_python() {
        if [[ -x "$SCRYBE_PYTHON_VENV/bin/python" ]]; then
            printf '%s\n' "$SCRYBE_PYTHON_VENV/bin/python"
        else
            printf '%s\n' python3
        fi
    }
    scrybe_ensure_venv() {
        command -v python3 >/dev/null || return 1
        if [[ ! -x "$SCRYBE_PYTHON_VENV/bin/python" ]]; then
            mkdir -p "$(dirname "$SCRYBE_PYTHON_VENV")"
            python3 -m venv "$SCRYBE_PYTHON_VENV" || return 1
        fi
        "$SCRYBE_PYTHON_VENV/bin/python" -m pip --version >/dev/null 2>&1 \
            || "$SCRYBE_PYTHON_VENV/bin/python" -m ensurepip --upgrade >/dev/null 2>&1 \
            || return 1
    }
    scrybe_pip_install() {
        scrybe_ensure_venv || return 1
        "$SCRYBE_PYTHON_VENV/bin/python" -m pip install -q "$@"
    }
fi

c_blue=$'\e[1;34m'; c_grn=$'\e[1;32m'; c_yel=$'\e[1;33m'; c_red=$'\e[1;31m'; c_rst=$'\e[0m'
step() { echo; echo "${c_blue}==> $*${c_rst}"; }
ok()   { echo "${c_grn}  ✔ $*${c_rst}"; }
warn() { echo "${c_yel}  ! $*${c_rst}"; }
die()  { echo "${c_red}  x $*${c_rst}" >&2; exit 1; }

case "$BACKEND" in
  openvino)
    step "Installing the OpenVINO (Intel) backend and rebuilding Scrybe"
    # Reuse the main setup, forcing OpenVINO on and skipping unrelated phases.
    SCRYBE_WITH_OPENVINO=1 \
    SCRYBE_SKIP_YDOTOOL=1 SCRYBE_SKIP_OLLAMA=1 SCRYBE_SKIP_BACKENDS=1 \
    SCRYBE_SKIP_HOTKEY=1 SCRYBE_SKIP_AUTOSTART=1 \
        bash "$SCRIPT_DIR/build-and-setup.sh"
    ok "OpenVINO backend installed. Restart Scrybe."
    ;;

  faster-whisper|fasterwhisper)
    step "Installing faster-whisper (NVIDIA CUDA / CPU)"
    command -v python3 >/dev/null || die "python3 not found — run build-and-setup.sh first."
    scrybe_pip_install faster-whisper || die "pip install failed."
    mkdir -p "$HOME/.local/share/scrybe/backends"
    sidecar_dst="$HOME/.local/share/scrybe/backends/faster_whisper_sidecar.py"
    if [[ -f "$SCRIPT_DIR/scripts/faster_whisper_sidecar.py" ]]; then
        install -m644 "$SCRIPT_DIR/scripts/faster_whisper_sidecar.py" "$sidecar_dst"
    elif command -v curl >/dev/null; then
        curl -fsSL "https://raw.githubusercontent.com/mrelmida/scrybe/$BRANCH/scripts/faster_whisper_sidecar.py" \
            -o "$sidecar_dst" || warn "could not refresh faster-whisper sidecar."
    else
        warn "curl not found; could not refresh faster-whisper sidecar."
    fi
    ok "faster-whisper installed in $SCRYBE_PYTHON_VENV. Select it in Settings ▸ Speech (or use 'auto')."
    ;;

  whispercpp|whisper.cpp)
    step "Building the whisper.cpp server (Vulkan)"
    SRC="$HOME/.local/src/whisper.cpp"
    MODEL_URL="https://huggingface.co/ggml-org/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin"
    command -v cmake >/dev/null || die "cmake not found — run build-and-setup.sh first."
    if [[ ! -d "$SRC/.git" ]]; then
        git clone --depth 1 https://github.com/ggml-org/whisper.cpp "$SRC" \
            || die "clone failed."
    fi
    # Vulkan if the loader is present, else a plain CPU build.
    vk=OFF; ldconfig -p 2>/dev/null | grep -q libvulkan && vk=ON
    cmake -S "$SRC" -B "$SRC/build" -DGGML_VULKAN=$vk >/dev/null
    cmake --build "$SRC/build" -j"$(nproc)" --target whisper-server
    mkdir -p "$SRC/models"
    if [[ ! -f "$SRC/models/ggml-large-v3-turbo.bin" ]]; then
        step "Downloading whisper.cpp model (large-v3-turbo, ~1.5 GB)"
        curl -fL "$MODEL_URL" -o "$SRC/models/ggml-large-v3-turbo.bin" \
            || warn "model download failed — place a ggml model in $SRC/models."
    fi
    mkdir -p "$HOME/.config/systemd/user"
    cat > "$HOME/.config/systemd/user/scrybe-whispercpp.service" <<EOF
[Unit]
Description=whisper.cpp server for Scrybe

[Service]
ExecStart=$SRC/build/bin/whisper-server -m $SRC/models/ggml-large-v3-turbo.bin --port 8080
Restart=on-failure

[Install]
WantedBy=default.target
EOF
    systemctl --user daemon-reload
    systemctl --user enable --now scrybe-whispercpp.service \
        && ok "whisper-server running on :8080 (Vulkan=$vk)." \
        || warn "Could not start the whisper-server service."
    echo "     In Settings ▸ Speech, set the backend to 'whispercpp'."
    ;;

  ""|*)
    die "Usage: install-backend.sh openvino | faster-whisper | whispercpp"
    ;;
esac
