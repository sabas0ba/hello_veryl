#!/usr/bin/env python3
"""ソフトコアのブートプログラムを Veryl の ROM パッケージへ変換する

(docs/riscv.md パッチ計画 #6．software/build.sh から呼ばれる)

巨大な単一 case 式は veryl のコード生成でスタックオーバーフローするため，
512 分岐毎に補助関数へ分割する (gen_tf_test_image.py / gen_riscv_rom.py と同じ方式)．

使い方:
  python scripts/gen_riscv_boot.py build/software/hello.bin
"""

import sys
from pathlib import Path

CHUNK = 512
OUT = Path("src/riscv/boot_rom.veryl")


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("使い方: gen_riscv_boot.py <program.bin>")
    src = Path(sys.argv[1])
    blob = src.read_bytes()
    if len(blob) % 4 != 0:
        blob += bytes(4 - len(blob) % 4)
    words = [int.from_bytes(blob[i:i + 4], "little") for i in range(0, len(blob), 4)]

    chunks = [words[i:i + CHUNK] for i in range(0, len(words), CHUNK)] or [[]]

    lines = [
        "// ソフトコアのブートプログラム (ROM イメージ)",
        f"// scripts/gen_riscv_boot.py が {src.as_posix()} から生成する．手で編集しない．",
        "// 元のソースは software/hello.S，逆アセンブルは build/software/hello.dis．",
        "",
        "pub package RvBootRom {",
        f"    /// プログラムのワード数",
        f"    const Words: u32 = {len(words)};",
        "",
    ]

    for c, chunk in enumerate(chunks):
        lines.append(f"    /// ワード {c * CHUNK}..{c * CHUNK + len(chunk) - 1}")
        lines.append(f"    function C{c} (")
        lines.append("        idx: input logic<16>,")
        lines.append("    ) -> logic<32> {")
        lines.append("        return case idx {")
        for i, w in enumerate(chunk):
            lines.append(f"            {c * CHUNK + i}: 32'h{w:08x},")
        lines.append("            default: 32'h00000000,")
        lines.append("        };")
        lines.append("    }")
        lines.append("")

    lines.append("    /// ワード番号からプログラム語を引く")
    lines.append("    function Word (")
    lines.append("        idx: input logic<16>,")
    lines.append("    ) -> logic<32> {")
    if len(chunks) == 1:
        lines.append("        return C0(idx);")
    else:
        expr = f"C{len(chunks) - 1}(idx)"
        for c in range(len(chunks) - 2, -1, -1):
            expr = f"if idx <: {(c + 1) * CHUNK} ? C{c}(idx) : {expr}"
        lines.append(f"        return {expr};")
    lines.append("    }")
    lines.append("}")
    lines.append("")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT} ({len(words)} words)")


if __name__ == "__main__":
    main()
