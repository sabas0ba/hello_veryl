/* TF カードの BOOT.BIN を PSRAM へ読み込んで実行する第一段ローダ
 * (docs/riscv.md「TF カードからのブート」)
 *
 * 読み込み先は UART ブートモニタと同じ 0x1000_0000 とする．
 * したがって software/build_demo.sh が作るイメージ (ram 配置なら
 * 先頭の複写部が自分をオンチップ RAM へ移す) をそのまま置ける．
 *
 * 戻り値 (実行へ移れなかったときだけモニタへ戻る):
 *   1 = TF 初期化タイムアウト / 2 = mount 失敗
 *   3 = BOOT.BIN が見つからない / 4 = 読み出し失敗 / 5 = サイズが 0
 */
#include "fat32.h"
#include "tfdev.h"
#include "uart.h"

#define BOOT_NAME "BOOT    BIN"
#define LOAD_BASE 0x10000000u
/* PSRAM ch0 は 4 MB．ローダ自身の作業領域は使わないので全域を許す */
#define LOAD_MAX 0x00400000u

int main(void)
{
    fat_file     f;
    unsigned int got = 0;
    int          rc;

    put_str("TFBOOT\r\n");

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

    rc = fat_open(BOOT_NAME, &f);
    if (rc != FAT_OK) {
        put_str("NO " BOOT_NAME "\r\n");
        return 3;
    }
    if (f.size == 0u) {
        put_str("EMPTY\r\n");
        return 5;
    }

    put_str("LOAD ");
    put_dec(f.size);
    put_str(" byte\r\n");

    rc = fat_read(&f, (void *) LOAD_BASE, LOAD_MAX, &got);
    if (rc != FAT_OK) {
        put_str("READ ERR ");
        put_dec((unsigned int) rc);
        put_str("\r\n");
        return 4;
    }

    put_str("GO\r\n");
    ((void (*)(void)) LOAD_BASE)();
    return 0; /* ここへは戻らない */
}
