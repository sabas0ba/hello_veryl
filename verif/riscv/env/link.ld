/* riscv-tests (rv32ui) 用リンカスクリプト (docs/riscv.md「メモリマップ」)
   命令・データとも BSRAM 領域 0x0000_0000 に配置する */
OUTPUT_ARCH( "riscv" )
ENTRY(_start)
SECTIONS
{
  . = 0x00000000;
  .text.init : { *(.text.init) }
  .text      : { *(.text) *(.text.*) }
  . = ALIGN(0x10);
  .data      : { *(.data) *(.data.*) *(.rodata) *(.rodata.*) *(.sdata) *(.sdata.*) }
  .bss       : { *(.bss) *(.sbss) }
  _end = .;
}
