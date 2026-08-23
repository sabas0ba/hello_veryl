#!/usr/bin/env bash
# TopRv (RV32I ソフトコア) の合成 -> PnR -> ビットストリーム生成
# コンテナ内で repo ルート (/work) を cwd として実行する．既存 Top 用は synth_pnr.sh．
#
# --freq は全クロック一律のため，PsramSubsystem 内の rPLL 出力 clk_mem (54 MHz) は
# --sdc で個別に制約する (docs/psram.md「クロック別タイミング制約」)．
set -euo pipefail

yosys -s scripts/synth_rv.ys

nextpnr-himbaechel \
    --json build/top_rv_synth.json \
    --write build/top_rv_pnr.json \
    --device 'GW1NR-LV9QN88PC6/I5' \
    --vopt family=GW1N-9C \
    --vopt cst=constraints/tangnano9k_rv.cst \
    --sdc constraints/tangnano9k_rv.sdc \
    --freq 27 2>&1 | tee build/nextpnr_rv.log

# マッチしない create_clock は nextpnr が警告なく無視するため，SDC が
# clk_mem ネットに実際にマッチしたことをログで確認する (制約の空振り検出)
if ! grep -qF "constraining clock net 'psram.clk_mem' to 54.00 MHz" build/nextpnr_rv.log; then
    echo "ERROR: clk_mem の 54 MHz 制約が適用されていない (SDC のネット名を確認)" >&2
    exit 1
fi

gowin_pack -d GW1N-9C -o build/top_rv.fs build/top_rv_pnr.json

echo "Bitstream generated: build/top_rv.fs"
