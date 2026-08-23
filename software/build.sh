#!/usr/bin/env bash
# ソフトコアのプログラムをビルドし，Veryl の ROM パッケージを生成する
# (docs/riscv.md パッチ計画 #6, #10)．コンテナ内での実行を前提とする:
#   .\scripts\fpga-run.ps1 bash software/build.sh          # 既定: hello
#   .\scripts\fpga-run.ps1 bash software/build.sh monitor  # UART ブートモニタ
#
# 生成物 src/riscv/boot_rom.veryl は合成時に ROM へ焼かれるため，
# 実機で riscv-tests を回すときは monitor を選んでビルドし直す．
set -euo pipefail
cd "$(dirname "$0")/.."

PROG="${1:-hello}"
if [ ! -f "software/$PROG.S" ]; then
    echo "ERROR: software/$PROG.S が無い" >&2
    exit 1
fi

OUT=build/software
mkdir -p "$OUT"

riscv64-unknown-elf-gcc -march=rv32im_zicsr -mabi=ilp32 -nostdlib -nostartfiles \
    -T software/link.ld -o "$OUT/$PROG.elf" "software/$PROG.S"
riscv64-unknown-elf-objcopy -O binary "$OUT/$PROG.elf" "$OUT/$PROG.bin"
riscv64-unknown-elf-objdump -d "$OUT/$PROG.elf" > "$OUT/$PROG.dis"

/opt/oss-cad-suite/py3bin/python3 scripts/gen_riscv_boot.py "$OUT/$PROG.bin"
echo "program: $PROG  size: $(stat -c %s "$OUT/$PROG.bin") byte"
