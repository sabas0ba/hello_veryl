// riscv-tests を実行するための最小テスト環境 (docs/riscv.md「検証」)
//
// 上流の env/p は起動時に satp / medeleg / mideleg / pmpaddr など
// S モード・PMP の CSR を触るため，M モードのみの本コアでは使えない．
// そこで環境 (ブート・トラップベクタ・終了通知) だけを自前に置き換える．
//
// **テスト本体 (isa/rv64*/*.S と isa/macros/scalar/test_macros.h) は改変しない**．
// 命令解釈のオラクルはテスト本体であり，環境は足場に過ぎないため，
// 差し替えても外部オラクルとしての性質は保たれる．
//
// 終了通知: MMIO_TOHOST へストアする．値の規約は riscv-tests と同じで，
// 1 = pass，それ以外は (テスト番号 << 1) | 1 = fail．
// 処理できないトラップは 1337 を書く．
#ifndef _ENV_HELLO_VERYL_H
#define _ENV_HELLO_VERYL_H

// riscv-tests 本体が使う定数 (MSTATUS_*, CAUSE_* 等) は上流の encoding.h を使う
#include "encoding.h"

#define TESTNUM gp

// テストベンチ / MMIO が監視するアドレス (docs/riscv.md「メモリマップ」)
#define MMIO_TOHOST 0x20000000

#define RVTEST_RV32U
#define RVTEST_RV64U
#define RVTEST_RV32M
#define RVTEST_RV64M
#define RVTEST_RV32S
#define RVTEST_RV64S
#define RVTEST_FP_ENABLE
#define RVTEST_VEC_ENABLE

// 先頭は reset_vector へ飛び，トラップベクタを跨ぐ (上流 env/p と同じ構造)．
// rv32mi はテスト側で mtvec_handler を定義し，環境の trap_vector から
// 飛ばされることを期待する．定義がなければ弱シンボルが 0 になるため，
// 処理不能なトラップとして失敗を通知する．
#define RVTEST_CODE_BEGIN                                               \
        .section .text.init;                                            \
        .align 6;                                                       \
        .weak mtvec_handler;                                            \
        .globl _start;                                                  \
_start:                                                                 \
        j reset_vector;                                                 \
        .align 6;                                                       \
trap_vector:                                                            \
        la t5, mtvec_handler;                                           \
        beqz t5, other_exception;                                       \
        jr t5;                                                          \
other_exception:                                                        \
        li a0, 1337;                                                    \
        li a1, MMIO_TOHOST;                                             \
        sw a0, 0(a1);                                                   \
1:      j 1b;                                                           \
reset_vector:                                                           \
        la t0, trap_vector;                                             \
        csrw mtvec, t0;

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
