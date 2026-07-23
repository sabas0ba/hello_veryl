#!/usr/bin/env bash
# PsramPhy 単体実証: 合成 -> PnR -> ネットリスト検査
# repo ルートから verif/run.sh 経由で実行する (build/sources.f 生成済み前提)
set -euo pipefail
mkdir -p build/psram_phy_probe

yosys -s verif/psram_phy/phy_probe.ys > build/psram_phy_probe/yosys.log 2>&1 \
    || { tail -30 build/psram_phy_probe/yosys.log; exit 1; }
echo "yosys OK"
# 未駆動ワイヤ警告は接続漏れの兆候のため失格にする
if grep -i "Warning: Wire" build/psram_phy_probe/yosys.log; then
    echo "FAIL: undriven wire warning"
    exit 1
fi

nextpnr-himbaechel \
    --json build/psram_phy_probe/phy_synth.json \
    --write build/psram_phy_probe/phy_pnr.json \
    --device 'GW1NR-LV9QN88PC6/I5' \
    --vopt family=GW1N-9C \
    --vopt cst=verif/psram_phy/phy_probe.cst \
    --freq 27 > build/psram_phy_probe/nextpnr.log 2>&1 \
    || { echo "== nextpnr FAILED =="; \
         grep -i 'error' build/psram_phy_probe/nextpnr.log | head -30; exit 1; }
echo "nextpnr OK"

# システム python はコンテナに無いため OSS CAD Suite 同梱の python を使う
/opt/oss-cad-suite/py3bin/python3 verif/psram_phy/check_netlist.py
