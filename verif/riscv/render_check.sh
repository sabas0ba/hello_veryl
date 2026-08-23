#!/usr/bin/env bash
# software/torus.c の描画をホスト上で実行して確認する
# (docs/riscv.md「LCD デモ」)．コンテナ内での実行を前提とする:
#   .\scripts\fpga-run.ps1 bash verif/riscv/render_check.sh
#
# 実機のコアと同じ整数演算だけで書かれているため，ホストで描いた絵柄は
# 実機の出力と一致する (符号なし右シフト等の実装依存を使っていない)．
# 目視用にフレームを出しつつ，退化 (真っ白・真っ黒・片寄り) を機械的に弾く．
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=build/render_check
mkdir -p "$OUT"
FRAMES="${1:-4}"

gcc -std=c99 -O2 -Wall -Wextra -DHOST -DHOST_FRAMES="$FRAMES" \
    -o "$OUT/torus_host" software/torus.c
"$OUT/torus_host" > "$OUT/frames.txt"

W=100
H=29

lines=$(wc -l < "$OUT/frames.txt")
want=$((FRAMES * H))
if [ "$lines" -ne "$want" ]; then
    echo "ERROR: 行数が $lines (期待 $want)" >&2
    exit 1
fi

fail=0
for f in $(seq 0 $((FRAMES - 1))); do
    from=$((f * H + 1))
    to=$(((f + 1) * H))
    frame=$(sed -n "${from},${to}p" "$OUT/frames.txt")

    ink=$(printf '%s\n' "$frame" | tr -d ' \n' | wc -c)
    # 画面 2900 セルのうち，トーラスが占める妥当な範囲
    if [ "$ink" -lt 200 ] || [ "$ink" -gt 2000 ]; then
        echo "ERROR: frame $f の描画セル数が $ink (200..2000 を期待)" >&2
        fail=1
    fi

    # 濃淡が使われていること (単一文字で塗り潰されていない)
    shades=$(printf '%s\n' "$frame" | tr -d ' \n' | fold -w1 | sort -u | wc -l)
    if [ "$shades" -lt 4 ]; then
        echo "ERROR: frame $f の濃淡が $shades 種類 (4 種類以上を期待)" >&2
        fail=1
    fi

    # 行の長さが桁数どおり (折り返しで改行させるため厳密に W である必要がある)
    badlen=$(printf '%s\n' "$frame" | awk -v w="$W" 'length($0) != w' | wc -l)
    if [ "$badlen" -ne 0 ]; then
        echo "ERROR: frame $f に長さ $W でない行が $badlen 行ある" >&2
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    exit 1
fi

echo "---- frame 0 ----"
sed -n "1,${H}p" "$OUT/frames.txt"
echo "OK: $FRAMES フレームすべて妥当 (build/render_check/frames.txt)"
exit 0
