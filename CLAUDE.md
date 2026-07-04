# Scrybe (repo dir: faststt)

On-device voice dictation for Linux (KDE Plasma 6 / Wayland). C++17 / Qt 6 /
QML daemon: global hotkey → overlay island → local Whisper STT → optional
Ollama cleanup → clipboard paste into the focused app.

## Build & test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure
cmake --build build                                        # build → build/bin/scrybe
ctest --test-dir build --output-on-failure                 # unit tests (Qt Test)
python3 tests/test_sidecar.py                              # sidecar protocol test
./build-and-setup.sh                                       # full install (deps, hotkey, autostart)
```

- `-DSCRYBE_BUILD_APP=OFF` builds only `scrybe_core` + tests (no KF6/LayerShell
  needed — what CI's ubuntu job uses).
- OpenVINO backend is optional: auto-detected under `/opt/scrybe-openvino`,
  forced with `-DSCRYBE_WITH_OPENVINO=ON|OFF`.
- Headless STT check without a mic: `scrybe --transcribe-file file.wav`.

## Architecture

- `src/core/Controller` — state machine (Idle→Listening→Transcribing→Beautifying→Pasting);
  owns all other components, exposed to QML as `controller`.
- `src/stt/` — `SttEngine` (GUI-thread facade) → `SttWorker` (own QThread) →
  pluggable `ISttBackend`s: OpenVINO (in-process, optional), faster-whisper
  (Python sidecar via stdin/stdout JSON+binary protocol), whisper.cpp (HTTP).
- `src/util/`, `src/stt/Resample.*` — pure-logic `scrybe_core` static lib
  (version compare, WAV codec, unquote, resampler). Unit-tested in `tests/`;
  put new testable logic here, not in the Qt classes.
- `scripts/faster_whisper_sidecar.py` — protocol documented in its docstring;
  keep in sync with `FasterWhisperBackend.cpp` and `tests/test_sidecar.py`.
- Settings: QSettings → `~/.config/scrybe/scrybe.conf`. Read keys at point of
  use (not cached in constructors) so config edits apply without restart.
- Model downloads write a `.complete` marker file; a model dir without it is
  treated as an interrupted download (Controller::ensureDownloaded and
  scripts/download-model.sh must stay in sync on this).

## Conventions

- User-facing strings use `tr()`; internal keys/IDs use `QStringLiteral`.
- No blocking calls (QProcess waitFor*, nested event loops) on the GUI thread —
  use signals or the STT worker thread.
- `QProcess::errorOccurred` handlers must only act on `FailedToStart`; other
  errors also reach the `finished` handler (double-handling bug otherwise).
