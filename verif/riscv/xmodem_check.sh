#!/usr/bin/env bash
# Exercise the target XMODEM receiver against a host-side UART model.
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=build/xmodem_check
mkdir -p "$OUT"

gcc -std=c99 -O2 -Wall -Wextra -Werror -Isoftware \
    -o "$OUT/xmodem_host" verif/riscv/xmodem_host.c software/xmodem.c

"$OUT/xmodem_host"
