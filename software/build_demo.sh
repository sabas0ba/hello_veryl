#!/usr/bin/env bash
# UART ブートモニタへ流し込むデモプログラムをビルドする
# (docs/riscv.md「LCD デモ」)．コンテナ内での実行を前提とする:
#   .\scripts\fpga-run.ps1 bash software/build_demo.sh torus [ram|psram]
#
# 生成物 build/software/<name>.bin を scripts/rv-load.ps1 で流し込む．
# ビットストリームの再生成は不要．
#
# 配置 (既定 ram):
#   ram   : イメージを PSRAM で受け取り，オンチップ RAM (8 KB) へ写して実行する．
#           PSRAM は 1 アクセスに数 us かかり命令フェッチが律速になるため，
#           ループの多い処理はこちらが 1 桁速い
#   psram : PSRAM 上でそのまま実行する．8 KB に収まらない場合に使う
#
# software/<name>.srcs があれば，そこに 1 行 1 個で書かれた追加ソースも
# 一緒にコンパイルする（FAT32 リーダなど共有部分のため）．
#
# -mno-relax / -msmall-data-limit=0 は gp 相対アドレッシングを禁じるため．
# 起動コードは gp を設定しないので，緩和されると静的変数の参照が壊れる．
set -euo pipefail
cd "$(dirname "$0")/.."

PROG="${1:-torus}"
PLACE="${2:-ram}"
if [ ! -f "software/$PROG.c" ]; then
    echo "ERROR: software/$PROG.c が無い" >&2
    exit 1
fi
case "$PLACE" in
    ram)   CRT=software/crt0_ram.S ; LD=software/link_ram.ld   ;;
    psram) CRT=software/crt0.S     ; LD=software/link_psram.ld ;;
    *)     echo "ERROR: 未知の配置 '$PLACE' (ram|psram)" >&2; exit 1 ;;
esac
if [ "$PROG" = tfwrite ] && [ "$PLACE" != ram ]; then
    echo "ERROR: tfwrite must use ram; psram overlaps its receive buffer" >&2
    exit 1
fi

EXTRA=""
if [ -f "software/$PROG.srcs" ]; then
    while read -r line; do
        case "$line" in
            "") continue ;;
            "#"*) continue ;;
        esac
        if [ ! -f "$line" ]; then
            echo "ERROR: software/$PROG.srcs が指す $line が無い" >&2
            exit 1
        fi
        EXTRA="$EXTRA $line"
    done < "software/$PROG.srcs"
fi

OUT=build/software
mkdir -p "$OUT"

OPT=-O2
SIZE_FLAGS=
if [ "$PROG" = tfwrite ]; then
    # FAT rollback paths make this image size-bound; TF/UART I/O dominates
    # runtime, so optimize this receiver for the 8 KB on-chip RAM instead.
    OPT=-Os
    SIZE_FLAGS="-ffunction-sections -fdata-sections -Wl,--gc-sections"
fi

riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32 "$OPT" $SIZE_FLAGS \
    -nostdlib -nostartfiles -ffreestanding \
    -mno-relax -msmall-data-limit=0 \
    -Wall -Wextra \
    -T "$LD" -o "$OUT/$PROG.elf" \
    "$CRT" "software/$PROG.c" $EXTRA
riscv64-unknown-elf-objcopy -O binary "$OUT/$PROG.elf" "$OUT/$PROG.bin"
riscv64-unknown-elf-objdump -d "$OUT/$PROG.elf" > "$OUT/$PROG.dis"

echo "program: $PROG ($PLACE)  size: $(stat -c %s "$OUT/$PROG.bin") byte"
riscv64-unknown-elf-size "$OUT/$PROG.elf"
exit 0
