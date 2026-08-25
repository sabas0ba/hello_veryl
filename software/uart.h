/* UART と MMIO の最小ヘルパ (docs/riscv.md「メモリマップ」) */
#ifndef UART_H
#define UART_H

#define MMIO_UART  0x20000010 /* W: 送信バイト / R: bit0 = 送信可 */
#define MMIO_UART_RX 0x20000014 /* R: bit8 = 受信あり / bit7:0 = データ */
#define MMIO_MTIME 0x20000030 /* R: 自走カウンタ下位 32 bit */

#define UART_RX_VALID 0x00000100u

unsigned int mmio_read(unsigned int addr);
void         mmio_write(unsigned int addr, unsigned int val);

void put_char(int c);
void put_str(const char *s);
void put_hex(unsigned int v, int digits);
void put_dec(unsigned int v);

#endif
