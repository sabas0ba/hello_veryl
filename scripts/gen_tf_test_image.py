#!/usr/bin/env python3
"""FAT32 テストディスクイメージ生成 (docs/tfcard.md「L1: 自作ビヘイビアモデルとテスト」)

MBR・BPB・FAT・ルートディレクトリ・ファイル実体を直接構築し，
非ゼロバイトのみの疎な参照関数 (Veryl package) として
src/tfcard/test_disk_image.veryl へ出力する．Python 標準ライブラリのみ使用．

生成するイメージ (いずれも構造的に FAT32 だが最小クラスタ数 65525 は
満たさない縮小版):

- Byte():   MBR + パーティション 1 (タイプ 0Ch，開始 LBA 8)
- SfByte(): スーパーフロッピー形式 (セクタ 0 が BPB，同一のファイル群)

ルートディレクトリの内容 (列挙順):
  1. ボリュームラベル "HELLOVERYL " (attr 08h，スキップ対象)
  2. LFN エントリ (attr 0Fh，スキップ対象)
  3. README  TXT  2348 byte，断片化チェーン 3 → 5 → 4，内容 (i*31+7) mod 256
  4. 削除済みエントリ (先頭 E5h，スキップ対象)
  5. SUBDIR       (attr 10h，スキップ対象)
  6. HELLO   TXT  23 byte，クラスタ 7，内容 "Hello, Veryl TF card!\\r\\n"
  7. DATA    BIN  256 byte，クラスタ 8，内容 00h..FFh
  8. IMAGE   BMP  78 byte，クラスタ 10，4x2 の無圧縮 24 bpp BMP
     (ボトムアップ．docs/tfcard.md「画像表示デモ仕様」の L1 用)
  9. 終端 (先頭 00h)

再生成: python scripts/gen_tf_test_image.py
"""

import sys
from pathlib import Path

SECTOR = 512
SPC = 2          # セクタ/クラスタ
RSVD = 4         # 予約セクタ数
NFATS = 2
FATSZ32 = 2      # セクタ/FAT
ROOT_CLUS = 2
TOTSEC32 = 128

README_SIZE = 2348
README_CHAIN = [3, 5, 4]  # 断片化 (昇順でない)
HELLO_BODY = b"Hello, Veryl TF card!\r\n"
DATA_BODY = bytes(range(256))

# 4x2 の無圧縮 24 bpp BMP (ボトムアップ)．TfImageDemo の L1 で
# 画素順・BGR→RGB565 変換・行反転を検証するため，全画素を異なる色にする．
# 画像座標 (上から) の行 0: 赤 緑 青 白 / 行 1: 黒 黄 シアン マゼンタ
IMAGE_W = 4
IMAGE_H = 2


def build_bmp() -> bytes:
    # 画像座標 (上から下) の画素を (R, G, B) で定義する
    rows_top_down = [
        [(0xFF, 0x00, 0x00), (0x00, 0xFF, 0x00), (0x00, 0x00, 0xFF), (0xFF, 0xFF, 0xFF)],
        [(0x00, 0x00, 0x00), (0xFF, 0xFF, 0x00), (0x00, 0xFF, 0xFF), (0xFF, 0x00, 0xFF)],
    ]
    row_raw = IMAGE_W * 3
    pad = (4 - row_raw % 4) % 4
    pixels = bytearray()
    # ボトムアップ: ファイル先頭は画像の最下行
    for row in reversed(rows_top_down):
        for (r, g, b) in row:
            pixels += bytes((b, g, r))
        pixels += bytes(pad)

    off_bits = 54
    hdr = bytearray()
    hdr += b"BM"
    hdr += le(off_bits + len(pixels), 4)   # bfSize
    hdr += le(0, 2) + le(0, 2)             # bfReserved1/2
    hdr += le(off_bits, 4)                 # bfOffBits
    hdr += le(40, 4)                       # biSize
    hdr += le(IMAGE_W, 4)                  # biWidth
    hdr += le(IMAGE_H, 4)                  # biHeight (正 = ボトムアップ)
    hdr += le(1, 2)                        # biPlanes
    hdr += le(24, 2)                       # biBitCount
    hdr += le(0, 4)                        # biCompression = BI_RGB
    hdr += le(len(pixels), 4)              # biSizeImage
    hdr += le(2835, 4) + le(2835, 4)       # 解像度 (72 dpi)
    hdr += le(0, 4) + le(0, 4)             # biClrUsed / biClrImportant
    assert len(hdr) == off_bits
    return bytes(hdr) + bytes(pixels)

