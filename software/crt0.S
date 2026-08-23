# PSRAM へロードして実行する C プログラムの起動コード
# (docs/riscv.md「LCD デモ」)
#
# UART ブートモニタ (software/monitor.S) は 0x1000_0000 へプログラムを置いて
# そこへ分岐する．スタックポインタは設定されないため，ここで用意する．
#
# スタックはオンチップのデータ RAM (0x0000_2000 から RamWords x 4 byte) の
# 末尾へ置く．PSRAM より 1 桁速いため，関数呼び出しの多い C コードでは
# ここに置くのが効く．

    .equ STACK_TOP,    0x00002800     # RamWords=512 -> 0x2000..0x27ff
    .equ MONITOR_ENTRY, 0x00000004    # モニタの復帰点

    .section .text.init
    .globl _start
_start:
    li   sp, STACK_TOP
    la   t0, __bss_start
    la   t1, __bss_end
1:  bgeu t0, t1, 2f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    1b
2:  call main                         # 戻り値 a0 がモニタの報告値になる
    li   t0, MONITOR_ENTRY
    jr   t0
