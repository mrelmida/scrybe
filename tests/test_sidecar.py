#!/usr/bin/env python3
"""Protocol test for scripts/faster_whisper_sidecar.py.

Runs the real sidecar against a stub `faster_whisper` module (injected via
PYTHONPATH) and exercises the stdin/stdout protocol end-to-end:
  - ready handshake
  - binary requests (n_bytes header + raw float32 payload)
  - legacy base64 requests
  - per-request error responses (malformed input doesn't kill the process)
  - device fallback when the requested device fails to load

Usage: python3 test_sidecar.py [path/to/faster_whisper_sidecar.py]
Exits 77 (CTest SKIP) if numpy is unavailable.
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

SKIP = 77

STUB = '''\
"""Test stub for faster_whisper: echoes the sample count back as text."""

class _Info:
    language = "en"

class _Segment:
    def __init__(self, text):
        self.text = text

class WhisperModel:
    def __init__(self, model, device="auto", compute_type="auto"):
        if device == "explode":
            raise RuntimeError("no such device")
        self.device = device

    def transcribe(self, audio, language=None, beam_size=5):
        text = "samples=%d lang=%s" % (len(audio), language)
        return iter([_Segment(text)]), _Info()
'''


def start_sidecar(sidecar, stub_dir, device="cpu"):
    env = dict(os.environ)
    env["PYTHONPATH"] = stub_dir + os.pathsep + env.get("PYTHONPATH", "")
    return subprocess.Popen(
        [sys.executable, sidecar, "--model", "base", "--device", device],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, env=env)


def readline_json(proc):
    line = proc.stdout.readline()
    assert line, "sidecar closed stdout unexpectedly"
    return json.loads(line)


def main():
    try:
        import numpy  # noqa: F401  (the sidecar itself needs it)
    except ImportError:
        print("SKIP: numpy not installed")
        return SKIP

    sidecar = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "..", "scripts", "faster_whisper_sidecar.py")
    assert os.path.exists(sidecar), f"sidecar not found: {sidecar}"

    with tempfile.TemporaryDirectory() as stub_dir:
        with open(os.path.join(stub_dir, "faster_whisper.py"), "w") as f:
            f.write(STUB)

        # --- ready handshake -------------------------------------------------
        proc = start_sidecar(sidecar, stub_dir)
        try:
            ready = readline_json(proc)
            assert ready == {"status": "ready", "device": "cpu"}, ready

            # --- binary request: header line + raw float32 payload ----------
            payload = struct.pack("<4f", 0.1, -0.2, 0.3, -0.4)
            header = json.dumps({"n_bytes": len(payload), "language": "de"})
            proc.stdin.write(header.encode() + b"\n" + payload)
            proc.stdin.flush()
            resp = readline_json(proc)
            assert resp == {"text": "samples=4 lang=de", "language": "en"}, resp

            # --- 'auto' language maps to None --------------------------------
            proc.stdin.write(
                json.dumps({"n_bytes": len(payload), "language": "auto"}).encode()
                + b"\n" + payload)
            proc.stdin.flush()
            resp = readline_json(proc)
            assert resp["text"] == "samples=4 lang=None", resp

            # --- legacy base64 request ---------------------------------------
            import base64
            b64 = base64.b64encode(struct.pack("<2f", 1.0, 2.0)).decode()
            proc.stdin.write(
                json.dumps({"audio_b64": b64, "language": "en"}).encode() + b"\n")
            proc.stdin.flush()
            resp = readline_json(proc)
            assert resp["text"] == "samples=2 lang=en", resp

            # --- malformed request → error line, process stays alive ---------
            proc.stdin.write(b"this is not json\n")
            proc.stdin.flush()
            resp = readline_json(proc)
            assert "error" in resp, resp

            proc.stdin.write(
                json.dumps({"n_bytes": len(payload)}).encode() + b"\n" + payload)
            proc.stdin.flush()
            resp = readline_json(proc)
            assert resp["text"] == "samples=4 lang=None", resp

            proc.stdin.close()
            assert proc.wait(timeout=10) == 0
        finally:
            if proc.poll() is None:
                proc.kill()

        # --- device fallback: requested device fails → CPU ------------------
        proc = start_sidecar(sidecar, stub_dir, device="explode")
        try:
            ready = readline_json(proc)
            assert ready == {"status": "ready", "device": "cpu"}, ready
            proc.stdin.close()
            proc.wait(timeout=10)
        finally:
            if proc.poll() is None:
                proc.kill()

    print("OK: sidecar protocol tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
