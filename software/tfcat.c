/* TF カード上の FAT32 からファイルを 1 つ見つけて中身の先頭を出す
 * (docs/riscv.md「TF カードからのブート」)
 *
 * software/fat32.c を実機で確認するための診断プログラム．
 * 探すファイル名は FILE_NAME (8.3 名 11 文字) で固定する．
 *
 * 戻り値 (モニタが R<8 桁 hex> で報告する):
 *   0 = 読めた / 1 = TF 初期化タイムアウト / 2 = mount 失敗
 *   3 = ファイルが見つからない / 4 = 読み出し失敗
 */
#include "fat32.h"
#include "tfdev.h"
#include "uart.h"

/* 画像デモが使うファイル．カードに既にある前提 (docs/tfcard.md) */
#define FILE_NAME "IMAGE   BMP"
#define DUMP_LEN  32

static unsigned char buf[DUMP_LEN];

int main(void)
{
    fat_file     f;
    unsigned int got = 0;
    int          rc, i;

    put_str("TFCAT\r\n");

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
    put_str("MOUNT OK\r\n");

    rc = fat_open(FILE_NAME, &f);
    if (rc != FAT_OK) {
        put_str("NOT FOUND ");
        put_dec((unsigned int) rc);
        put_str("\r\n");
        return 3;
    }
    put_str("FOUND clus=");
    put_hex(f.start_cluster, 8);
    put_str(" size=");
    put_dec(f.size);
    put_str("\r\n");

    rc = fat_read(&f, buf, DUMP_LEN, &got);
    if (rc != FAT_OK) {
        put_str("READ ERR ");
        put_dec((unsigned int) rc);
        put_str("\r\n");
        return 4;
    }

    put_str("HEAD:");
    for (i = 0; i < (int) got; i++) {
        put_char(' ');
        put_hex(buf[i], 2);
    }
    put_str("\r\n");
    return 0;
}
