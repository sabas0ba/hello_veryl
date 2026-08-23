#!/usr/bin/env python3
"""画像表示デモ (docs/tfcard.md「画像表示デモ仕様」) 用の BMP を生成する．

TfImageDemo が受理するのは無圧縮 24 bpp・寸法固定 (既定 100x60) の BMP のみ．
生成したファイルを TF カードのルートへ `*.BMP` の名前で置く．

使い方:
  python scripts/gen_demo_bmp.py                     # テストパターンを生成
  python scripts/gen_demo_bmp.py --from photo.bmp    # 既存の BMP/PNG を縮小して変換
  python scripts/gen_demo_bmp.py -o out/IMAGE.BMP    # 出力先を指定

`--from` は無圧縮 24 bpp BMP と PNG (非インタレース) を受け付け，
最近傍で寸法を合わせる．形式は拡張子ではなくシグネチャで判別するため，
`.bmp` という名前の PNG も正しく扱える．JPEG は非対応
(Python 標準ライブラリのみを使う方針のため)．
"""

import argparse
import zlib
from pathlib import Path

DEFAULT_W = 100
DEFAULT_H = 60

PNG_SIG = bytes((0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A))


def le(value: int, width: int) -> bytes:
    return value.to_bytes(width, "little")


def row_bytes(width: int) -> int:
    raw = width * 3
    return raw + (4 - raw % 4) % 4


def build_bmp(pixels_top_down: list[list[tuple[int, int, int]]]) -> bytes:
    """(R, G, B) の行リスト (上から下) から無圧縮 24 bpp BMP を組み立てる"""
    height = len(pixels_top_down)
    width = len(pixels_top_down[0])
    stride = row_bytes(width)
    pad = stride - width * 3

    body = bytearray()
    # BMP はボトムアップで格納する (最終行が画像の最上行)
    for row in reversed(pixels_top_down):
        for (r, g, b) in row:
            body += bytes((b, g, r))
        body += bytes(pad)

    off_bits = 54
    hdr = bytearray()
    hdr += b"BM"
    hdr += le(off_bits + len(body), 4)      # bfSize
    hdr += le(0, 2) + le(0, 2)              # bfReserved1/2
    hdr += le(off_bits, 4)                  # bfOffBits
    hdr += le(40, 4)                        # biSize
    hdr += le(width, 4)                     # biWidth
    hdr += le(height, 4)                    # biHeight (正 = ボトムアップ)
    hdr += le(1, 2)                         # biPlanes
    hdr += le(24, 2)                        # biBitCount
    hdr += le(0, 4)                         # biCompression = BI_RGB
    hdr += le(len(body), 4)                 # biSizeImage
    hdr += le(2835, 4) + le(2835, 4)        # 解像度 (72 dpi)
    hdr += le(0, 4) + le(0, 4)              # biClrUsed / biClrImportant
    assert len(hdr) == off_bits
    return bytes(hdr) + bytes(body)


