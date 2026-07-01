#!/usr/bin/env python3
"""scrybe faster-whisper sidecar.

A persistent worker: loads a faster-whisper (CTranslate2) model once, then
transcribes 16 kHz mono float32 audio sent as newline-delimited JSON on stdin,
replying with one JSON line per request on stdout.

Protocol:
  startup -> {"status":"ready","device":"cuda"}  or  {"status":"error","error":...}
  request  <- {"audio_b64":"<base64 float32 LE>","language":"auto"}
  response -> {"text":"...","language":"en"}      or  {"error":...}
"""
import argparse
import base64
import json
import sys


def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default="base")
    ap.add_argument("--device", default="auto")          # cuda | cpu | auto
    ap.add_argument("--compute-type", default="auto")    # float16 | int8 | auto
    args = ap.parse_args()

    try:
        import numpy as np
        from faster_whisper import WhisperModel
    except Exception as e:  # noqa: BLE001
        emit({"status": "error", "error": f"faster-whisper/numpy missing: {e}"})
        return 1

    device = args.device
    compute = args.compute_type
    if compute == "auto":
        compute = "float16" if device == "cuda" else "int8"

    def load(dev, comp):
        return WhisperModel(args.model, device=dev, compute_type=comp)

    try:
        model = load(device, compute)
    except Exception:  # noqa: BLE001
        # Fall back to CPU/int8 if the requested device/type is unavailable.
        try:
            model = load("cpu", "int8")
            device = "cpu"
        except Exception as e2:  # noqa: BLE001
            emit({"status": "error", "error": str(e2)})
            return 1

    emit({"status": "ready", "device": device})

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            audio = np.frombuffer(base64.b64decode(req["audio_b64"]),
                                  dtype=np.float32)
            lang = req.get("language", "auto")
            language = None if lang in ("auto", "") else lang
            segments, info = model.transcribe(audio, language=language,
                                              beam_size=5)
            text = "".join(s.text for s in segments).strip()
            emit({"text": text, "language": info.language})
        except Exception as e:  # noqa: BLE001
            emit({"error": str(e)})
    return 0


if __name__ == "__main__":
    sys.exit(main())
