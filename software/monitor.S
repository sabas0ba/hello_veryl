# UART ブートモニタ (docs/riscv.md「実機での riscv-tests 実行」)
#
# ROM から起動し，UART で受け取ったプログラムを PSRAM へ書いて実行する．
# riscv-tests を 1 件ずつ流し込んで実機で回すための足場．
#
# プロトコル (ホスト <-> ボード):
#   起動時    : ボードが "RVMON\r\n" を送る
#   繰り返し  : ボードが '>' を送る (受付可)
#               ホストが語数 (4 バイト little endian) を送る
#               ホストが語数ぶんのプログラム (4 バイト little endian x N) を送る
#               ボードは 0x1000_0000 へ書いてそこへジャンプする
#               テストは終了時に 0x0000_0004 へ戻る (テスト環境の PASS/FAIL)
#               ボードが "R" + 結果 8 桁 hex + CRLF を送る
#
# テストからの復帰点はアドレス 0x0000_0004 に固定する．復帰時に生きている
# レジスタは結果を保持する a0 のみとみなし，他は再初期化する．

    .equ MMIO_UART,    0x20000010     # W: 送信バイト / R: bit0 = 送信可
    .equ MMIO_UART_RX, 0x20000014     # R: bit8 = 受信あり, bit7-0 = データ
    .equ MMIO_LED,     0x20000020
    .equ LOAD_BASE,    0x10000000

    .section .text.init
    .globl _start
_start:
    j    cold_boot                    # 0x00
test_return:
    j    report                       # 0x04 テストからの復帰点

cold_boot:
    la   s0, msg_banner
1:  lbu  a0, 0(s0)
    beqz a0, main_loop
    li   a1, MMIO_UART
2:  lw   a2, 0(a1)
    andi a2, a2, 1
    beqz a2, 2b
    sw   a0, 0(a1)
    addi s0, s0, 1
    j    1b

main_loop:
    li   a0, 0x3e                     # '>' 受付可
    li   a1, MMIO_UART
1:  lw   a2, 0(a1)
    andi a2, a2, 1
    beqz a2, 1b
    sw   a0, 0(a1)

    # 語数を 4 バイト受信する
    li   t0, 4
    li   t1, 0
    li   t2, 0
2:  li   a1, MMIO_UART_RX
3:  lw   a2, 0(a1)
    andi a3, a2, 0x100
    beqz a3, 3b
    andi a2, a2, 0xff
    sll  a2, a2, t2
    or   t1, t1, a2
    addi t2, t2, 8
    addi t0, t0, -1
    bnez t0, 2b

    beqz t1, main_loop
    li   s3, LOAD_BASE
    mv   s4, s3

load_loop:
    li   t0, 4
    li   t3, 0
    li   t2, 0
4:  li   a1, MMIO_UART_RX
5:  lw   a2, 0(a1)
    andi a3, a2, 0x100
    beqz a3, 5b
    andi a2, a2, 0xff
    sll  a2, a2, t2
    or   t3, t3, a2
    addi t2, t2, 8
    addi t0, t0, -1
    bnez t0, 4b
    sw   t3, 0(s4)
    addi s4, s4, 4
    addi t1, t1, -1
    bnez t1, load_loop

    jr   s3                           # ロードしたプログラムへ

report:
    mv   s5, a0                       # 結果を退避 (a0 のみ有効)
    li   a0, 0x52                     # 'R'
    li   a1, MMIO_UART
1:  lw   a2, 0(a1)
    andi a2, a2, 1
    beqz a2, 1b
    sw   a0, 0(a1)

    li   t0, 8                        # 8 桁の hex
    li   t2, 28
2:  srl  a0, s5, t2
    andi a0, a0, 0xf
    li   a3, 10
    blt  a0, a3, 3f
    addi a0, a0, 0x37                 # 'A' - 10
    j    4f
3:  addi a0, a0, 0x30                 # '0'
4:  li   a1, MMIO_UART
5:  lw   a2, 0(a1)
    andi a2, a2, 1
    beqz a2, 5b
    sw   a0, 0(a1)
    addi t2, t2, -4
    addi t0, t0, -1
    bnez t0, 2b

    li   a0, 13
    li   a1, MMIO_UART
6:  lw   a2, 0(a1)
    andi a2, a2, 1
    beqz a2, 6b
    sw   a0, 0(a1)
    li   a0, 10
7:  lw   a2, 0(a1)
    andi a2, a2, 1
    beqz a2, 7b
    sw   a0, 0(a1)

    j    main_loop

msg_banner:
    .asciz "RVMON\r\n"
