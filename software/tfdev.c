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
    while ((mmio_read(MMIO_TF_CTRL) & TF_BUSY) != 0u) {
    }
    if ((mmio_read(MMIO_TF_CTRL) & TF_ERR) != 0u) {
        return 1;
    }
    for (i = 0; i < 128u; i++) {
        unsigned int w = mmio_read(TF_BUF + i * 4u);

        buf[i * 4u + 0u] = (unsigned char) w;
        buf[i * 4u + 1u] = (unsigned char) (w >> 8);
        buf[i * 4u + 2u] = (unsigned char) (w >> 16);
        buf[i * 4u + 3u] = (unsigned char) (w >> 24);
    }
    return 0;
}
