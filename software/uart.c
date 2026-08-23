#include "uart.h"

unsigned int mmio_read(unsigned int addr)
{
    return *(volatile unsigned int *) addr;
}

void mmio_write(unsigned int addr, unsigned int val)
{
    *(volatile unsigned int *) addr = val;
}

void put_char(int c)
{
    while ((mmio_read(MMIO_UART) & 1u) == 0u) {
    }
    mmio_write(MMIO_UART, (unsigned int) c);
}

void put_str(const char *s)
{
    while (*s != '\0') {
        put_char(*s++);
    }
}

void put_hex(unsigned int v, int digits)
{
    int i;

    for (i = digits - 1; i >= 0; i--) {
        int d = (int) ((v >> (i * 4)) & 0xfu);
        put_char(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

void put_dec(unsigned int v)
{
    char buf[10];
    int  n = 0;

    if (v == 0) {
        put_char('0');
        return;
    }
    while (v != 0 && n < 10) {
        buf[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    while (n-- > 0) {
        put_char(buf[n]);
    }
}
