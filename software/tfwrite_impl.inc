/* UART (XMODEM-CRC) で受けた binary を FAT32 ファイルへ書く。
 *
 * payload は先頭 4 byte が file size (little-endian)、続く byte 列が
 * file 本体。scripts/rv-xmodem.ps1 がこの framing を作る。
 * 呼出元は出力先の 8.3 名と表示名を compile-time 定数で指定する。 */
#if !defined(TFWRITE_BANNER) || !defined(TFWRITE_SHORT_NAME) \
 || !defined(TFWRITE_DISPLAY_NAME)
#error "TF writer specialization macros are required"
#endif

#include "fat32.h"
#include "tfdev.h"
#include "uart.h"
#include "xmodem.h"

#define RX_BASE 0x10000000u
#define RX_MAX  0x00400000u

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int) p[0] | ((unsigned int) p[1] << 8)
         | ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

int main(void)
{
    unsigned char *rx = (unsigned char *) RX_BASE;
    unsigned int   received = 0;
    unsigned int   size;
    fat_file       file;
    int            rc;

    put_str(TFWRITE_BANNER "\r\n");
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

    put_str("XMODEM\r\n");
    rc = xmodem_receive(rx, RX_MAX, &received);
    if (rc != XMODEM_OK) {
        put_str("XMODEM ERR ");
        put_dec((unsigned int) rc);
        put_str("\r\n");
        return 3;
    }
    if (received < 4u) {
        put_str("SHORT\r\n");
        return 4;
    }
    size = rd32(rx);
    if (size == 0u || size > received - 4u || size > RX_MAX - 4u) {
        put_str("SIZE ERR\r\n");
        return 5;
    }

    put_str("WRITE ");
    put_dec(size);
    put_str(" byte\r\n");
    rc = fat_write_file(TFWRITE_SHORT_NAME, rx + 4, size);
    if (rc != FAT_OK) {
        put_str("WRITE ERR ");
        put_dec((unsigned int) rc);
        put_str("\r\n");
        return 6;
    }
    rc = fat_open(TFWRITE_SHORT_NAME, &file);
    if (rc != FAT_OK || file.size != size) {
        put_str("VERIFY ERR\r\n");
        return 7;
    }
    put_str("WROTE " TFWRITE_DISPLAY_NAME "\r\n");
    return 0;
}
