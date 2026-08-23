/* TF カードのファイルを PSRAM へ読み込む経路を実機で確認する
 * (docs/riscv.md「TF カードからのブート」)
 *
 * tfboot と同じ経路（TF セクタリーダ → FAT32 → PSRAM へ書き込み）を
 * 実際のカードにある既知のファイルで通し，PSRAM から読み返して検証する．
 * BOOT.BIN を用意しなくてもロード経路を確かめられる．
 *
 * 検証内容（BMP の独立した複数フィールドが互いに整合することを見る）:
 *   - 読めたバイト数がディレクトリエントリのサイズと一致する
 *   - PSRAM から読み返した先頭が "BM"
 *   - ヘッダの bfSize が読めたバイト数と一致する
 *   - 幅・高さ・bpp から計算した画像サイズ + ヘッダ長が一致する
 *
 * 戻り値 (モニタが R<8 桁 hex> で報告する):
 *   0 = すべて整合 / 1 = TF 初期化タイムアウト / 2 = mount 失敗
 *   3 = ファイルが見つからない / 4 = 読み出し失敗 / 5 = 検証で不一致
 */
#include "fat32.h"
#include "tfdev.h"
#include "uart.h"

#define FILE_NAME "IMAGE   BMP"
#define LOAD_BASE 0x10000000u
#define LOAD_MAX  0x00400000u

static unsigned int ld8(unsigned int off)
{
    return (unsigned int) *(volatile unsigned char *) (LOAD_BASE + off);
}

static unsigned int ld16(unsigned int off)
{
    return ld8(off) | (ld8(off + 1) << 8);
}

static unsigned int ld32(unsigned int off)
{
    return ld16(off) | (ld16(off + 2) << 16);
}

int main(void)
{
    fat_file     f;
    unsigned int got = 0;
    unsigned int bf_size, hdr_off, w, h, bpp, row, want;
    int          rc, bad = 0;

    put_str("TFLOAD\r\n");

    if (tf_wait_init() != 0) {
        put_str("INIT TIMEOUT\r\n");
        return 1;
    }
    rc = fat_mount();
    if (rc != FAT_OK) {
        put_str("MOUNT ERR ");
        put_dec((unsigned int) rc);
        put_str("\r\n");
        return 2;
    }
    rc = fat_open(FILE_NAME, &f);
    if (rc != FAT_OK) {
        put_str("NOT FOUND\r\n");
        return 3;
    }

    put_str("LOAD ");
    put_dec(f.size);
    put_str(" byte -> psram\r\n");

    rc = fat_read(&f, (void *) LOAD_BASE, LOAD_MAX, &got);
    if (rc != FAT_OK) {
        put_str("READ ERR ");
        put_dec((unsigned int) rc);
        put_str("\r\n");
        return 4;
    }
    if (got != f.size) {
        put_str("SHORT ");
        put_dec(got);
        put_str("\r\n");
        bad = 1;
    }

    /* ここから先は PSRAM から読み返した値だけで判定する */
    if (ld8(0) != 0x42u || ld8(1) != 0x4du) {
        put_str("NOT BM\r\n");
        bad = 1;
    }
    bf_size = ld32(2);
    hdr_off = ld32(10);
    w       = ld32(18);
    h       = ld32(22);
    bpp     = ld16(28);

    put_str("bfSize=");
    put_dec(bf_size);
    put_str(" off=");
    put_dec(hdr_off);
    put_str(" w=");
    put_dec(w);
    put_str(" h=");
    put_dec(h);
    put_str(" bpp=");
    put_dec(bpp);
    put_str("\r\n");

    if (bf_size != got) {
        put_str("bfSize MISMATCH\r\n");
        bad = 1;
    }
    row  = (w * (bpp / 8u) + 3u) & ~3u;
    want = hdr_off + row * h;
    if (want != got) {
        put_str("SIZE MISMATCH want=");
        put_dec(want);
        put_str("\r\n");
        bad = 1;
    }

    if (bad != 0) {
        return 5;
    }
    put_str("OK\r\n");
    return 0;
}
