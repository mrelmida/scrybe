#!/usr/bin/env bash
# Downloads a pre-converted OpenVINO Whisper model from Hugging Face into
# ~/.local/share/scrybe/models/<dir>, ready for ov::genai::WhisperPipeline.
#
# Usage: scripts/download-model.sh [base|small|medium|distil]   (default: small)
set -euo pipefail

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

python3 -c "import huggingface_hub" 2>/dev/null \
    || pip3 install --user -q huggingface_hub

echo "Downloading $REPO -> $DIR"
python3 - "$REPO" "$DIR" <<'PY'
import sys
from huggingface_hub import snapshot_download
snapshot_download(repo_id=sys.argv[1], local_dir=sys.argv[2],
                  allow_patterns=["*.xml", "*.bin", "*.json", "*.txt"])
print("done")
PY

echo "Model ready: $DIR"
