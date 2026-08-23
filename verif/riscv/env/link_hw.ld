/* riscv-tests 用リンカスクリプト (実機版) (docs/riscv.md「メモリマップ」)
   命令・データとも PSRAM 領域 0x1000_0000 に配置する．

   リンカは `la reg, sym` を，シンボルが 12 bit 即値に収まる場合
   `li reg, <絶対アドレス>` へ緩和する．そのため 0x0 でリンクした
   バイナリを実機の 0x1000_0000 へロードするとデータ参照だけが
   0x0 側 (ブート ROM) を指し，最初のロード/ストアで失敗する．
   実機ではロードアドレスそのものでリンクして回避する． */
OUTPUT_ARCH( "riscv" )
ENTRY(_start)
SECTIONS
{
  . = 0x10000000;
  .text.init : { *(.text.init) }
  .text      : { *(.text) *(.text.*) }
  . = ALIGN(0x10);
  .data      : { *(.data) *(.data.*) *(.rodata) *(.rodata.*) *(.sdata) *(.sdata.*) }
  .bss       : { *(.bss) *(.sbss) }
  _end = .;
}
