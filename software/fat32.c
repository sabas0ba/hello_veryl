#include "fat32.h"

#define SECTOR 512

static unsigned char sec[SECTOR];

static struct {
    unsigned int fat_start;    /* FAT 領域の先頭 LBA */
    unsigned int data_start;   /* データ領域の先頭 LBA (クラスタ 2 の位置) */
    unsigned int sec_per_clus; /* クラスタあたりのセクタ数 */
    unsigned int root_clus;    /* ルートディレクトリの先頭クラスタ */
    unsigned int num_fats;     /* FAT の面数 (書き込み時は全面を更新する) */
    unsigned int fat_size;     /* FAT 1 面のセクタ数 */
    unsigned int max_clus;     /* 使用できる最大クラスタ番号 */
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

static void wr32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16);
    p[3] = (unsigned char) (v >> 24);
}

static void wr16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
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
    fs.num_fats   = nfat;
    fs.fat_size   = fatsz;
    fs.fat_start  = base + rsvd;
    fs.data_start = fs.fat_start + nfat * fatsz;

    /* 使用できる最大クラスタ番号．総セクタ数とデータ領域の起点から求める */
    {
        unsigned int tot = rd32(sec + 32);
        unsigned int data_sec;

        if (tot == 0) {
            return FAT_ERR_NO_FS;
        }
        data_sec     = tot - (fs.data_start - base);
        fs.max_clus  = data_sec / fs.sec_per_clus + 1;
        /* FAT に収まる範囲も超えないようにする */
        if (fs.max_clus > fatsz * (SECTOR / 4) - 1) {
            fs.max_clus = fatsz * (SECTOR / 4) - 1;
        }
        if (fs.max_clus > 0x0fffffefu) {
            fs.max_clus = 0x0fffffefu;
        }
    }
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
/* ---- ここから書き込み系 (docs/riscv.md「TF カードからのブート」) ---- */

/* ディレクトリエントリの所在と内容 */
typedef struct {
    unsigned int lba;  /* エントリを含むセクタ */
    unsigned int off;  /* セクタ内オフセット (32 の倍数) */
    unsigned int clus; /* 先頭クラスタ */
    unsigned int size; /* バイト数 */
} dir_ent;

/* ルートディレクトリを走査する．
 * name11 が一致すれば found へ入れて FAT_OK．
 * 見つからなければ最初の空きスロットを free_ent へ入れて FAT_ERR_FOUND． */
static int dir_scan(const char *name11, dir_ent *found, dir_ent *free_ent)
{
    unsigned int c         = fs.root_clus;
    int          have_free = 0;

    if (free_ent != 0) {
        free_ent->lba = 0;
    }
    while (c >= 2) {
        unsigned int s;

        for (s = 0; s < fs.sec_per_clus; s++) {
            unsigned int lba = clus_lba(c) + s;
            unsigned int e;

            if (fat_dev_read(lba, sec) != 0) {
                return FAT_ERR_IO;
            }
            for (e = 0; e < SECTOR; e += 32) {
                const unsigned char *d = sec + e;

                if (d[0] == 0x00 || d[0] == 0xe5) {
                    if (!have_free && free_ent != 0) {
                        free_ent->lba = lba;
                        free_ent->off = e;
                        have_free     = 1;
                    }
                    if (d[0] == 0x00) {
                        return FAT_ERR_FOUND; /* 以降は未使用 */
                    }
                    continue;
                }
                if ((d[11] & 0x08) != 0 || (d[11] & 0x10) != 0) {
                    continue; /* ボリュームラベル (LFN 含む) とディレクトリ */
                }
                {
                    int i, hit = 1;

                    for (i = 0; i < 11; i++) {
                        if ((char) d[i] != name11[i]) {
                            hit = 0;
                            break;
                        }
                    }
                    if (hit && found != 0) {
                        found->lba  = lba;
                        found->off  = e;
                        found->clus = ((unsigned int) rd16(d + 20) << 16)
                                    | rd16(d + 26);
                        found->size = rd32(d + 28);
                        return FAT_OK;
                    }
                }
            }
        }
        c = next_clus(c);
    }
    return FAT_ERR_FOUND;
}

/* FAT エントリを読む */
static int fat_get(unsigned int c, unsigned int *val)
{
    unsigned int off = c * 4;

    if (fat_dev_read(fs.fat_start + off / SECTOR, sec) != 0) {
        return FAT_ERR_IO;
    }
    *val = rd32(sec + (off % SECTOR)) & 0x0fffffffu;
    return FAT_OK;
}

