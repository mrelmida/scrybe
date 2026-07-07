#!/usr/bin/env bash
# Shared Python environment helpers for Scrybe scripts.

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

scrybe_python_has_module() {
    local module="$1"
    "$(scrybe_python)" -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('$module') else 1)" \
        >/dev/null 2>&1
}
