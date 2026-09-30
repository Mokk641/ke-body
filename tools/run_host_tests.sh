#!/bin/sh
# Host-side tests: no board, no ESP-IDF toolchain needed (only gcc and the IDF cJSON source).
#   tools/run_host_tests.sh [path-to-esp-idf]
# Optional: UI_C=<path to an older ui.c> to run the touch test against it (to prove it catches a bug).
set -e
cd "$(dirname "$0")/.."
IDF="${1:-${IDF_PATH:-$HOME/esp/esp-idf}}"
OUT="${TMPDIR:-/tmp}/ke-host-tests"
mkdir -p "$OUT"

echo "== lineedit =="
gcc -Wall -Wextra -std=c99 -Imain -o "$OUT/test_lineedit" tools/test_lineedit.c main/lineedit.c
"$OUT/test_lineedit"

echo
echo "== ui touch =="
gcc -O1 -Wall -Wno-format-truncation -std=gnu11 -DHOST_TEST \
    -Itools/hoststubs -Imain -I"$IDF/components/json/cJSON" \
    -o "$OUT/test_ui_touch" tools/test_ui_touch.c "${UI_C:-main/ui.c}" main/ui_render.c main/gfx.c \
    main/fonts/font_*.c "$IDF/components/json/cJSON/cJSON.c" -lm
"$OUT/test_ui_touch"
