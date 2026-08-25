/* TF カードの byte-stream 入出力 (RvTfIo) のドライバ
 * (docs/riscv.md「TF カードからのブート」) */
#ifndef TFDEV_H
#define TFDEV_H

#define MMIO_TF_CTRL 0x20000050 /* W: bit0 = 開始, bit1 = 方向 / R: 状態語 */
#define MMIO_TF_LBA  0x20000054 /* R/W: 入出力する LBA */
#define MMIO_TF_DATA 0x20000058 /* R/W: 1 byte の stream data */

#define TF_BUSY      0x00000001u
#define TF_INIT_DONE 0x00000002u
#define TF_INIT_ERR  0x0000001cu
#define TF_ERR       0x000000e0u
#define TF_RX_VALID  0x00000100u
#define TF_TX_SPACE  0x00000200u

/* 初期化完了を待つ．0 = 完了，1 = タイムアウト */
int tf_wait_init(void);

#endif
