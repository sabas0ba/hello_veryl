#!/usr/bin/env bash
# riscv-tests (rv32ui / rv32um / rv32mi) を自前の最小テスト環境でビルドする
# (docs/riscv.md「検証」)．FPGA ツールチェーンコンテナ内での実行を前提とする:
#   .\scripts\fpga-run.ps1 bash verif/riscv/build_tests.sh [sim|hw]
#
# ターゲット:
#   sim (既定) : 0x0000_0000 リンク -> build/riscv_tests
#                シミュレーションのランナー (test_riscv_tests) は RvMem の
#                先頭へ直接プリロードするため 0x0 でリンクする
#   hw         : 0x1000_0000 リンク -> build/riscv_tests_hw
#                実機は UART モニタが PSRAM (0x1000_0000) へロードする．
#                リンカが la を絶対アドレスの li へ緩和するため，
#                ロードアドレスでリンクしないとデータ参照が壊れる
set -euo pipefail
cd "$(dirname "$0")/../.."

TARGET="${1:-sim}"
SRC=/opt/riscv-tests
ENV=verif/riscv/env

case "$TARGET" in
    sim) LD="$ENV/link.ld"    ; OUT=build/riscv_tests    ;;
    hw)  LD="$ENV/link_hw.ld" ; OUT=build/riscv_tests_hw ;;
    *)   echo "ERROR: 未知のターゲット '$TARGET' (sim|hw)" >&2; exit 1 ;;
esac

if [ ! -d "$SRC" ]; then
    echo "ERROR: $SRC が無い (container/Containerfile の riscv-tests 層を確認)" >&2
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT"

# 対象外の拡張・機能 (docs/riscv.md「ISA スコープ」「残課題・リスク」)
#   fence_i          : fence.i は Zifencei 拡張
#   ma_data          : 非整列アクセスがハードウェアで動作することを要求する．
#                      本コアは RISC-V 仕様が許すもう一方の挙動 (アドレス非整列例外の
#                      送出) を選んでいるため通らない (rv32mi の *-misaligned が
#                      そちらのオラクルになる)
#   breakpoint       : デバッグトリガ (tdata CSR) は非対応
#   pmpaddr          : PMP は非対応
#   instret_overflow : Zicntr (minstret/mcycle) は非対応
#   zicntr           : 同上
SKIP="fence_i ma_data breakpoint pmpaddr instret_overflow zicntr"

ok=0
skipped=""

build_set() {
    local dir="$1"
    local march="$2"
    for f in "$SRC/isa/$dir"/*.S; do
        local n
        n=$(basename "$f" .S)
        case " $SKIP " in
            *" $n "*) skipped="$skipped $n"; continue ;;
        esac
        riscv64-unknown-elf-gcc -march="$march" -mabi=ilp32 -nostdlib -nostartfiles \
            -I"$ENV" -I"$SRC/isa/macros/scalar" -I"$SRC/env" \
            -T"$LD" -o "$OUT/$n.elf" "$f"
        riscv64-unknown-elf-objcopy -O binary "$OUT/$n.elf" "$OUT/$n.bin"
        ok=$((ok + 1))
    done
}

build_set rv32ui rv32i_zicsr
build_set rv32um rv32im_zicsr
build_set rv32mi rv32im_zicsr

echo "built: $ok tests ($TARGET) -> $OUT"
[ -n "$skipped" ] && echo "skipped (対象外拡張):$skipped"
exit 0
