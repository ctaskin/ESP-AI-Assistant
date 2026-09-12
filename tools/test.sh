#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
tmp_bin=$(mktemp "${TMPDIR:-/tmp}/ceko-tests.XXXXXX")
trap 'rm -f "$tmp_bin"' EXIT
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Imain tests/test_core.c main/audio_math.c main/capture_gate.c main/ws_message.c main/history.c main/session_policy.c -lm -o "$tmp_bin"
bash tools/syntax-check.sh
"$tmp_bin"
