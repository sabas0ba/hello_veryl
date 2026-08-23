// riscv-tests (rv32ui) を CSR なしのコアで実行するための最小テスト環境
// (docs/riscv.md「検証」)．
//
// 標準の env/p は起動時に mtvec / mcause / mhartid を触るため Zicsr を要求する．
// 本コアは Zicsr を段階 9 で実装する予定であり，それまで rv32ui を回せなくなる．
// そこで環境 (ブート・終了通知) のみ自前に置き換える．
//
// **テスト本体 (isa/rv64ui/*.S と isa/macros/scalar/test_macros.h) は改変しない**．
// 命令解釈のオラクルはテスト本体であり，環境は足場に過ぎないため，
// 差し替えても外部オラクルとしての性質は保たれる．
//
// 終了通知: MMIO_TOHOST へストアする．値の規約は riscv-tests と同じで，
// 1 = pass，それ以外は (テスト番号 << 1) | 1 = fail．
#ifndef _ENV_HELLO_VERYL_H
#define _ENV_HELLO_VERYL_H

#define TESTNUM gp

// テストベンチ / MMIO が監視するアドレス (docs/riscv.md「メモリマップ」)
#define MMIO_TOHOST 0x20000000

#define RVTEST_RV32U
#define RVTEST_RV64U
#define RVTEST_FP_ENABLE
#define RVTEST_VEC_ENABLE

#define RVTEST_CODE_BEGIN                                               \
        .section .text.init;                                            \
        .align 6;                                                       \
        .globl _start;                                                  \
_start:

#define RVTEST_CODE_END

#define RVTEST_PASS                                                     \
        li a0, 1;                                                       \
        li a1, MMIO_TOHOST;                                             \
        sw a0, 0(a1);                                                   \
1:      j 1b;

#define RVTEST_FAIL                                                     \
        slli a0, TESTNUM, 1;                                            \
        ori  a0, a0, 1;                                                 \
        li a1, MMIO_TOHOST;                                             \
        sw a0, 0(a1);                                                   \
1:      j 1b;

#define RVTEST_DATA_BEGIN .align 4; .global begin_signature; begin_signature:
#define RVTEST_DATA_END   .align 4; .global end_signature; end_signature:

#endif
