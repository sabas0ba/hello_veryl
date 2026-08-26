#!/usr/bin/env bash
# RISC-V まわりのソフトウェア側検査 (verif/run.sh から呼ばれる)
#
# 合成後検査ではなく，実機へ持ち込むプログラムの回帰．
# 実機のコアと同じ C をホスト上で走らせて確かめる．
set -euo pipefail
cd "$(dirname "$0")/../.."

echo "-- LCD デモの描画"
bash verif/riscv/render_check.sh 3

echo "-- FAT32 リーダ"
bash verif/riscv/fat32_check.sh

echo "-- XMODEM receiver"
bash verif/riscv/xmodem_check.sh

echo "-- demo placement guards"
if output=$(bash software/build_demo.sh tfwrite psram 2>&1); then
    echo "ERROR: tfwrite psram placement was accepted" >&2
    exit 1
fi
case "$output" in
    *"overlaps its receive buffer"*) ;;
    *) echo "ERROR: unexpected tfwrite psram rejection: $output" >&2; exit 1 ;;
esac
