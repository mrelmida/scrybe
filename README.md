<div align="center">

# Scrybe

**On-device voice dictation for Linux — speak anywhere, paste anywhere.**

Press a global hotkey, an animated overlay appears with a live voice visualizer,
you talk, and your words are pasted straight into whatever app you're using.
All speech recognition runs **locally** — nothing leaves your machine.

</div>

---

## Install

```bash
curl -fsSL https://mrelmida.dev/scrybe/install.sh | bash
```

This installs all dependencies, builds Scrybe, and sets it up as a desktop app
with a global hotkey and login autostart. Then launch **Scrybe** from your app
menu (it lives in the system tray) and press **`Meta+Alt+D`** to dictate.

> Supported distributions: **Fedora**, **Arch**, **Debian/Ubuntu**, **openSUSE**
> (KDE Plasma 6 / Wayland).

## Features

- 🎙️ **Push-to-talk dictation** with a global hotkey (default `Meta+Alt+D`).
- 🏝️ **Animated overlay** — a translucent "island" with a live, audio-reactive
  **voice visualizer**, anchored to the top or bottom of the screen.
- 📝 **Live transcription** as you speak (optional).
- ⌨️ **Enter** to send · **Esc** to cancel.
- 📋 **Clipboard paste** into the focused app — no simulated keystrokes; your
  clipboard is restored afterward.
