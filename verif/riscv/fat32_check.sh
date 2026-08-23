#!/usr/bin/env bash
# software/fat32.c をホスト上で検証する (docs/riscv.md「TF カードからのブート」)
# コンテナ内での実行を前提とする:
#   .\scripts\fpga-run.ps1 bash verif/riscv/fat32_check.sh
#
# 実機のコアと同じ C を，同じテストイメージ (RTL の Fat32Reader が
# 使うものと同一の生成器) に対して走らせる．
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=build/fat32_check
mkdir -p "$OUT" build/tf_test

/opt/oss-cad-suite/py3bin/python3 scripts/gen_tf_test_image.py --raw build/tf_test

gcc -std=c99 -O2 -Wall -Wextra \
    -o "$OUT/fat32_host" verif/riscv/fat32_host.c software/fat32.c

"$OUT/fat32_host" build/tf_test/sf.img build/tf_test/mbr.img
