/* ソフトコアのブートプログラム用リンカスクリプト
   命令・データとも ROM 領域 0x0000_0000 に配置する (docs/riscv.md「メモリマップ」) */
OUTPUT_ARCH( "riscv" )
ENTRY(_start)
SECTIONS
{
  . = 0x00000000;
  .text.init : { *(.text.init) }
  .text      : { *(.text) *(.text.*) }
  . = ALIGN(4);
  .rodata    : { *(.rodata) *(.rodata.*) *(.srodata) *(.srodata.*) }
  .data      : { *(.data) *(.data.*) *(.sdata) *(.sdata.*) }
  _end = .;
}
