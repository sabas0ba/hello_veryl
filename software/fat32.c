#include "fat32.h"

#define SECTOR 512

static unsigned char sec[SECTOR];

static struct {
    unsigned int fat_start;    /* FAT 領域の先頭 LBA */
    unsigned int data_start;   /* データ領域の先頭 LBA (クラスタ 2 の位置) */
    unsigned int sec_per_clus; /* クラスタあたりのセクタ数 */
    unsigned int root_clus;    /* ルートディレクトリの先頭クラスタ */
} fs;

static unsigned int rd16(const unsigned char *p)
{
    return (unsigned int) p[0] | ((unsigned int) p[1] << 8);
}

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int) p[0] | ((unsigned int) p[1] << 8)
         | ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

static unsigned int clus_lba(unsigned int c)
{
    return fs.data_start + (c - 2) * fs.sec_per_clus;
}

/* BPB を検証してレイアウトを決める．sec に BPB が入っている前提 */
static int take_bpb(unsigned int base)
{
    unsigned int rsvd, nfat, fatsz;

    if (rd16(sec + 11) != SECTOR) {
        return FAT_ERR_NO_FS;
    }
    if (rd16(sec + 22) != 0) { /* FATSz16 != 0 は FAT12/16 */
        return FAT_ERR_NO_FS;
    }
    fs.sec_per_clus = sec[13];
    rsvd            = rd16(sec + 14);
    nfat            = sec[16];
    fatsz           = rd32(sec + 36);
    fs.root_clus    = rd32(sec + 44);
    if (fs.sec_per_clus == 0 || rsvd == 0 || nfat == 0 || fatsz == 0
        || fs.root_clus < 2) {
        return FAT_ERR_NO_FS;
    }
    fs.fat_start  = base + rsvd;
    fs.data_start = fs.fat_start + nfat * fatsz;
    return FAT_OK;
}

int fat_mount(void)
{
    unsigned int part;
    int          sig_ok, jump_ok;

    if (fat_dev_read(0, sec) != 0) {
        return FAT_ERR_IO;
    }
    sig_ok  = (sec[510] == 0x55 && sec[511] == 0xaa);
    jump_ok = (sec[0] == 0xeb || sec[0] == 0xe9);

    /* Fat32Reader と同じ判定: JMP + BytsPerSec=512 + シグネチャ なら
     * スーパーフロッピー (セクタ 0 が BPB)．そうでなければ MBR の
     * パーティション 1 をタイプ 0Bh/0Ch のときだけ使う． */
    if (jump_ok && rd16(sec + 11) == SECTOR && sig_ok) {
        return take_bpb(0);
    }
    if (!sig_ok) {
        return FAT_ERR_NO_FS;
    }
    if (sec[446 + 4] != 0x0b && sec[446 + 4] != 0x0c) {
        return FAT_ERR_NO_FS;
    }
    part = rd32(sec + 446 + 8);
    if (part == 0) {
        return FAT_ERR_NO_FS;
    }
    if (fat_dev_read(part, sec) != 0) {
        return FAT_ERR_IO;
    }
    if (sec[510] != 0x55 || sec[511] != 0xaa) {
        return FAT_ERR_NO_FS;
    }
    return take_bpb(part);
}

/* クラスタチェーンの次を引く．終端・不正なら 0 を返す */
static unsigned int next_clus(unsigned int c)
{
    unsigned int off = c * 4;
    unsigned int v;

    if (fat_dev_read(fs.fat_start + off / SECTOR, sec) != 0) {
        return 0;
    }
    v = rd32(sec + (off % SECTOR)) & 0x0fffffffu;
    if (v < 2 || v >= 0x0ffffff8u) {
        return 0;
    }
    return v;
}

int fat_open(const char *name11, fat_file *f)
{
    unsigned int c = fs.root_clus;

    while (c != 0) {
        unsigned int s;

        for (s = 0; s < fs.sec_per_clus; s++) {
            unsigned int e;

            if (fat_dev_read(clus_lba(c) + s, sec) != 0) {
                return FAT_ERR_IO;
            }
            for (e = 0; e < SECTOR; e += 32) {
                const unsigned char *d = sec + e;
                int                  i, hit;

                if (d[0] == 0x00) { /* 以降は未使用 */
                    return FAT_ERR_FOUND;
                }
                if (d[0] == 0xe5) { /* 削除済み */
                    continue;
                }
                if ((d[11] & 0x08) != 0 || (d[11] & 0x10) != 0) {
                    continue; /* ボリュームラベル (LFN 含む) とディレクトリ */
                }
                hit = 1;
                for (i = 0; i < 11; i++) {
                    if ((char) d[i] != name11[i]) {
                        hit = 0;
                        break;
                    }
                }
                if (hit) {
                    f->start_cluster = ((unsigned int) rd16(d + 20) << 16)
                                     | rd16(d + 26);
                    f->size          = rd32(d + 28);
                    return FAT_OK;
                }
            }
        }
        c = next_clus(c);
    }
    return FAT_ERR_FOUND;
}

int fat_read(const fat_file *f, void *dst, unsigned int max_len,
             unsigned int *out_len)
{
    unsigned char *out  = (unsigned char *) dst;
    unsigned int   left = f->size;
    unsigned int   done = 0;
    unsigned int   c    = f->start_cluster;

    if (left > max_len) {
        left = max_len;
    }
    if (left != 0 && c < 2) {
        return FAT_ERR_CHAIN;
    }
    while (left != 0) {
        unsigned int s;

        if (c < 2) {
            return FAT_ERR_CHAIN;
        }
        for (s = 0; s < fs.sec_per_clus && left != 0; s++) {
            unsigned int n = left < SECTOR ? left : SECTOR;
            unsigned int i;

            if (fat_dev_read(clus_lba(c) + s, sec) != 0) {
                return FAT_ERR_IO;
            }
            for (i = 0; i < n; i++) {
                out[done + i] = sec[i];
            }
            done += n;
            left -= n;
        }
        if (left != 0) {
            c = next_clus(c);
        }
    }
    if (out_len != 0) {
        *out_len = done;
    }
    return FAT_OK;
}
