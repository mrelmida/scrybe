#!/usr/bin/env bash
# Downloads a pre-converted OpenVINO Whisper model from Hugging Face into
# ~/.local/share/scrybe/models/<dir>, ready for ov::genai::WhisperPipeline.
#
# Usage: scripts/download-model.sh [base|small|medium|distil]   (default: small)
set -euo pipefail

SCRIPT_PATH="${BASH_SOURCE[0]:-$0}"
if [[ -n "$SCRIPT_PATH" && -f "$SCRIPT_PATH" ]]; then
    SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")/.." && pwd)"
else
    SCRIPT_DIR="${SCRYBE_SRC:-$PWD}"
fi

# shellcheck source=python-env.sh
source "$SCRIPT_DIR/scripts/python-env.sh"

MODEL="${1:-small}"
case "$MODEL" in
    tiny)     REPO="OpenVINO/whisper-tiny-fp16-ov" ;;
    base)     REPO="OpenVINO/whisper-base-fp16-ov" ;;
    small)    REPO="OpenVINO/whisper-small-fp16-ov" ;;
    medium)   REPO="OpenVINO/whisper-medium-fp16-ov" ;;
    turbo)    REPO="OpenVINO/whisper-large-v3-turbo-fp16-ov" ;;
    large-v3) REPO="OpenVINO/whisper-large-v3-fp16-ov" ;;
    distil)   REPO="OpenVINO/distil-whisper-large-v3-fp16-ov" ;;
    *) echo "Unknown model '$MODEL' (tiny|base|small|medium|turbo|large-v3|distil)" >&2; exit 1 ;;
esac

DIR="$HOME/.local/share/scrybe/models/$(basename "$REPO")"
mkdir -p "$DIR"

scrybe_python_has_module huggingface_hub || scrybe_pip_install huggingface_hub

echo "Downloading $REPO -> $DIR"
"$(scrybe_python)" - "$REPO" "$DIR" <<'PY'
import sys
from huggingface_hub import snapshot_download
snapshot_download(repo_id=sys.argv[1], local_dir=sys.argv[2],
                  allow_patterns=["*.xml", "*.bin", "*.json", "*.txt"])
print("done")
PY

# Completion marker: the app treats a directory without it as an interrupted
# download and resumes it (keep in sync with Controller::ensureDownloaded).
touch "$DIR/.complete"

echo "Model ready: $DIR"
