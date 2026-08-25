#!/usr/bin/env python3
"""fat32_host 書き込み後の FAT32 構造を独立に検査する。"""

from __future__ import annotations

import sys
from pathlib import Path

SECTOR = 512
EOC = 0x0FFFFFF8


def u16(data: bytes, off: int) -> int:
    return int.from_bytes(data[off : off + 2], "little")


def u32(data: bytes, off: int) -> int:
    return int.from_bytes(data[off : off + 4], "little")


def require(cond: bool, message: str) -> None:
    if not cond:
        raise ValueError(message)


def verify(path: Path) -> None:
    image = path.read_bytes()
    require(len(image) % SECTOR == 0, "image size is not sector aligned")

    sector0 = image[:SECTOR]
    if (
        sector0[0] in (0xEB, 0xE9)
        and u16(sector0, 11) == SECTOR
        and sector0[510:512] == b"\x55\xaa"
    ):
        base = 0
    else:
        require(sector0[510:512] == b"\x55\xaa", "MBR signature")
        require(sector0[450] in (0x0B, 0x0C), "FAT32 partition type")
        base = u32(sector0, 454)

    bpb = image[base * SECTOR : (base + 1) * SECTOR]
    require(len(bpb) == SECTOR, "BPB is outside image")
    require(u16(bpb, 11) == SECTOR, "BPB bytes/sector")
    sectors_per_cluster = bpb[13]
    reserved = u16(bpb, 14)
    fat_count = bpb[16]
    total_sectors = u32(bpb, 32)
    fat_sectors = u32(bpb, 36)
    root_cluster = u32(bpb, 44)
    require(sectors_per_cluster > 0, "BPB sectors/cluster")
    require(reserved > 0 and fat_count > 0 and fat_sectors > 0, "BPB FAT layout")

    fat_start = base + reserved
    data_start = fat_start + fat_count * fat_sectors
    fat_bytes = fat_sectors * SECTOR
    fats = [
        image[(fat_start + i * fat_sectors) * SECTOR :][0:fat_bytes]
        for i in range(fat_count)
    ]
    require(all(len(fat) == fat_bytes for fat in fats), "FAT is outside image")
    require(all(fat == fats[0] for fat in fats[1:]), "FAT copies differ")
    fat = fats[0]

    data_sectors = total_sectors - (data_start - base)
    max_cluster = min(
        data_sectors // sectors_per_cluster + 1,
        len(fat) // 4 - 1,
    )

    def fat_value(cluster: int) -> int:
        return u32(fat, cluster * 4) & 0x0FFFFFFF

    owners: dict[int, bytes] = {}

    def walk_chain(start: int, owner: bytes) -> list[int]:
        if start == 0:
            return []
        chain: list[int] = []
        local: set[int] = set()
        cluster = start
        while cluster < EOC:
            require(2 <= cluster <= max_cluster, f"{owner!r}: cluster out of range")
            require(cluster not in local, f"{owner!r}: cluster chain loop")
            require(cluster not in owners, f"cluster {cluster} is shared")
            local.add(cluster)
            owners[cluster] = owner
            chain.append(cluster)
            next_cluster = fat_value(cluster)
            require(next_cluster != 0, f"{owner!r}: chain reaches free cluster")
            cluster = next_cluster
        return chain

    def cluster_sector(cluster: int) -> int:
        return data_start + (cluster - 2) * sectors_per_cluster

    root_chain = walk_chain(root_cluster, b"<root>")
    found_boot = False
    stop = False
    for cluster in root_chain:
        for sector_index in range(sectors_per_cluster):
            lba = cluster_sector(cluster) + sector_index
            sector = image[lba * SECTOR : (lba + 1) * SECTOR]
            require(len(sector) == SECTOR, "root directory is outside image")
            for off in range(0, SECTOR, 32):
                entry = sector[off : off + 32]
                if entry[0] == 0:
                    stop = True
                    break
                if entry[0] == 0xE5 or entry[11] & 0x08:
                    continue

                name = entry[0:11]
                start = u16(entry, 20) << 16 | u16(entry, 26)
                size = u32(entry, 28)
                chain = walk_chain(start, name)
                if not entry[11] & 0x10:
                    required = (
                        size + sectors_per_cluster * SECTOR - 1
                    ) // (sectors_per_cluster * SECTOR)
                    require(len(chain) == required, f"{name!r}: chain length")
                if name == b"BOOT    BIN":
                    require(size == 1700, "BOOT.BIN final size")
                    found_boot = True
            if stop:
                break
        if stop:
            break

    require(found_boot, "BOOT.BIN directory entry")
    allocated = {
        cluster
        for cluster in range(2, max_cluster + 1)
        if fat_value(cluster) != 0
    }
    require(allocated == set(owners), "allocated orphan cluster")
    print(
        f"  {path}: FAT copies/cluster ownership OK "
        f"({len(allocated)} allocated clusters)"
    )


def main() -> int:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <image>...", file=sys.stderr)
        return 2
    try:
        for arg in sys.argv[1:]:
            verify(Path(arg))
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
