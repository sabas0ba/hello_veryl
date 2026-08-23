#!/usr/bin/env python3
"""PsramPhy の PnR 後ネットリスト検査 (docs/psram.md「物理層」の L2 相当検査).

run.sh が生成する build/psram_phy_probe/phy_pnr.json に対し，以下を機械照合する:

1. bit ごとの接続: ODDR.Q0 -> IOBUF.I / ODDR.Q1 -> IOBUF.OEN / IOBUF.O -> IDDR.D
2. 配置: bit i のセル 3 点が sip_cst 上の該当パッド (docs/psram.md
   「パッドマッピング」表 = Apicula chipdb 由来，実機読み出しで検証済み) に
   揃って配置され，IOLOGIC の I/O 側 (…AI/…AO 等) がパッド半部と一致する
3. クロック: CK/CK# の ODDR は rPLL CLKOUTP，データ系 ODDR/IDDR は CLKOUT に接続

依存: OSS CAD Suite 同梱 python (標準ライブラリのみ)
"""

import json
import re
import sys

PNR_JSON = "build/psram_phy_probe/phy_pnr.json"

# 期待パッド位置 (docs/psram.md「パッドマッピング」表: sip_cst['GW1NR-9C']['QFN88P'])
DQ_PADS = {
    0: ("X0Y1", "B"),
    1: ("X0Y2", "A"),
    2: ("X0Y2", "B"),
    3: ("X0Y3", "A"),
    4: ("X0Y8", "A"),
    5: ("X0Y13", "A"),
    6: ("X0Y15", "A"),
    7: ("X0Y16", "B"),
}
RWDS_PAD = ("X0Y16", "A")
CK_PAD = ("X0Y7", "A")
CK_N_PAD = ("X0Y6", "A")
CS_N_PAD = ("X0Y5", "B")
RESET_N_PAD = ("X0Y1", "A")

errors = []


def err(msg):
    errors.append(msg)
    print(f"NG: {msg}")


def bel_of(cell):
    return cell.get("attributes", {}).get("NEXTPNR_BEL", "")


def loc_half(cell):
    bel = bel_of(cell)
    if "/" not in bel:
        return ("", "")
    loc, b = bel.split("/", 1)
    m = re.match(r"(?:IOB|IOLOGIC)([AB])", b)
    return (loc, m.group(1) if m else "")


def net(cell, port):
    bits = cell["connections"].get(port, [])
    return bits[0] if len(bits) == 1 else None


def find_cells(cells, pattern):
    pat = re.compile(pattern)
    return {name: c for name, c in cells.items() if pat.search(name)}


def get_one(cells, pattern, what):
    found = find_cells(cells, pattern)
    if len(found) != 1:
        err(f"{what}: セルが一意でない ({len(found)} 件: {list(found)[:3]})")
        return None
    return next(iter(found.values()))


def check_trio(oddr, iobuf, iddr, pad, what):
    """接続 3 点と配置 (同一パッド，IOLOGIC I/O 側) を検査する."""
    loc, half = pad
    if net(oddr, "Q0") != net(iobuf, "I"):
        err(f"{what}: ODDR.Q0 -> IOBUF.I が未接続/不一致")
    if net(oddr, "Q1") != net(iobuf, "OEN"):
        err(f"{what}: ODDR.Q1 -> IOBUF.OEN が未接続/不一致")
    if iddr is not None and net(iobuf, "O") != net(iddr, "D"):
        err(f"{what}: IOBUF.O -> IDDR.D が未接続/不一致")
    for cell, kind, suffix in ((oddr, "ODDR", "O"), (iobuf, "IOBUF", ""),
                               (iddr, "IDDR", "I")):
        if cell is None:
            continue
        cloc, chalf = loc_half(cell)
        if (cloc, chalf) != (loc, half):
            err(f"{what}: {kind} の配置 {cloc}/{chalf} != 期待 {loc}/{half}")
        bel = bel_of(cell)
        if kind != "IOBUF" and not bel.endswith(suffix):
            err(f"{what}: {kind} の BEL {bel} が {suffix} 側でない")


