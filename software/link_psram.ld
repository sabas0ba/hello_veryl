/* PSRAM へロードして実行する C プログラム用リンカスクリプト
   (docs/riscv.md「LCD デモ」)

   UART ブートモニタが 0x1000_0000 へ置くため，そのアドレスでリンクする．
   .bss は objcopy -O binary の出力に含まれない (NOBITS) ので，
   転送量は .text + .data のぶんだけになる．crt0.S がゼロ埋めする．

   モニタは 4 バイト単位で受け取るため，ロードされる最後のセクションを
   ALIGN(4) で終端してイメージ長を 4 の倍数にする． */
OUTPUT_ARCH( "riscv" )
ENTRY(_start)
SECTIONS
{
  . = 0x10000000;
  .text.init : { *(.text.init) }
  .text      : { *(.text) *(.text.*) }
  . = ALIGN(4);
  .rodata    : { *(.rodata) *(.rodata.*) *(.srodata) *(.srodata.*) . = ALIGN(4); }
  . = ALIGN(4);
  .data      : { *(.data) *(.data.*) *(.sdata) *(.sdata.*) . = ALIGN(4); }
  . = ALIGN(4);
  __bss_start = .;
  .bss       : { *(.bss) *(.bss.*) *(.sbss) *(.sbss.*) *(COMMON) }
  . = ALIGN(4);
  __bss_end = .;
  _end = .;
}
