#!/usr/bin/env bash
# RISC-V まわりのソフトウェア側検査 (verif/run.sh から呼ばれる)
#
# 合成後検査ではなく，実機へ持ち込むプログラムの回帰．
# いまのところ LCD デモの描画チェックだけを含む．
set -euo pipefail
cd "$(dirname "$0")/../.."

bash verif/riscv/render_check.sh 3