/* FAT エントリを書く．全面 (num_fats) を更新する */
static int fat_write_entry(unsigned int copy, unsigned int c,
                           unsigned int val)
{
    unsigned int off = c * 4;
    unsigned int s   = off / SECTOR;
    unsigned int lba = fs.fat_start + copy * fs.fat_size + s;
    unsigned int cur;

    if (fat_dev_read(lba, sec) != 0) {
        return FAT_ERR_IO;
    }
    cur = rd32(sec + (off % SECTOR));
    wr32(sec + (off % SECTOR), (cur & 0xf0000000u) | (val & 0x0fffffffu));
    if (fat_dev_write(lba, sec) != 0) {
        return FAT_ERR_IO;
    }
    return FAT_OK;
}

static int fat_set(unsigned int c, unsigned int val)
{
    unsigned int old;
    unsigned int i;
    int          rc;

    rc = fat_get(c, &old);
    if (rc != FAT_OK) {
        return rc;
    }
    for (i = 0; i < fs.num_fats; i++) {
        unsigned int j;

        rc = fat_write_entry(i, c, val);
        if (rc != FAT_OK) {
            /* The failed write may have reached the medium. Restore every
             * copy touched so far, including the failing copy. */
            for (j = 0; j <= i; j++) {
                (void) fat_write_entry(j, c, old);
            }
            return FAT_ERR_IO;
        }
    }
    return FAT_OK;
}

/* 空きクラスタを 1 つ確保して終端にする．prev >= 2 ならそこへ繋ぐ */
static int fat_alloc(unsigned int prev, unsigned int *out)
{
    unsigned int c;

    for (c = 2; c <= fs.max_clus; c++) {
        unsigned int off = c * 4;
        unsigned int v;
        int          rc;

        /* FAT セクタは境界をまたぐときだけ読み直す */
        if (c == 2 || off % SECTOR == 0) {
            if (fat_dev_read(fs.fat_start + off / SECTOR, sec) != 0) {
                return FAT_ERR_IO;
            }
        }
        v = rd32(sec + (off % SECTOR)) & 0x0fffffffu;
        if (v != 0) {
            continue;
        }
        rc = fat_set(c, 0x0ffffff8u); /* 終端 */
        if (rc != FAT_OK) {
            return rc;
        }
        if (prev >= 2) {
            rc = fat_set(prev, c);
            if (rc != FAT_OK) {
                (void) fat_set(c, 0);
                return rc;
            }
        }
        *out = c;
        return FAT_OK;
    }
    return FAT_ERR_FULL;
}

/* c から先のチェーンをすべて解放する */
static int fat_free_chain(unsigned int c)
{
    while (c >= 2 && c < 0x0ffffff8u) {
        unsigned int nx;
        int          rc = fat_get(c, &nx);

        if (rc != FAT_OK) {
            return rc;
        }
        rc = fat_set(c, 0);
        if (rc != FAT_OK) {
            return rc;
        }
        c = nx;
    }
    return FAT_OK;
}

static int fat_chain_info(unsigned int c, unsigned int *count,
                          unsigned int *tail, unsigned int *terminal)
{
    unsigned int n = 0;

    *count    = 0;
    *tail     = 0;
    *terminal = 0;
    if (c == 0) {
        return FAT_OK;
    }
    while (c >= 2 && c <= fs.max_clus) {
        unsigned int nx;
        int          rc;

        if (n >= fs.max_clus - 1) {
            return FAT_ERR_CHAIN;
        }
        n++;
        *tail = c;
        rc = fat_get(c, &nx);
        if (rc != FAT_OK) {
            return rc;
        }
        if (nx >= 0x0ffffff8u) {
            *count    = n;
            *terminal = nx;
            return FAT_OK;
        }
        if (nx < 2 || nx > fs.max_clus) {
            return FAT_ERR_CHAIN;
        }
        c = nx;
    }
    return FAT_ERR_CHAIN;
}

static int fat_have_free(unsigned int need)
{
    unsigned int c;

    if (need == 0) {
        return FAT_OK;
    }
    for (c = 2; c <= fs.max_clus; c++) {
        unsigned int off = c * 4;
        unsigned int v;

        if (c == 2 || off % SECTOR == 0) {
            if (fat_dev_read(fs.fat_start + off / SECTOR, sec) != 0) {
                return FAT_ERR_IO;
            }
        }
        v = rd32(sec + (off % SECTOR)) & 0x0fffffffu;
        if (v == 0 && --need == 0) {
            return FAT_OK;
        }
    }
    return FAT_ERR_FULL;
}

static int fat_rollback_alloc(unsigned int old_tail,
                              unsigned int old_terminal,
                              unsigned int first_new)
{
    if (first_new < 2) {
        return FAT_OK;
    }
    if (old_tail >= 2 && fat_set(old_tail, old_terminal) != FAT_OK) {
        return FAT_ERR_IO;
    }
    return fat_free_chain(first_new);
}

