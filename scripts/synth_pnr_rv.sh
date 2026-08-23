#!/usr/bin/env bash
# TopRv (RV32I ソフトコア) の合成 -> PnR -> ビットストリーム生成
# コンテナ内で repo ルート (/work) を cwd として実行する．
# 既存 Top 用は synth_pnr.sh．TopRv は PLL を持たないためクロックは 27 MHz 単一で，
# --sdc によるクロック別制約は不要．
set -euo pipefail

yosys -s scripts/synth_rv.ys

nextpnr-himbaechel \
    --json build/top_rv_synth.json \
    --write build/top_rv_pnr.json \
    --device 'GW1NR-LV9QN88PC6/I5' \
    --vopt family=GW1N-9C \
    --vopt cst=constraints/tangnano9k_rv.cst \
    --freq 27 2>&1 | tee build/nextpnr_rv.log

gowin_pack -d GW1N-9C -o build/top_rv.fs build/top_rv_pnr.json

echo "Bitstream generated: build/top_rv.fs"
