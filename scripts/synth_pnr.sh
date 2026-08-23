#!/usr/bin/env bash
# Synthesis -> place & route -> bitstream, run inside the container image
# defined by container/Containerfile. cwd must be the repo root (mounted /work).
set -euo pipefail

yosys -s scripts/synth.ys

# --freq は全クロック一律の目標値なので基準クロック (27 MHz) に合わせ，
# rPLL 出力 clk_mem (54 MHz) だけを SDC で個別に制約する
nextpnr-himbaechel \
    --json build/top_synth.json \
    --write build/top_pnr.json \
    --device 'GW1NR-LV9QN88PC6/I5' \
    --vopt family=GW1N-9C \
    --vopt cst=constraints/tangnano9k.cst \
    --sdc constraints/tangnano9k.sdc \
    --freq 27 2>&1 | tee build/nextpnr.log

# マッチしない create_clock は nextpnr が警告なく無視するため，SDC が
# clk_mem ネットに実際にマッチしたことをログで確認する (制約の空振り検出)
if ! grep -qF "constraining clock net 'clk_mem' to 54.00 MHz" build/nextpnr.log; then
    echo "ERROR: clk_mem の 54 MHz 制約が適用されていない (SDC のネット名を確認)" >&2
    exit 1
fi

gowin_pack -d GW1N-9C -o build/top.fs build/top_pnr.json

echo "Bitstream generated: build/top.fs"
