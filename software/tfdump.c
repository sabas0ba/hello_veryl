/* TF カードのセクタリーダ (RvTfSector) を実機で確認する
 * (docs/riscv.md「TF カードからのブート」)
 *
 * 初期化完了を待ち，LBA 0 を 1 セクタ読んで先頭 32 バイトと
 * ブートシグネチャ (オフセット 510 の 0xAA55) を UART へ出す．
 * FAT32 の解釈はここでは行わない．
 *
 * 戻り値 (モニタが R<8 桁 hex> で報告する):
 *   0 = シグネチャまで確認できた
 *   1 = 初期化がタイムアウトした
 *   2 = セクタ読み出しでエラー
 *   3 = ブートシグネチャが違う
 */

#define MMIO_UART    0x20000010 /* W: 送信バイト / R: bit0 = 送信可 */
#define MMIO_MTIME   0x20000030 /* R: 自走カウンタ下位 32 bit */
#define MMIO_TF_CTRL 0x20000050 /* W: bit0 = 開始 / R: 状態語 */
#define MMIO_TF_LBA  0x20000054 /* R/W: 読み出す LBA */
#define TF_BUF       0x20001000 /* セクタバッファ (512 byte, リードのみ) */

#define TF_BUSY      0x00000001u
#define TF_INIT_DONE 0x00000002u
#define TF_INIT_ERR  0x0000001cu
#define TF_ERR       0x000000e0u

/* 27 MHz なので 2 秒ぶん */
#define INIT_TIMEOUT 54000000u

static unsigned int mmio_read(unsigned int addr)
{
    return *(volatile unsigned int *) addr;
}

static void mmio_write(unsigned int addr, unsigned int val)
{
    *(volatile unsigned int *) addr = val;
}

static void put_char(int c)
{
    while ((mmio_read(MMIO_UART) & 1u) == 0u) {
    }
    mmio_write(MMIO_UART, (unsigned int) c);
}

static void put_str(const char *s)
{
    while (*s != '\0') {
        put_char(*s++);
    }
}

static void put_hex(unsigned int v, int digits)
{
    int i;

    for (i = digits - 1; i >= 0; i--) {
        int d = (int) ((v >> (i * 4)) & 0xfu);
        put_char(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

int main(void)
{
    unsigned int st, t0, sig;
    int          i;

    put_str("TF\r\n");

    t0 = mmio_read(MMIO_MTIME);
    for (;;) {
        st = mmio_read(MMIO_TF_CTRL);
        if ((st & TF_INIT_DONE) != 0u) {
            break;
        }
        if (mmio_read(MMIO_MTIME) - t0 > INIT_TIMEOUT) {
            put_str("INIT TIMEOUT st=");
            put_hex(st, 8);
            put_str("\r\n");
            return 1;
        }
    }
    put_str("INIT OK st=");
    put_hex(st, 8);
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
