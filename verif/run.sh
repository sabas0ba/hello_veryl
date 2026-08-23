#!/usr/bin/env bash
# 合成後検査 (verif/) の一括ランナー．リグレッションの一部として実行する:
#   ローカル: .\scripts\verif.ps1
#   CI      : .github/workflows/ci.yml の verif ジョブ
# FPGA ツールチェーンコンテナ (container/Containerfile) 内での実行を前提とする
set -euo pipefail
cd "$(dirname "$0")/.."

# トランスパイルとソースリスト生成 (synth スクリプトは repo ルート相対で参照)
# build/ はクリーンチェックアウト (CI) には存在しないため先に作る
mkdir -p build
veryl build
sed 's|^/work/||' hello_veryl.f > build/sources.f

status=0
for check in verif/*/check.sh; do
    suite=$(basename "$(dirname "$check")")
    echo "==== verif: $suite ===="
    if bash "$check"; then
        echo "==== verif: $suite PASS ===="
    else
        echo "==== verif: $suite FAIL ===="
        status=1
    fi
done
exit $status
