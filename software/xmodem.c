#include "xmodem.h"

#include "uart.h"

#define SOH 0x01
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define CRC_REQUEST 0x43
#define BLOCK_SIZE 128u
#define MAX_RETRIES 16

/* 27 MHz の mtime tick。unsigned 差分なので 32-bit wrap をまたげる。 */
#define BYTE_TIMEOUT 27000000u

static int get_byte(unsigned int timeout, unsigned char *out)
{
    unsigned int start = mmio_read(MMIO_MTIME);

    for (;;) {
        unsigned int v = mmio_read(MMIO_UART_RX);

        if ((v & UART_RX_VALID) != 0u) {
            *out = (unsigned char) v;
            return 0;
        }
        if (mmio_read(MMIO_MTIME) - start > timeout) {
            return 1;
        }
    }
}

static unsigned int crc16(const unsigned char *data, unsigned int len)
{
    unsigned int crc = 0;
    unsigned int i;

    for (i = 0; i < len; i++) {
        int bit;

        crc ^= (unsigned int) data[i] << 8;
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) != 0u ? (crc << 1) ^ 0x1021u : crc << 1;
            crc &= 0xffffu;
        }
    }
    return crc;
}

static int receive_block(unsigned char *block, unsigned char *number)
{
    unsigned char inv;
    unsigned char crc_hi;
    unsigned char crc_lo;
    unsigned int  i;
    unsigned int  got_crc;

    if (get_byte(BYTE_TIMEOUT, number) != 0
        || get_byte(BYTE_TIMEOUT, &inv) != 0) {
        return XMODEM_TIMEOUT;
    }
    for (i = 0; i < BLOCK_SIZE; i++) {
        if (get_byte(BYTE_TIMEOUT, block + i) != 0) {
            return XMODEM_TIMEOUT;
        }
    }
    if (get_byte(BYTE_TIMEOUT, &crc_hi) != 0
        || get_byte(BYTE_TIMEOUT, &crc_lo) != 0) {
        return XMODEM_TIMEOUT;
    }
    if ((unsigned char) (*number + inv) != 0xffu) {
        return XMODEM_PROTOCOL;
    }
    got_crc = (unsigned int) crc_hi << 8 | crc_lo;
    if (crc16(block, BLOCK_SIZE) != got_crc) {
        return XMODEM_PROTOCOL;
    }
    return XMODEM_OK;
}

int xmodem_receive(void *dst, unsigned int max_len, unsigned int *out_len)
{
    unsigned char *output = (unsigned char *) dst;
    unsigned char  block[BLOCK_SIZE];
    unsigned char  expected = 1;
    unsigned int   done = 0;
    int            retries = 0;
    int            started = 0;

    for (;;) {
        unsigned char ch;
        int           rc;

        if (!started) {
            put_char(CRC_REQUEST);
        }
        if (get_byte(BYTE_TIMEOUT, &ch) != 0) {
            if (++retries >= MAX_RETRIES) {
                return XMODEM_TIMEOUT;
            }
            continue;
        }
        if (ch == EOT) {
            put_char(ACK);
            if (out_len != 0) {
                *out_len = done;
            }
            return XMODEM_OK;
        }
        if (ch == CAN) {
            unsigned char next;

            if (get_byte(BYTE_TIMEOUT, &next) == 0 && next == CAN) {
                return XMODEM_CANCEL;
            }
            continue;
        }
        if (ch != SOH) {
            if (++retries >= MAX_RETRIES) {
                return XMODEM_PROTOCOL;
            }
            continue;
        }

        {
            unsigned char number;

            started = 1;
            rc = receive_block(block, &number);
            if (rc != XMODEM_OK) {
                put_char(NAK);
                if (++retries >= MAX_RETRIES) {
                    return rc;
                }
                continue;
            }
            if (number == expected) {
                unsigned int i;

                if (done + BLOCK_SIZE > max_len) {
                    put_char(CAN);
                    put_char(CAN);
                    return XMODEM_OVERFLOW;
                }
                for (i = 0; i < BLOCK_SIZE; i++) {
                    output[done + i] = block[i];
                }
                done += BLOCK_SIZE;
                expected++;
            } else if (number != (unsigned char) (expected - 1u)) {
                put_char(NAK);
                if (++retries >= MAX_RETRIES) {
                    return XMODEM_PROTOCOL;
                }
                continue;
            }
            retries = 0;
            put_char(ACK);
        }
    }
}
