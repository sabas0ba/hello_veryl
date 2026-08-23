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
