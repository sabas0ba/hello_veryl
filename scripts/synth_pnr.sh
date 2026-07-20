#!/usr/bin/env bash
# Synthesis -> place & route -> bitstream, run inside the container image
# defined by container/Containerfile. cwd must be the repo root (mounted /work).
set -euo pipefail

yosys -s scripts/synth.ys

nextpnr-himbaechel \
    --json build/top_synth.json \
    --write build/top_pnr.json \
    --device 'GW1NR-LV9QN88PC6/I5' \
    --vopt family=GW1N-9C \
    --vopt cst=constraints/tangnano9k.cst \
    --freq 27

gowin_pack -d GW1N-9C -o build/top.fs build/top_pnr.json

echo "Bitstream generated: build/top.fs"