EOC = 0x0FFFFFFF


def le(value: int, width: int) -> bytes:
    return value.to_bytes(width, "little")


def readme_body() -> bytes:
    return bytes((i * 31 + 7) & 0xFF for i in range(README_SIZE))


def dir_entry(name: bytes, attr: int, clus: int, size: int) -> bytes:
    assert len(name) == 11
    e = bytearray(32)
    e[0:11] = name
    e[11] = attr
    e[20:22] = le(clus >> 16, 2)
    e[26:28] = le(clus & 0xFFFF, 2)
    e[28:32] = le(size, 4)
    return bytes(e)


def lfn_entry(ord_no: int, chars: str, checksum: int) -> bytes:
    """LFN (VFAT) エントリ 1 個 (リーダは attr 0Fh で読み飛ばすだけ)"""
    e = bytearray(b"\xff" * 32)
    e[0] = ord_no
    e[11] = 0x0F
    e[12] = 0
    e[13] = checksum
    e[26:28] = le(0, 2)
    u = chars.ljust(13, "\x00")[:13].encode("utf-16-le")
    e[1:11] = u[0:10]
    e[14:26] = u[10:22]
    e[28:32] = e[28:30] + u[22:26][0:2]  # slot 内の残り 2 文字分
    return bytes(e)


def build_bpb() -> bytes:
    s = bytearray(SECTOR)
    s[0:3] = b"\xeb\x58\x90"        # JMP (スーパーフロッピー判定にも使用)
    s[3:11] = b"HELLOVRL"           # OEM 名
    s[11:13] = le(SECTOR, 2)        # BytsPerSec
    s[13] = SPC                     # SecPerClus
    s[14:16] = le(RSVD, 2)          # RsvdSecCnt
    s[16] = NFATS                   # NumFATs
    s[17:19] = le(0, 2)             # RootEntCnt (FAT32 は 0)
    s[21] = 0xF8                    # Media
    s[22:24] = le(0, 2)             # FATSz16 (FAT32 は 0)
    s[32:36] = le(TOTSEC32, 4)      # TotSec32
    s[36:40] = le(FATSZ32, 4)       # FATSz32
    s[44:48] = le(ROOT_CLUS, 4)     # RootClus
    s[48:50] = le(1, 2)             # FSInfo
    s[50:52] = le(6, 2)             # BkBootSec
    s[64] = 0x80                    # DrvNum
    s[66] = 0x29                    # BootSig
    s[67:71] = le(0x12345678, 4)    # VolID
    s[71:82] = b"HELLOVERYL "       # VolLab
    s[82:90] = b"FAT32   "          # FilSysType
    s[510:512] = b"\x55\xaa"
    return bytes(s)


IMAGE_BODY = build_bmp()


def build_fat() -> bytes:
    entries = {
        0: 0x0FFFFFF8,
        1: EOC,
        ROOT_CLUS: EOC,
        3: 5,       # README chain: 3 -> 5 -> 4
        5: 4,
        4: EOC,
        6: EOC,     # SUBDIR
        7: EOC,     # HELLO.TXT
        8: EOC,     # DATA.BIN
        10: EOC,    # IMAGE.BMP
    }
    fat = bytearray(FATSZ32 * SECTOR)
    for idx, val in entries.items():
        fat[idx * 4:idx * 4 + 4] = le(val, 4)
    return bytes(fat)


def build_root_dir() -> bytes:
    d = bytearray(SPC * SECTOR)
    entries = [
        dir_entry(b"HELLOVERYL ", 0x08, 0, 0),
        lfn_entry(0x41, "readme.txt", 0x00),
        dir_entry(b"README  TXT", 0x20, README_CHAIN[0], README_SIZE),
        b"\xe5" + dir_entry(b"XELETED TXT", 0x20, 9, 10)[1:],
        dir_entry(b"SUBDIR     ", 0x10, 6, 0),
        dir_entry(b"HELLO   TXT", 0x20, 7, len(HELLO_BODY)),
        dir_entry(b"DATA    BIN", 0x20, 8, len(DATA_BODY)),
        dir_entry(b"IMAGE   BMP", 0x20, 10, len(IMAGE_BODY)),
    ]
    for i, e in enumerate(entries):
        d[i * 32:(i + 1) * 32] = e
    return bytes(d)