def check_sdr_out(cells, name_pat, pad, clk_net, what):
    """CK/CK#/CS#/RESET# 系: ODDR -> OBUF とその配置・クロックを検査する."""
    oddr = get_one(cells, name_pat, what)
    if oddr is None:
        return
    loc, half = pad
    cloc, chalf = loc_half(oddr)
    if (cloc, chalf) != (loc, half):
        err(f"{what}: ODDR の配置 {cloc}/{chalf} != 期待 {loc}/{half}")
    if net(oddr, "CLK") != clk_net:
        err(f"{what}: ODDR.CLK が期待クロックに未接続")
    # 同一パッドの OBUF へ Q0 が入ること
    q0 = net(oddr, "Q0")
    obufs = [c for c in cells.values()
             if c["type"] == "OBUF" and loc_half(c) == (loc, half)]
    if len(obufs) != 1 or net(obufs[0], "I") != q0:
        err(f"{what}: ODDR.Q0 -> 同一パッド OBUF.I の接続が確認できない")


def main():
    with open(PNR_JSON) as f:
        pnr = json.load(f)
    cells = pnr["modules"]["top"]["cells"]

    # クロックネットの特定 (rPLL の出力)
    plls = [c for c in cells.values() if c["type"] == "rPLL"]
    if len(plls) != 1:
        print(f"NG: rPLL が一意でない ({len(plls)} 件)")
        return 1
    pll = plls[0]
    clkout = net(pll, "CLKOUT")
    clkoutp = net(pll, "CLKOUTP")
    if clkout is None or clkoutp is None:
        err("rPLL の CLKOUT/CLKOUTP ネットを特定できない")

    # DQ x8
    for i in range(8):
        oddr = get_one(cells, rf"g_dq\[{i}\]\.u_oddr$", f"dq[{i}] ODDR")
        iobuf = get_one(cells, rf"g_dq\[{i}\]\.u_iobuf$", f"dq[{i}] IOBUF")
        iddr = get_one(cells, rf"g_dq\[{i}\]\.u_iddr$", f"dq[{i}] IDDR")
        if None in (oddr, iobuf, iddr):
            continue
        check_trio(oddr, iobuf, iddr, DQ_PADS[i], f"dq[{i}]")
        if net(oddr, "CLK") != clkout or net(iddr, "CLK") != clkout:
            err(f"dq[{i}]: ODDR/IDDR の CLK が CLKOUT でない")

    # RWDS
    oddr = get_one(cells, r"u_rwds_oddr$", "rwds ODDR")
    iobuf = get_one(cells, r"u_rwds_iobuf$", "rwds IOBUF")
    iddr = get_one(cells, r"u_rwds_iddr$", "rwds IDDR")
    if None not in (oddr, iobuf, iddr):
        check_trio(oddr, iobuf, iddr, RWDS_PAD, "rwds")
        if net(oddr, "CLK") != clkout or net(iddr, "CLK") != clkout:
            err("rwds: ODDR/IDDR の CLK が CLKOUT でない")

    # CK/CK# (CLKOUTP) と CS#/RESET# (CLKOUT)
    check_sdr_out(cells, r"u_ck_oddr$", CK_PAD, clkoutp, "ck")
    check_sdr_out(cells, r"u_ck_n_oddr$", CK_N_PAD, clkoutp, "ck_n")
    check_sdr_out(cells, r"u_cs_oddr$", CS_N_PAD, clkout, "cs_n")
    check_sdr_out(cells, r"u_reset_oddr$", RESET_N_PAD, clkout, "reset_n")

    if errors:
        print(f"\nFAIL: {len(errors)} 件の不一致")
        return 1
    print("OK: PsramPhy の接続・配置・クロックはすべて期待どおり")
    return 0


if __name__ == "__main__":
    sys.exit(main())
