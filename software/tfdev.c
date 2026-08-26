#include "tfdev.h"

#include "fat32.h"
#include "uart.h"

/* 27 MHz なので 2 秒ぶん */
#define INIT_TIMEOUT 54000000u

int tf_wait_init(void)
{
    unsigned int t0 = mmio_read(MMIO_MTIME);

    for (;;) {
        if ((mmio_read(MMIO_TF_CTRL) & TF_INIT_DONE) != 0u) {
            return 0;
        }
        if (mmio_read(MMIO_MTIME) - t0 > INIT_TIMEOUT) {
            return 1;
        }
    }
}

int fat_dev_read(unsigned int lba, unsigned char *buf)
{
    unsigned int i;

    mmio_write(MMIO_TF_LBA, lba);
    mmio_write(MMIO_TF_CTRL, 1);
    for (i = 0; i < 512u; i++) {
        unsigned int st;
        unsigned int v;

        do {
            st = mmio_read(MMIO_TF_CTRL);
            if ((st & TF_BUSY) == 0u && (st & TF_RX_VALID) == 0u) {
                return 1;
            }
        } while ((st & TF_RX_VALID) == 0u);

        v = mmio_read(MMIO_TF_DATA);
        if ((v & TF_RX_VALID) == 0u) {
            return 1;
        }
        buf[i] = (unsigned char) v;
    }
    while ((mmio_read(MMIO_TF_CTRL) & TF_BUSY) != 0u) {
    }
    return (mmio_read(MMIO_TF_CTRL) & TF_ERR) != 0u;
}

int fat_dev_write(unsigned int lba, const unsigned char *buf)
{
    unsigned int i;

    mmio_write(MMIO_TF_LBA, lba);
    mmio_write(MMIO_TF_CTRL, 3); /* bit0 = 開始，bit1 = ライト */
    for (i = 0; i < 512u; i++) {
        unsigned int st;

        do {
            st = mmio_read(MMIO_TF_CTRL);
            if ((st & TF_BUSY) == 0u) {
                return 1;
            }
        } while ((st & TF_TX_SPACE) == 0u);
        mmio_write(MMIO_TF_DATA, buf[i]);
    }
    while ((mmio_read(MMIO_TF_CTRL) & TF_BUSY) != 0u) {
    }
    return (mmio_read(MMIO_TF_CTRL) & TF_ERR) != 0u;
}