def build_volume(part_start: int, with_mbr: bool) -> dict[int, bytes]:
    """LBA -> 512 byte セクタの疎な辞書を返す"""
    fat_start = part_start + RSVD
    data_start = fat_start + NFATS * FATSZ32

    def clus_lba(n: int) -> int:
        return data_start + (n - 2) * SPC

    sectors: dict[int, bytes] = {}

    def place(lba: int, blob: bytes) -> None:
        for i in range(0, len(blob), SECTOR):
            sec = blob[i:i + SECTOR].ljust(SECTOR, b"\x00")
            sectors[lba + i // SECTOR] = sec

    if with_mbr:
        mbr = bytearray(SECTOR)
        p = 446
        mbr[p] = 0x80                       # boot flag
        mbr[p + 4] = 0x0C                   # FAT32 (LBA)
        mbr[p + 8:p + 12] = le(part_start, 4)
        mbr[p + 12:p + 16] = le(TOTSEC32 - part_start, 4)
        mbr[510:512] = b"\x55\xaa"
        place(0, bytes(mbr))

    place(part_start, build_bpb())
    fat = build_fat()
    for k in range(NFATS):
        place(fat_start + k * FATSZ32, fat)
    place(clus_lba(ROOT_CLUS), build_root_dir())

    body = readme_body()
    cbytes = SPC * SECTOR
    for i, c in enumerate(README_CHAIN):
        place(clus_lba(c), body[i * cbytes:(i + 1) * cbytes])
    place(clus_lba(7), HELLO_BODY)
    place(clus_lba(8), DATA_BODY)
    place(clus_lba(10), IMAGE_BODY)
    return sectors


def emit_function(name: str, sectors: dict[int, bytes]) -> list[str]:
    # 巨大な単一 case 式は veryl のコード生成でスタックオーバーフローするため，
    # セクタ毎の補助関数 (最大 512 分岐) + LBA ディスパッチに分割する
    lines: list[str] = []
    n = 0
    used: list[int] = []
    for lba in sorted(sectors):
        sec = sectors[lba]
        arms = [(off, val) for off, val in enumerate(sec) if val != 0]
        if not arms:
            continue
        used.append(lba)
        lines += [
            f"    function {name}S{lba:04x} (",
            "        off: input logic<9>,",
            "    ) -> logic<8> {",
            "        return case off {",
        ]
        for off, val in arms:
            lines.append(f"            9'd{off:<3}: 8'h{val:02x},")
            n += 1
        lines += [
            "            default: 8'h00,",
            "        };",
            "    }",
        ]
    lines += [
        f"    function {name} (",
        "        lba: input logic<32>,",
        "        off: input logic<9> ,",
        "    ) -> logic<8> {",
        "        return case lba {",
    ]
    for lba in used:
        lines.append(f"            32'd{lba:<3}: {name}S{lba:04x}(off),")
    lines += [
        "            default: 8'h00,",
        "        };",
        "    }",
    ]
    print(f"  {name}: {n} nonzero bytes in {len(used)} sectors")
    return lines


def write_raw(path: Path, sectors: dict[int, bytes]) -> None:
    """疎な辞書を TOTSEC32 セクタの生イメージとして書き出す (ホスト側テスト用)"""
    img = bytearray(TOTSEC32 * SECTOR)
    for lba, sec in sectors.items():
        img[lba * SECTOR:(lba + 1) * SECTOR] = sec
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(img))
    print(f"wrote {path} ({len(img)} byte)")


def main() -> None:
    root = Path(__file__).resolve().parent.parent

    # --raw <dir>: Veryl パッケージではなく生イメージを書き出す．
    # software/fat32.c をホストで検証するために使う (verif/riscv/fat32_check.sh)．
    if len(sys.argv) >= 3 and sys.argv[1] == "--raw":
        d = Path(sys.argv[2])
        if not d.is_absolute():
            d = root / d
        write_raw(d / "mbr.img", build_volume(8, True))
        write_raw(d / "sf.img", build_volume(0, False))
        return

    out = root / "src" / "tfcard" / "test_disk_image.veryl"
    lines = [
        "// FAT32 テストディスクイメージ (生成物，編集しない)",
        "// 生成: python scripts/gen_tf_test_image.py (内容の説明もスクリプト参照)",
        "// 非ゼロバイトのみを保持する疎な参照関数．範囲外は 00h を返す．",
        "package TfTestImg {",
        "    // MBR + パーティション 1 (開始 LBA 8)",
    ]
    lines += emit_function("Byte", build_volume(8, True))
    lines.append("")
    lines.append("    // スーパーフロッピー形式 (セクタ 0 が BPB)")
    lines += emit_function("SfByte", build_volume(0, False))
    lines.append("}")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