- 🧠 **Optional LLM formatting** (local, via [Ollama](https://ollama.com)) —
  clean-up, **Markdown structuring**, **summarizing/shortening**, or your own
  **custom style presets**.
- ⚙️ **Settings window** to pick the backend, model, language, and formatting
  style, and to create custom presets.
- 🔌 **Runs on your hardware** — Intel, NVIDIA, Vulkan GPUs, or CPU.
- 💤 **Lightweight** — the model loads on demand and unloads when idle.
- 🐧 Native **C++/Qt 6 (QML)** for **KDE Plasma 6 / Wayland**.

## Hardware support

Scrybe has pluggable speech-to-text backends and picks the best one for your
machine automatically (`stt/backend=auto`):

| Backend | Best for | Engine |
|---|---|---|
| `openvino` | **Intel** iGPU / Arc / NPU + CPU | OpenVINO GenAI |
| `faster-whisper` | **NVIDIA** (CUDA) + CPU | CTranslate2 |
| `whispercpp` | **Vulkan** / CUDA / any GPU + CPU | whisper.cpp |

Auto-selection uses faster-whisper when an NVIDIA GPU is present, otherwise
OpenVINO. Any backend can be forced in the config.

## Usage

1. Launch **Scrybe** (app menu) — it runs in the tray and autostarts on login.
2. Press **`Meta+Alt+D`**. The island appears and starts listening.
3. Speak. With live preview on, the text fills in as you talk.
4. Press **Enter** to finalize and paste, or **Esc** to cancel.

Open **Settings** from the tray (or `scrybe --settings`) to choose the backend,
model, language, and formatting style, and to manage custom presets. Rebind the
hotkey in *System Settings → Shortcuts → Scrybe*.

## Configuration

Settings live in `~/.config/scrybe/scrybe.conf` and most are exposed in the tray.

| Key | Values | Default | Meaning |
|---|---|---|---|
| `stt/backend` | `auto`·`openvino`·`faster-whisper`·`whispercpp` | `auto` | recognition engine |
| `stt/model` | `tiny`·`base`·`small`·`medium`·`turbo`·`large-v3`·`distil` | `small` | model size |
| `stt/device` | `AUTO:GPU,CPU`·`GPU`·`CPU`·`NPU`·`cuda`·`cpu` | `AUTO:GPU,CPU` | compute device |
| `stt/language` | `auto` or a code (`en`, `de`, `tr`, …) | `auto` | dictation language |
| `ui/preview` | `true`·`false` | `true` | live transcription preview |
| `island/position` | `top`·`bottom` | `top` | overlay anchor |
| `paste/restoreClipboard` | `true`·`false` | `true` | restore clipboard after paste |
| `llm/model` | Ollama model | `qwen2.5:1.5b` | cleanup model |
| `whispercpp/endpoint` | URL | `http://127.0.0.1:8080` | whisper-server endpoint |

```bash
kwriteconfig6 --file ~/.config/scrybe/scrybe.conf --group stt --key model turbo
```

## Models

Speech models download on first use (pick one in the tray → **Speech model**), or:

```bash
scripts/download-model.sh turbo   # tiny|base|small|medium|turbo|large-v3|distil
```

| Key | Model | Notes |
|---|---|---|
| `tiny` | whisper-tiny | fastest, least accurate |
| `base` | whisper-base | fast |
| `small` | whisper-small | balanced (default) |
| `medium` | whisper-medium | accurate |
| `turbo` | whisper-large-v3-turbo | **best speed/accuracy** |
| `large-v3` | whisper-large-v3 | most accurate, slower |
| `distil` | distil-whisper-large-v3 | fast, English-focused |

## Backends

### faster-whisper (NVIDIA / CPU)
```bash
kwriteconfig6 --file ~/.config/scrybe/scrybe.conf --group stt --key backend faster-whisper
kwriteconfig6 --file ~/.config/scrybe/scrybe.conf --group stt --key device cuda   # or cpu
```

### whisper.cpp (Vulkan / any GPU)
Build the server with your backend, then point Scrybe at it:
```bash
git clone https://github.com/ggml-org/whisper.cpp && cd whisper.cpp
cmake -B build -DGGML_VULKAN=ON        # or -DGGML_CUDA=ON
cmake --build build -j --target whisper-server
./build/bin/whisper-server -m models/ggml-large-v3-turbo.bin --port 8080
```
```bash
kwriteconfig6 --file ~/.config/scrybe/scrybe.conf --group stt --key backend whispercpp
```

## LLM formatting (optional)

Enable in **Settings** (or the tray). Before pasting, the transcript is processed
by a local Ollama model. Pick a **style**:

- **Clean formatting** — fix punctuation, casing, and filler words.
- **Markdown** — structure into headings, lists, bold, and code blocks.
- **Summarize & shorten** — condense and remove duplication.
- **Custom presets** — write your own instruction (e.g. *"Rewrite as a formal
  email."*); saved presets appear in the style dropdown.

It never answers questions or changes meaning, and falls back to the raw text if
Ollama isn't running. Pull a model with `ollama pull qwen2.5:1.5b`.

## Build from source

```bash
git clone https://github.com/mrelmida/scrybe && cd scrybe
./build-and-setup.sh
```

Or manually (dependencies already installed):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Common commands:

```bash
scrybe --help                        # all options
scrybe --version
scrybe --settings                    # open the settings window
scrybe --transcribe-file file.wav    # self-test the recognizer (no mic)
make build | run | install | clean   # dev convenience targets
```

### Uninstall

```bash
scripts/uninstall.sh            # remove the app (keeps your models + config)
scripts/uninstall.sh --purge    # also remove models, config, and OpenVINO
```

## Troubleshooting

- **Paste doesn't work** — ensure `ydotoold` is running and you're in the
  `input` group (the installer configures this; **log out/in once** after the
  first install). Terminals use Ctrl+Shift+V, which isn't supported yet.
- **Hotkey does nothing** — check *System Settings → Shortcuts → Scrybe*.
- **First dictation shows "Loading speech model…"** — normal; the model loads on
  demand and is cached afterward.

## Architecture

```
scrybe (C++/Qt6 daemon)
├── core/Controller     state machine (Idle→Listening→Transcribing→Beautifying→Pasting)
├── audio/AudioCapture  microphone capture + level metering
├── stt/                pluggable backends (OpenVINO · faster-whisper · whisper.cpp)
├── llm/LlmBeautifier   Ollama client
├── paste/Paster        clipboard + Ctrl+V injection
└── qml/                animated overlay (island, visualizer, transcript)
```

Global hotkeys use KDE's KGlobalAccel; the overlay is a Wayland layer-shell
surface that returns focus to your app for pasting.

## Roadmap

- [ ] Settings UI (in place of editing the config file)
- [ ] Voice-activity detection
- [ ] Configurable paste shortcut (terminal support)
- [ ] Per-backend model management in the UI

## License

MIT — see [LICENSE](LICENSE).
