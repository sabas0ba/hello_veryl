# PSRAM 経由で受け取り，オンチップ RAM へ移して実行する C プログラムの起動コード
# (docs/riscv.md「LCD デモ」)
#
# UART ブートモニタ (software/monitor.S) はプログラムを PSRAM の
# 0x1000_0000 へ置いてそこへ分岐する．PSRAM は 1 アクセスが数 us かかり，
# 命令フェッチが律速になるため，イメージをオンチップ RAM (0x0000_2000 から
# 8 KB) へ写してからそこで実行する．
#
# 先頭の複写部だけは PSRAM 上で実行される．リンク上の番地 (0x0000_2000 台) と
# 実行番地 (0x1000_0000 台) が食い違うので，ここでは PC 相対アドレッシングを
# 使わない．絶対アドレスの lui/addi と相対分岐だけで書く
# (ビルドは -mno-relax なので lui/addi が緩和されることもない)．

    .equ LOAD_BASE,     0x10000000    # モニタがイメージを置く番地
    .equ RAM_BASE,      0x00002000    # オンチップ RAM の先頭
    .equ MONITOR_ENTRY, 0x00000004    # モニタの復帰点

    .section .text.boot
    .globl _start
_start:
    li   t0, LOAD_BASE
    li   t1, RAM_BASE
    lui  t2, %hi(__image_words)
    addi t2, t2, %lo(__image_words)
1:  lw   t3, 0(t0)
    sw   t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    addi t2, t2, -1
    bnez t2, 1b
    lui  t0, %hi(_ram_entry)
    addi t0, t0, %lo(_ram_entry)
    jr   t0

    .section .text.init
    .globl _ram_entry
_ram_entry:
    lui  sp, %hi(__stack_top)
    addi sp, sp, %lo(__stack_top)
    lui  t0, %hi(__bss_start)
    addi t0, t0, %lo(__bss_start)
    lui  t1, %hi(__bss_end)
    addi t1, t1, %lo(__bss_end)
2:  bgeu t0, t1, 3f
    sw   zero, 0(t0)
    addi t0, t0, 4
    j    2b
3:  call main                         # 戻り値 a0 がモニタの報告値になる
    li   t0, MONITOR_ENTRY
    jr   t0
