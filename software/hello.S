# RV32IM ソフトコアの実機ブリングアップ用プログラム
# (docs/riscv.md パッチ計画 #6, #7, #8)
#
# 1. UART へ挨拶を 1 行出力する
# 2. PSRAM へパターンを書いて読み返し，一致を確認して結果を 1 行出力する
# 3. M 拡張 (乗除算) の代表値とゼロ除算の規定値を確認し結果を 1 行出力する
# 4. LED を点滅させて生存を示す
#
# 同じプログラムを L1 (test_rv_soc，PSRAM は AXI メモリモデルで代替) と
# L4 (実機) の双方で走らせる．
#
# MMIO / PSRAM のアドレスは docs/riscv.md「メモリマップ」と
# src/riscv/soc.veryl のデコードに従う．
#
# レジスタはリセットで初期化されない (RISC-V 仕様も初期値を定めない．
# docs/riscv.md「レジスタファイルの実装」) ため，使用前に必ず設定する．

    .equ MMIO_UART,  0x20000010       # W: 送信バイト / R: bit0 = 送信可
    .equ MMIO_LED,   0x20000020       # W: 下位 6 bit
    .equ PSRAM_BASE, 0x10000000
    .equ TEST_WORDS, 64

    .section .text.init
    .globl _start
_start:
    la   a0, msg_hello
    jal  ra, puts

# ---- PSRAM 書き込み: パターン = seed ^ index ----
    li   t0, PSRAM_BASE
    li   t1, TEST_WORDS
    li   t2, 0
    li   t3, 0x12345678
wr_loop:
    slli t4, t2, 2
    add  t4, t0, t4
    xor  t5, t3, t2
    sw   t5, 0(t4)
    addi t2, t2, 1
    blt  t2, t1, wr_loop

# ---- 読み返して照合．不一致数を s2 に数える ----
    li   t2, 0
    li   s2, 0
rd_loop:
    slli t4, t2, 2
    add  t4, t0, t4
    lw   t6, 0(t4)
    xor  t5, t3, t2
    beq  t6, t5, rd_next
    addi s2, s2, 1
rd_next:
    addi t2, t2, 1
    blt  t2, t1, rd_loop

    beqz s2, psram_ok
    la   a0, msg_ps_ng
    jal  ra, puts
    j    blink
psram_ok:
    la   a0, msg_ps_ok
    jal  ra, puts

# ---- M 拡張 (乗除算) の確認 ----
    li   t0, 123456
    li   t1, 7891
    mul  t2, t0, t1                   # 974191296
    li   t3, 974191296
    bne  t2, t3, md_ng
    div  t4, t2, t1                   # 元の 123456 へ戻る
    bne  t4, t0, md_ng
    li   t0, -1000
    li   t1, 3
    div  t2, t0, t1                   # 0 方向へ切り捨て -> -333
    li   t3, -333
    bne  t2, t3, md_ng
    rem  t2, t0, t1                   # 剰余の符号は被除数に従う -> -1
    li   t3, -1
    bne  t2, t3, md_ng
    li   t1, 0
    div  t2, t0, t1                   # ゼロ除算: DIV は -1
    li   t3, -1
    bne  t2, t3, md_ng
    rem  t2, t0, t1                   # ゼロ除算: REM は被除数
    li   t3, -1000
    bne  t2, t3, md_ng
    la   a0, msg_md_ok
    jal  ra, puts
    j    blink
md_ng:
    la   a0, msg_md_ng
    jal  ra, puts

# ---- 生存表示 ----
blink:
    li   s3, MMIO_LED
    li   s4, 0x3f
blink_loop:
    sw   s4, 0(s3)
    xori s4, s4, 0x3f                 # 全点灯 / 全消灯を交互に
    li   s5, 1000000                  # 27 MHz・多サイクル実行で 1 秒弱
delay:
    addi s5, s5, -1
    bnez s5, delay
    j    blink_loop

# ---- puts: a0 = NUL 終端文字列．a0-a3 を破壊する ----
puts:
    li   a2, MMIO_UART
puts_loop:
    lbu  a1, 0(a0)
    beqz a1, puts_done
puts_wait:
    lw   a3, 0(a2)
    andi a3, a3, 1
    beqz a3, puts_wait                # 送信可になるまで待つ
    sw   a1, 0(a2)
    addi a0, a0, 1
    j    puts_loop
puts_done:
    ret

msg_hello:
    .asciz "Hello from the RV32I core on Tang Nano 9K!\r\n"
msg_ps_ok:
    .asciz "PSRAM OK\r\n"
msg_ps_ng:
    .asciz "PSRAM NG\r\n"
msg_md_ok:
    .asciz "MULDIV OK\r\n"
msg_md_ng:
    .asciz "MULDIV NG\r\n"