int fat_write_file(const char *name11, const void *src, unsigned int len)
{
    const unsigned char *p = (const unsigned char *) src;
    dir_ent              ent;
    dir_ent              slot;
    unsigned int         cpb; /* クラスタあたりバイト数 */
    unsigned int         need;
    unsigned int         first;
    unsigned int         c, prev;
    unsigned int         old_count, old_tail, old_terminal;
    unsigned int         first_new = 0;
    unsigned int         drop = 0;
    unsigned int         done = 0;
    unsigned int         i;
    int                  rc;
    int                  rollback_rc;
    int                  existed;

    if (fs.sec_per_clus == 0) {
        return FAT_ERR_NO_FS;
    }
    cpb  = fs.sec_per_clus * SECTOR;
    need = len == 0 ? 0 : 1 + (len - 1) / cpb;

    rc      = dir_scan(name11, &ent, &slot);
    existed = (rc == FAT_OK);
    if (!existed && rc != FAT_ERR_FOUND) {
        return rc;
    }
    if (!existed) {
        if (slot.lba == 0) {
            return FAT_ERR_DIR; /* ルートディレクトリに空きがない */
        }
        ent      = slot;
        ent.clus = 0;
        ent.size = 0;
    }

    rc = fat_chain_info(ent.clus, &old_count, &old_tail, &old_terminal);
    if (rc != FAT_OK) {
        return rc;
    }
    rc = fat_have_free(need > old_count ? need - old_count : 0);
    if (rc != FAT_OK) {
        return rc;
    }

    /* クラスタチェーンを必要な長さに整える */
    first = ent.clus;
    prev  = 0;
    c     = ent.clus;
    for (i = 0; i < need; i++) {
        if (c < 2 || c >= 0x0ffffff8u) {
            unsigned int nc;

            rc = fat_alloc(prev, &nc);
            if (rc != FAT_OK) {
                goto rollback;
            }
            if (first_new == 0) {
                first_new = nc;
            }
            if (i == 0) {
                first = nc;
            }
            c = nc;
        }
        prev = c;
        rc   = fat_get(c, &c);
        if (rc != FAT_OK) {
            goto rollback;
        }
    }
    if (need > 0) {
        if (c >= 2 && c < 0x0ffffff8u) {
            drop = c;
        }
    } else if (first >= 2) {
        drop  = first;
        first = 0;
    }

    /* データを書く */
    c = first;
    while (done < len) {
        unsigned int s;

        if (c < 2 || c >= 0x0ffffff8u) {
            rc = FAT_ERR_CHAIN;
            goto rollback;
        }
        for (s = 0; s < fs.sec_per_clus && done < len; s++) {
            unsigned int n = len - done;
            unsigned int k;

            if (n > SECTOR) {
                n = SECTOR;
            }
            for (k = 0; k < SECTOR; k++) {
                sec[k] = k < n ? p[done + k] : 0;
            }
            if (fat_dev_write(clus_lba(c) + s, sec) != 0) {
                rc = FAT_ERR_IO;
                goto rollback;
            }
            done += n;
        }
        if (done < len) {
            rc = fat_get(c, &c);
            if (rc != FAT_OK) {
                goto rollback;
            }
        }
    }

    /* ディレクトリエントリを更新する */
    if (fat_dev_read(ent.lba, sec) != 0) {
        rc = FAT_ERR_IO;
        goto rollback;
    }
    {
        unsigned char *d = sec + ent.off;

        if (!existed) {
            for (i = 0; i < 32; i++) {
                d[i] = 0;
            }
            for (i = 0; i < 11; i++) {
                d[i] = (unsigned char) name11[i];
            }
            d[11] = 0x20; /* archive */
        }
        wr16(d + 20, first >> 16);
        wr16(d + 26, first & 0xffffu);
        wr32(d + 28, len);
    }
    if (fat_dev_write(ent.lba, sec) != 0) {
        rc = FAT_ERR_IO;
        goto rollback;
    }

    /* Directory metadata is durable before an old tail is detached. */
    if (drop >= 2) {
        if (need > 0) {
            rc = fat_set(prev, 0x0ffffff8u);
            if (rc != FAT_OK) {
                return rc;
            }
        }
        rc = fat_free_chain(drop);
        if (rc != FAT_OK) {
            return rc;
        }
    }
    return FAT_OK;

rollback:
    rollback_rc = fat_rollback_alloc(old_tail, old_terminal, first_new);
    return rollback_rc != FAT_OK ? rollback_rc : rc;
}