def test_pattern(width: int, height: int) -> list[list[tuple[int, int, int]]]:
    """上 2/3 はカラーバー，下 1/3 は輝度グラデーション"""
    bars = [
        (255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (0, 0, 0),
    ]
    split = height * 2 // 3
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            if y < split:
                row.append(bars[x * len(bars) // width])
            else:
                v = x * 255 // max(1, width - 1)
                row.append((v, v, v))
        rows.append(row)
    # 外周 1 px の赤枠 (表示範囲と向きの確認用)
    for x in range(width):
        rows[0][x] = (255, 0, 0)
        rows[height - 1][x] = (255, 0, 0)
    for y in range(height):
        rows[y][0] = (255, 0, 0)
        rows[y][width - 1] = (255, 0, 0)
    return rows


def read_bmp24(path: Path) -> list[list[tuple[int, int, int]]]:
    """無圧縮 24 bpp BMP を読み，(R, G, B) の行リスト (上から下) を返す"""
    data = path.read_bytes()
    if data[0:2] != b"BM":
        raise SystemExit(f"{path}: BM シグネチャがない (無圧縮 BMP のみ対応)")
    off_bits = int.from_bytes(data[10:14], "little")
    width = int.from_bytes(data[18:22], "little", signed=True)
    height = int.from_bytes(data[22:26], "little", signed=True)
    bpp = int.from_bytes(data[28:30], "little")
    compression = int.from_bytes(data[30:34], "little")
    if bpp != 24 or compression != 0:
        raise SystemExit(f"{path}: 無圧縮 24 bpp のみ対応 (bpp={bpp}, compression={compression})")
    if width <= 0:
        raise SystemExit(f"{path}: 幅が不正 ({width})")

    bottom_up = height > 0
    abs_h = abs(height)
    stride = row_bytes(width)
    rows = []
    for r in range(abs_h):
        start = off_bits + r * stride
        raw = data[start:start + width * 3]
        if len(raw) < width * 3:
            raise SystemExit(f"{path}: 画素データが不足している")
        rows.append([(raw[i * 3 + 2], raw[i * 3 + 1], raw[i * 3]) for i in range(width)])
    if bottom_up:
        rows.reverse()
    return rows


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png(path: Path, bg: tuple[int, int, int]):
    """非インタレース PNG を読み，(R, G, B) の行リスト (上から下) を返す

    ビット深度 8/16，カラータイプ 0/2/3/4/6 に対応する．
    アルファは bg に対して合成する．
    """
    data = path.read_bytes()
    if data[0:8] != PNG_SIG:
        raise SystemExit(f"{path}: PNG シグネチャがない")

    pos = 8
    ihdr = None
    plte = b""
    trns = b""
    idat = bytearray()
    while pos + 8 <= len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length  # 長さ + 型 + データ + CRC
        if ctype == b"IHDR":
            ihdr = body
        elif ctype == b"PLTE":
            plte = body
        elif ctype == b"tRNS":
            trns = body
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break
    if ihdr is None:
        raise SystemExit(f"{path}: IHDR がない")

    width = int.from_bytes(ihdr[0:4], "big")
    height = int.from_bytes(ihdr[4:8], "big")
    depth = ihdr[8]
    color = ihdr[9]
    interlace = ihdr[12]
    if interlace != 0:
        raise SystemExit(f"{path}: インタレース PNG は非対応")
    if depth not in (8, 16):
        raise SystemExit(f"{path}: ビット深度 {depth} は非対応 (8 または 16)")

    samples = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if samples is None:
        raise SystemExit(f"{path}: カラータイプ {color} は非対応")
    if color == 3 and depth != 8:
        raise SystemExit(f"{path}: パレット PNG はビット深度 8 のみ対応")

    sample_bytes = depth // 8
    bpp = samples * sample_bytes
    stride = width * bpp

    raw = zlib.decompress(bytes(idat))
    if len(raw) < (stride + 1) * height:
        raise SystemExit(f"{path}: 画素データが不足している")

    rows_raw = []
    prev = bytearray(stride)
    off = 0
    for _y in range(height):
        ftype = raw[off]
        line = bytearray(raw[off + 1:off + 1 + stride])
        off += 1 + stride
        if ftype == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                c = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF
        elif ftype != 0:
            raise SystemExit(f"{path}: 未知のフィルタ種別 {ftype}")
        rows_raw.append(bytes(line))
        prev = line

    def sample(line: bytes, idx: int) -> int:
        # 16 bit は上位バイトを採る
        return line[idx * sample_bytes]

    rows = []
    for line in rows_raw:
        row = []
        for x in range(width):
            base = x * samples
            alpha = 255
            if color == 0:
                v = sample(line, base)
                r = g = b = v
            elif color == 4:
                v = sample(line, base)
                alpha = sample(line, base + 1)
                r = g = b = v
            elif color == 2:
                r = sample(line, base)
                g = sample(line, base + 1)
                b = sample(line, base + 2)
            elif color == 6:
                r = sample(line, base)
                g = sample(line, base + 1)
                b = sample(line, base + 2)
                alpha = sample(line, base + 3)
            else:  # color == 3 (パレット)
                idx = line[x]
                if (idx + 1) * 3 > len(plte):
                    raise SystemExit(f"{path}: PLTE の範囲外を参照している")
                r, g, b = plte[idx * 3], plte[idx * 3 + 1], plte[idx * 3 + 2]
                if idx < len(trns):
                    alpha = trns[idx]
            if alpha != 255:
                r = (r * alpha + bg[0] * (255 - alpha)) // 255
                g = (g * alpha + bg[1] * (255 - alpha)) // 255
                b = (b * alpha + bg[2] * (255 - alpha)) // 255
            row.append((r, g, b))
        rows.append(row)
    return rows


def read_image(path: Path, bg: tuple[int, int, int]):
    """シグネチャで BMP / PNG を判別して読む (拡張子は見ない)"""
    head = path.read_bytes()[:8]
    if head[0:2] == b"BM":
        return read_bmp24(path)
    if head == PNG_SIG:
        return read_png(path, bg)
    raise SystemExit(f"{path}: BMP でも PNG でもない (先頭 {head[:4].hex()})")


def resize_nearest(rows, width: int, height: int):
    src_h = len(rows)
    src_w = len(rows[0])
    return [
        [rows[y * src_h // height][x * src_w // width] for x in range(width)]
        for y in range(height)
    ]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="src", type=Path, default=None,
                    help="変換元の画像 (無圧縮 24 bpp BMP または非インタレース PNG)")
    ap.add_argument("--bg", choices=("white", "black"), default="white",
                    help="PNG のアルファを合成する背景色 (既定: white)")
    ap.add_argument("-o", "--output", type=Path, default=Path("build/IMAGE.BMP"),
                    help="出力先 (既定: build/IMAGE.BMP)")
    ap.add_argument("--width", type=int, default=DEFAULT_W)
    ap.add_argument("--height", type=int, default=DEFAULT_H)
    args = ap.parse_args()

    if args.width % 2 != 0:
        raise SystemExit("幅は偶数である必要がある (32 bit ワード = 2 px)")

    if args.src is not None:
        bg = (255, 255, 255) if args.bg == "white" else (0, 0, 0)
        rows = resize_nearest(read_image(args.src, bg), args.width, args.height)
    else:
        rows = test_pattern(args.width, args.height)

    blob = build_bmp(rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    print(f"wrote {args.output} ({args.width}x{args.height}, {len(blob)} byte)")


if __name__ == "__main__":
    main()
