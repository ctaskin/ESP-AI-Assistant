#!/usr/bin/env bash
# Type checks the WebSocket transport and both provider files on a machine
# without ESP-IDF, using the stub headers in tests/stubs. It proves the files
# parse and match the interfaces used here; it does NOT replace `idf.py build`
# and does not prove the message shapes are what the services expect.
set -euo pipefail
cd "$(dirname "$0")/.."
check() { cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsyntax-only -Imain -Itests/stubs "$@"; }
for src in main/rt_openai.c main/rt_gemini.c main/realtime.c; do check "$src"; done
check -DCONFIG_CEKO_PROVIDER_GEMINI=1 main/realtime.c
echo "PASS: transport and provider files type check against the stub headers"
