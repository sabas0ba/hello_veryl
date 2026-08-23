#!/usr/bin/env python3
"""riscv-tests のバイナリを Veryl パッケージへ変換する (docs/riscv.md「検証」)

verif/riscv/build_tests.sh が生成した build/riscv_tests/*.bin を読み，
テスト番号とワード番号で引ける関数群を src/riscv/test_rom.veryl へ出力する．

巨大な単一 case 式は veryl のコード生成でスタックオーバーフローするため，
テスト毎・512 分岐毎に補助関数へ分割する (gen_tf_test_image.py と同じ方式)．

再生成:
  .\\scripts\\fpga-run.ps1 bash verif/riscv/build_tests.sh
  .\\scripts\\fpga-run.ps1 /opt/oss-cad-suite/py3bin/python3 scripts/gen_riscv_rom.py
"""

from pathlib import Path

CHUNK = 512
SRC_DIR = Path("build/riscv_tests")
OUT = Path("src/riscv/test_rom.veryl")


def emit_test(index: int, name: str, words: list[int]) -> list[str]:
    """1 テストぶんの関数群を出力する"""
    lines = []
    chunks = [words[i:i + CHUNK] for i in range(0, len(words), CHUNK)] or [[]]

    for c, chunk in enumerate(chunks):
        lines.append(f"    /// {name} のワード {c * CHUNK}..{c * CHUNK + len(chunk) - 1}")
        lines.append(f"    function T{index}C{c} (")
        lines.append("        idx: input logic<16>,")
        lines.append("    ) -> logic<32> {")
        lines.append("        return case idx {")
        for i, w in enumerate(chunk):
            lines.append(f"            {c * CHUNK + i}: 32'h{w:08x},")
        lines.append("            default: 32'h00000000,")
        lines.append("        };")
        lines.append("    }")
        lines.append("")

    lines.append(f"    /// {name} ({len(words)} words)")
    lines.append(f"    function T{index} (")
    lines.append("        idx: input logic<16>,")
    lines.append("    ) -> logic<32> {")
    if len(chunks) == 1:
        lines.append(f"        return T{index}C0(idx);")
    else:
        expr = f"T{index}C{len(chunks) - 1}(idx)"
        for c in range(len(chunks) - 2, -1, -1):
            expr = f"if idx <: {(c + 1) * CHUNK} ? T{index}C{c}(idx) : {expr}"
        lines.append(f"        return {expr};")
    lines.append("    }")
    lines.append("")
    return lines


def main() -> None:
    files = sorted(SRC_DIR.glob("*.bin"))
    if not files:
        raise SystemExit(f"{SRC_DIR} に .bin が無い (verif/riscv/build_tests.sh を先に実行する)")

    tests = []
    for f in files:
        blob = f.read_bytes()
        if len(blob) % 4 != 0:
            blob += bytes(4 - len(blob) % 4)
        words = [int.from_bytes(blob[i:i + 4], "little") for i in range(0, len(blob), 4)]
        tests.append((f.stem, words))

    lines = [
        "// riscv-tests (rv32ui) のプログラムイメージ",
        "// scripts/gen_riscv_rom.py が build/riscv_tests/*.bin から生成する．手で編集しない．",
        "//",
        "// テスト番号と名前の対応:",
    ]
    for i, (name, words) in enumerate(tests):
        lines.append(f"//   {i:2d}: {name} ({len(words)} words)")
    lines += [
        "",
        "pub package RvTestImg {",
        f"    /// テスト本数",
        f"    const Count: u32 = {len(tests)};",
        f"    /// 最大ワード数 (プリロードのループ上限)",
        f"    const MaxWords: u32 = {max(len(w) for _, w in tests)};",
        "",
    ]

    for i, (name, words) in enumerate(tests):
        lines += emit_test(i, name, words)

    # ワード数
    lines.append("    /// テストのワード数")
    lines.append("    function Len (")
    lines.append("        id: input logic<8>,")
    lines.append("    ) -> logic<16> {")
    lines.append("        return case id {")
    for i, (_, words) in enumerate(tests):
        lines.append(f"            {i}: 16'd{len(words)},")
    lines.append("            default: 16'd0,")
    lines.append("        };")
    lines.append("    }")
    lines.append("")

    # ディスパッチ
    lines.append("    /// テスト id とワード番号からプログラム語を引く")
    lines.append("    function Word (")
    lines.append("        id : input logic<8> ,")
    lines.append("        idx: input logic<16>,")
    lines.append("    ) -> logic<32> {")
    lines.append("        return case id {")
    for i, _ in enumerate(tests):
        lines.append(f"            {i}: T{i}(idx),")
    lines.append("            default: 32'h00000000,")
    lines.append("        };")
    lines.append("    }")
    lines.append("}")
    lines.append("")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(lines), encoding="utf-8")
    total = sum(len(w) for _, w in tests)
    print(f"wrote {OUT} ({len(tests)} tests, {total} words)")


if __name__ == "__main__":
    main()
