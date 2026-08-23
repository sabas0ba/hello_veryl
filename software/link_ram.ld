/* オンチップ RAM で実行する C プログラム用リンカスクリプト
   (docs/riscv.md「LCD デモ」)

   イメージは UART ブートモニタが PSRAM の 0x1000_0000 へ置くが，
   実行は RAM の 0x0000_2000 で行う．そのためリンクは RAM の番地で行い，
   先頭の .text.boot が自分自身を RAM へ複写する (software/crt0_ram.S)．

   .bss と z バッファは複写対象に入れない (NOBITS / 直接番地指定) ので，
   転送量は .text + .rodata + .data のぶんだけになる． */
OUTPUT_ARCH( "riscv" )
ENTRY(_start)

RAM_BASE  = 0x00002000;
RAM_SIZE  = 0x00002000;  /* RvSoc の RamWords = 2048 語 */

SECTIONS
{
  . = RAM_BASE;
  .text.boot : { *(.text.boot) }
  .text      : { *(.text.init) *(.text) *(.text.*) }
  . = ALIGN(4);
  .rodata    : { *(.rodata) *(.rodata.*) *(.srodata) *(.srodata.*) . = ALIGN(4); }
  . = ALIGN(4);
  .data      : { *(.data) *(.data.*) *(.sdata) *(.sdata.*) . = ALIGN(4); }
  . = ALIGN(4);
  __image_end = .;

  __bss_start = .;
  .bss (NOLOAD) : { *(.bss) *(.bss.*) *(.sbss) *(.sbss.*) *(COMMON) }
  . = ALIGN(4);
  __bss_end = .;

  __stack_top   = RAM_BASE + RAM_SIZE;
  __image_words = (__image_end - RAM_BASE) / 4;

  /* RAM に収まらないと静かに壊れるので，リンク時に落とす。
     スタックの余地として 1 KB を残す． */
  ASSERT(__bss_end <= __stack_top - 0x400,
         "オンチップ RAM (8 KB) に収まらない: .text+.rodata+.data+.bss を減らすこと")
}
