#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ ! -x bin/quadaeria ]]; then ./build.sh; fi
if [[ -z "${QUADAERIA_IGPU:-}" ]] && command -v nvidia-smi >/dev/null 2>&1 \
   && { ldconfig -p 2>/dev/null | grep -q libGLX_nvidia; }; then
    export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
fi
exec ./bin/quadaeria "$@"
