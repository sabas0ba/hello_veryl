# RV32I ソフトコアの実機ブリングアップ用プログラム (docs/riscv.md パッチ計画 #6)
#
# UART へ 1 行出力し，その後 LED を点滅させて生存を示す．
# MMIO のアドレスは docs/riscv.md「メモリマップ」と src/riscv/mmio.veryl に従う．

    .equ MMIO_UART, 0x20000010      # W: 送信バイト / R: bit0 = 送信可
    .equ MMIO_LED,  0x20000020      # W: 下位 6 bit

    .section .text.init
    .globl _start
_start:
    la   s0, msg
    li   s1, MMIO_UART

putc_loop:
    lbu  a0, 0(s0)
    beqz a0, msg_done
wait_tx:
    lw   a1, 0(s1)
    andi a1, a1, 1
    beqz a1, wait_tx            # 送信可になるまで待つ
    sw   a0, 0(s1)
    addi s0, s0, 1
    j    putc_loop

msg_done:
    li   t0, MMIO_LED
    li   t1, 0x3f

blink:
    sw   t1, 0(t0)
    xori t1, t1, 0x3f           # 全点灯 / 全消灯を交互に
    li   t2, 1000000            # 27 MHz・多サイクル実行で 1 秒弱
delay:
    addi t2, t2, -1
    bnez t2, delay
    j    blink

msg:
    .asciz "Hello from the RV32I core on Tang Nano 9K!\r\n"
