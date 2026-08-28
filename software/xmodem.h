/* UART 上の XMODEM-CRC receiver。
 *
 * 128-byte block (SOH) だけを受付け、CRC16-XMODEM を検査する。
 * XMODEM 自体は file size を持たないため、TF 書込みアプリケーションでは
 * 受信 payload の先頭 4 byte を little-endian の実サイズとする。 */
#ifndef XMODEM_H
#define XMODEM_H

enum {
    XMODEM_OK       = 0,
    XMODEM_TIMEOUT  = 1,
    XMODEM_CANCEL   = 2,
    XMODEM_PROTOCOL = 3,
    XMODEM_OVERFLOW = 4
};

int xmodem_receive(void *dst, unsigned int max_len, unsigned int *out_len);

#endif
