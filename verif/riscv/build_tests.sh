#!/usr/bin/env bash
# riscv-tests (rv32ui) を自前の最小テスト環境でビルドする
# (docs/riscv.md「検証」)．FPGA ツールチェーンコンテナ内での実行を前提とする:
#   .\scripts\fpga-run.ps1 bash verif/riscv/build_tests.sh
set -euo pipefail
cd "$(dirname "$0")/../.."

SRC=/opt/riscv-tests
ENV=verif/riscv/env
OUT=build/riscv_tests

if [ ! -d "$SRC" ]; then
    echo "ERROR: $SRC が無い (container/Containerfile の riscv-tests 層を確認)" >&2
    exit 1
fi

mkdir -p "$OUT"

# fence.i は Zifencei 拡張であり本コアの対象外 (docs/riscv.md「ISA スコープ」)
SKIP="fence_i"

ok=0
skipped=""
for f in "$SRC"/isa/rv32ui/*.S; do
    n=$(basename "$f" .S)
    case " $SKIP " in
        *" $n "*) skipped="$skipped $n"; continue ;;
    esac
    riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles \
        -I"$ENV" -I"$SRC/isa/macros/scalar" \
        -T"$ENV/link.ld" -o "$OUT/$n.elf" "$f"
    riscv64-unknown-elf-objcopy -O binary "$OUT/$n.elf" "$OUT/$n.bin"
    ok=$((ok + 1))
done

echo "built: $ok tests -> $OUT"
[ -n "$skipped" ] && echo "skipped (対象外拡張):$skipped"
exit 0
