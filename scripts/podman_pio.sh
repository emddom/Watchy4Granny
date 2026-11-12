#!/usr/bin/env bash
set -euo pipefail

BASE_IMAGE="docker.io/espressif/idf:latest"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIO_CACHE_DIR="${HOME}/.platformio"
PIP_CACHE_DIR="${HOME}/.cache/pip"

mkdir -p "${PIO_CACHE_DIR}" "${PIP_CACHE_DIR}"

DEVICE_ARGS=()
if [ -e /dev/ttyUSB0 ]; then
    DEVICE_ARGS+=(--device /dev/ttyUSB0:/dev/ttyUSB0 --group-add keep-groups)
fi

TTY_ARGS=()
if [ -t 0 ]; then
    TTY_ARGS+=(-it)
fi

# Default to `pio run -e watchy-v1-v2` if no arguments are passed
if [ $# -eq 0 ]; then
    set -- pio run -e watchy-v1-v2
elif [ "$1" != "pio" ] && [ "$1" != "python3" ] && [ "$1" != "bash" ] && [ "$1" != "sh" ]; then
    set -- pio "$@"
fi

exec podman run --rm "${TTY_ARGS[@]}" \
    --userns=keep-id \
    --entrypoint /bin/bash \
    "${DEVICE_ARGS[@]}" \
    -v "${PROJECT_DIR}:/workspace:Z" \
    -v "${PIO_CACHE_DIR}:/platformio_cache:Z" \
    -v "${PIP_CACHE_DIR}:/pip_cache:Z" \
    -e PLATFORMIO_CORE_DIR=/platformio_cache \
    -w /workspace \
    "${BASE_IMAGE}" \
    -c '
        set -euo pipefail
        VENV_DIR="/platformio_cache/espressif_container_venv"
        if [ ! -x "${VENV_DIR}/bin/pio" ]; then
            echo "Initializing PlatformIO in container venv (${VENV_DIR})..."
            IDF_PY="$(ls /opt/esp/python_env/*/bin/python3 | head -n 1)"
            "${IDF_PY}" -m venv --system-site-packages "${VENV_DIR}"
            "${VENV_DIR}/bin/pip" install --find-links /pip_cache/wheels platformio
        fi
        export PATH="${VENV_DIR}/bin:$(dirname "$(ls /opt/esp/python_env/*/bin/python3 | head -n 1)"):${PATH}"
        exec "$@"
    ' -- "$@"
