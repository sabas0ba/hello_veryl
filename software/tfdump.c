/* TF カードのセクタリーダ (RvTfSector) を実機で確認する
 * (docs/riscv.md「TF カードからのブート」)
 *
 * FAT32 を介さない最下層の確認．初期化完了を待ち，LBA 0 を 1 セクタ読んで
 * 先頭 32 バイトとブートシグネチャ (オフセット 510 の 0xAA55) を UART へ出す．
 *
 * 戻り値 (モニタが R<8 桁 hex> で報告する):
 *   0 = シグネチャまで確認できた
 *   1 = 初期化がタイムアウトした
 *   2 = セクタ読み出しでエラー
 *   3 = ブートシグネチャが違う
 */
#include "tfdev.h"
#include "uart.h"

int main(void)
{
    unsigned int st, sig;
    int          i;

    put_str("TF\r\n");

    if (tf_wait_init() != 0) {
        put_str("INIT TIMEOUT st=");
        put_hex(mmio_read(MMIO_TF_CTRL), 8);
        put_str("\r\n");
        return 1;
    }
    put_str("INIT OK st=");
    put_hex(mmio_read(MMIO_TF_CTRL), 8);
    put_str("\r\n");

    mmio_write(MMIO_TF_LBA, 0);
    mmio_write(MMIO_TF_CTRL, 1);
    while ((mmio_read(MMIO_TF_CTRL) & TF_BUSY) != 0u) {
    }
    st = mmio_read(MMIO_TF_CTRL);
    if ((st & TF_ERR) != 0u) {
        put_str("READ ERR st=");
        put_hex(st, 8);
        put_str("\r\n");
        return 2;
    }

    put_str("LBA0:");
    for (i = 0; i < 8; i++) {
        put_char(' ');
        put_hex(mmio_read(TF_BUF + (unsigned int) i * 4), 8);
    }
    put_str("\r\n");

    /* オフセット 508..511 = ワード 127．シグネチャは 510..511 = 上位 16 bit */
    sig = mmio_read(TF_BUF + 127 * 4) >> 16;
    put_str("SIG=");
    put_hex(sig, 4);
    put_str("\r\n");
    if (sig != 0xaa55u) {
        return 3;
    }
    return 0;
}
