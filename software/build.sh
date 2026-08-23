#!/usr/bin/env bash
# ソフトコアのブートプログラムをビルドし，Veryl の ROM パッケージを生成する
# (docs/riscv.md パッチ計画 #6)．コンテナ内での実行を前提とする:
#   .\scripts\fpga-run.ps1 bash software/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=build/software
mkdir -p "$OUT"

riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles \
    -T software/link.ld -o "$OUT/hello.elf" software/hello.S
riscv64-unknown-elf-objcopy -O binary "$OUT/hello.elf" "$OUT/hello.bin"
riscv64-unknown-elf-objdump -d "$OUT/hello.elf" > "$OUT/hello.dis"

/opt/oss-cad-suite/py3bin/python3 scripts/gen_riscv_boot.py "$OUT/hello.bin"
echo "size: $(stat -c %s "$OUT/hello.bin") byte"
