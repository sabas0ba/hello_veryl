/* software/fat32.c をホスト上で検証する (docs/riscv.md「TF カードからのブート」)
 *
 * scripts/gen_tf_test_image.py --raw が書き出した生イメージを
 * セクタ単位で読ませ，ハードウェアの Fat32Reader の L1 テストと
 * 同じ内容 (README.TXT の断片化チェーン，HELLO.TXT，DATA.BIN，
 * LFN/ボリュームラベル/削除済み/サブディレクトリの読み飛ばし) を確認する．
 */
#include <stdio.h>
#include <string.h>

#include "../../software/fat32.h"

#define SECTOR 512
#define IMAGE_BYTES (128u * SECTOR)
#define BOUNDARY_CLUSTER 50u
#define FAT_SCAN_POISON_CLUSTER 120u
#define NO_FAIL_LBA 0xffffffffu

static FILE *img;
static int   fail_writes_after = -1;
static int   report_error_after_write = -1;
static unsigned int fail_read_lba = NO_FAIL_LBA;
static unsigned char image_before[IMAGE_BYTES];
static unsigned char image_after[IMAGE_BYTES];
static unsigned char sentinel_before[SECTOR];

int fat_dev_read(unsigned int lba, unsigned char *buf)
{
    if (lba == fail_read_lba) {
        fail_read_lba = NO_FAIL_LBA;
        return 1;
    }
    if (fseek(img, (long) lba * SECTOR, SEEK_SET) != 0) {
        return 1;
    }
    if (fread(buf, 1, SECTOR, img) != SECTOR) {
        return 1;
    }
    return 0;
}

int fat_dev_write(unsigned int lba, const unsigned char *buf)
{
    int report_error = 0;

    if (fail_writes_after == 0) {
        fail_writes_after = -1;
        return 1;
    }
    if (fail_writes_after > 0) {
        fail_writes_after--;
    }
    if (report_error_after_write == 0) {
        report_error_after_write = -1;
        report_error = 1;
    } else if (report_error_after_write > 0) {
        report_error_after_write--;
    }
    if (fseek(img, (long) lba * SECTOR, SEEK_SET) != 0) {
        return 1;
    }
    if (fwrite(buf, 1, SECTOR, img) != SECTOR) {
        return 1;
    }
    return report_error;
}

static int fails;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("ERROR: %s\n", what);
        fails++;
    }
}

static int snapshot_image(unsigned char *buf)
{
    if (fflush(img) != 0 || fseek(img, 0, SEEK_SET) != 0) {
        return 1;
    }
    return fread(buf, 1, IMAGE_BYTES, img) != IMAGE_BYTES;
}

static int restore_image(const unsigned char *buf)
{
    if (fseek(img, 0, SEEK_SET) != 0
        || fwrite(buf, 1, IMAGE_BYTES, img) != IMAGE_BYTES) {
        return 1;
    }
    return fflush(img) != 0;
}

static void check_image_unchanged(const char *label)
{
    int rc = snapshot_image(image_after);

    check(rc == 0, "snapshot after failed write");
    if (rc == 0) {
        check(memcmp(image_before, image_after, IMAGE_BYTES) == 0, label);
    }
}

static void make_dir_entry(unsigned char *d, const char *name11,
                           unsigned int attr)
{
    memset(d, 0, 32);
    memcpy(d, name11, 11);
    d[11] = (unsigned char) attr;
}

typedef struct {
    unsigned int base;
    unsigned int fat_start;
    unsigned int fat_size;
    unsigned int num_fats;
    unsigned int data_start;
    unsigned int sec_per_clus;
    unsigned int root_clus;
    unsigned int fsinfo_lba;
} test_layout;

static unsigned int host_rd16(const unsigned char *p)
{
    return (unsigned int) p[0] | ((unsigned int) p[1] << 8);
}

static unsigned int host_rd32(const unsigned char *p)
{
    return (unsigned int) p[0] | ((unsigned int) p[1] << 8)
         | ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

static void host_wr32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
    p[2] = (unsigned char) (v >> 16);
    p[3] = (unsigned char) (v >> 24);
}

static void host_wr16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char) v;
    p[1] = (unsigned char) (v >> 8);
}

static int load_layout(test_layout *layout)
{
    static unsigned char buf[SECTOR];
    unsigned int         base;
    unsigned int         reserved;

    if (fat_dev_read(0, buf) != 0) {
        return 1;
    }
    if ((buf[0] == 0xeb || buf[0] == 0xe9)
        && host_rd16(buf + 11) == SECTOR) {
        base = 0;
    } else {
        base = host_rd32(buf + 446 + 8);
        if (base == 0 || fat_dev_read(base, buf) != 0) {
            return 1;
        }
    }
    reserved             = host_rd16(buf + 14);
    layout->base         = base;
    layout->sec_per_clus = buf[13];
    layout->num_fats     = buf[16];
    layout->fat_size     = host_rd32(buf + 36);
    layout->root_clus    = host_rd32(buf + 44);
    layout->fsinfo_lba   = base + host_rd16(buf + 48);
    layout->fat_start    = base + reserved;
    layout->data_start   = layout->fat_start
                         + layout->num_fats * layout->fat_size;
    return layout->sec_per_clus == 0 || layout->num_fats == 0
        || layout->fat_size == 0 || layout->root_clus < 2;
}

static unsigned int test_clus_lba(const test_layout *layout,
                                  unsigned int cluster)
{
    return layout->data_start
         + (cluster - 2) * layout->sec_per_clus;
}

static int set_fat_entry_copy(const test_layout *layout, unsigned int copy,
                              unsigned int cluster, unsigned int value)
{
    static unsigned char buf[SECTOR];
    unsigned int         off = cluster * 4;
    unsigned int         lba = layout->fat_start
                             + copy * layout->fat_size + off / SECTOR;
    unsigned int         old;

    if (copy >= layout->num_fats || fat_dev_read(lba, buf) != 0) {
        return 1;
    }
    old = host_rd32(buf + off % SECTOR);
    host_wr32(buf + off % SECTOR,
              (old & 0xf0000000u) | (value & 0x0fffffffu));
    return fat_dev_write(lba, buf);
}

static int get_fat_entry_copy(const test_layout *layout, unsigned int copy,
                              unsigned int cluster, unsigned int *value)
{
    static unsigned char buf[SECTOR];
    unsigned int         off = cluster * 4;
    unsigned int         lba = layout->fat_start
                             + copy * layout->fat_size + off / SECTOR;

    if (copy >= layout->num_fats || fat_dev_read(lba, buf) != 0) {
        return 1;
    }
    *value = host_rd32(buf + off % SECTOR) & 0x0fffffffu;
    return 0;
}

static int set_fat_entry(const test_layout *layout, unsigned int cluster,
                         unsigned int value)
{
    unsigned int copy;

    for (copy = 0; copy < layout->num_fats; copy++) {
        if (set_fat_entry_copy(layout, copy, cluster, value) != 0) {
            return 1;
        }
    }
    return 0;
}

static int seed_next_cluster_end(const test_layout *layout)
{
    static unsigned char buf[SECTOR];
    unsigned int         root_lba = test_clus_lba(layout, layout->root_clus);
    unsigned int         e;

    if (layout->sec_per_clus < 2
        || fat_dev_read(root_lba + layout->sec_per_clus - 1, buf) != 0) {
        return 1;
    }
    for (e = 0; e + 32 < SECTOR; e += 32) {
        make_dir_entry(buf + e, "FILLER     ", 0x08);
    }
    buf[SECTOR - 32] = 0;
    if (fat_dev_write(root_lba + layout->sec_per_clus - 1, buf) != 0
        || set_fat_entry(layout, layout->root_clus, BOUNDARY_CLUSTER) != 0
        || set_fat_entry(layout, BOUNDARY_CLUSTER, 0x0ffffff8u) != 0
        || fat_dev_read(test_clus_lba(layout, BOUNDARY_CLUSTER), buf) != 0) {
        return 1;
    }
    make_dir_entry(buf, "STALE   BIN", 0x20);
    return fat_dev_write(test_clus_lba(layout, BOUNDARY_CLUSTER), buf);
}

static int seed_chain_tail(const test_layout *layout)
{
    static unsigned char buf[SECTOR];
    unsigned int         base = test_clus_lba(layout, BOUNDARY_CLUSTER);
    unsigned int         s;

    for (s = 0; s < layout->sec_per_clus; s++) {
        unsigned int e;

        if (fat_dev_read(base + s, buf) != 0) {
            return 1;
        }
        for (e = 0; e < SECTOR; e += 32) {
            if (s + 1 == layout->sec_per_clus && e + 32 == SECTOR) {
                buf[e] = 0;
            } else {
                make_dir_entry(buf + e, "FILLER     ", 0x08);
            }
        }
        if (fat_dev_write(base + s, buf) != 0) {
            return 1;
        }
    }
    for (s = 0; s < SECTOR; s++) {
        buf[s] = (unsigned char) (5u + s * 13u);
    }
    memcpy(sentinel_before, buf, SECTOR);
    return fat_dev_write(base + layout->sec_per_clus, buf);
}

static int check_sentinel(const test_layout *layout)
{
    static unsigned char buf[SECTOR];
    unsigned int         lba = test_clus_lba(layout, BOUNDARY_CLUSTER)
                             + layout->sec_per_clus;

    return fat_dev_read(lba, buf) == 0
        && memcmp(buf, sentinel_before, SECTOR) == 0;
}

/* Place a stale entry immediately after the logical end marker. When
 * sector_end is set, move the marker to the last slot so its successor is
 * in the next sector. */
static int seed_stale_after_end(int sector_end)
{
    static unsigned char buf[SECTOR];
    unsigned int         lba;

    for (lba = 0; lba < IMAGE_BYTES / SECTOR; lba++) {
        unsigned int e;
        int          root = 0;

        if (fat_dev_read(lba, buf) != 0) {
            return 1;
        }
        for (e = 0; e < SECTOR; e += 32) {
            if (memcmp(buf + e, "README  TXT", 11) == 0) {
                root = 1;
                break;
            }
        }
        if (!root) {
            continue;
        }
        for (e = 0; e < SECTOR; e += 32) {
            if (buf[e] != 0) {
                continue;
            }
            if (!sector_end) {
                if (e + 64 > SECTOR) {
                    return 1;
                }
                make_dir_entry(buf + e + 32, "STALE   BIN", 0x20);
                return fat_dev_write(lba, buf);
            }
            while (e + 32 < SECTOR) {
                make_dir_entry(buf + e, "FILLER     ", 0x08);
                e += 32;
            }
            buf[e] = 0;
            if (fat_dev_write(lba, buf) != 0
                || fat_dev_read(lba + 1, buf) != 0) {
                return 1;
            }
            make_dir_entry(buf, "STALE   BIN", 0x20);
            return fat_dev_write(lba + 1, buf);
        }
        return 1;
    }
    return 1;
}

static void check_file(const char *name11, unsigned int want_size,
                       const unsigned char *want, const char *label)
{
    fat_file     f;
    unsigned int got = 0;
    static unsigned char buf[4096];
    int          rc;

    rc = fat_open(name11, &f);
    if (rc != FAT_OK) {
        printf("ERROR: %s: fat_open rc=%d\n", label, rc);
        fails++;
        return;
    }
    if (f.size != want_size) {
        printf("ERROR: %s: size %u (期待 %u)\n", label, f.size, want_size);
        fails++;
        return;
    }
    rc = fat_read(&f, buf, sizeof buf, &got);
    if (rc != FAT_OK) {
        printf("ERROR: %s: fat_read rc=%d\n", label, rc);
        fails++;
        return;
    }
    if (got != want_size) {
        printf("ERROR: %s: 読めたのは %u byte (期待 %u)\n", label, got, want_size);
        fails++;
        return;
    }
    if (memcmp(buf, want, want_size) != 0) {
        unsigned int i;
        for (i = 0; i < want_size; i++) {
            if (buf[i] != want[i]) {
                printf("ERROR: %s: offset %u が %02x (期待 %02x)\n",
                       label, i, buf[i], want[i]);
                break;
            }
        }
        fails++;
        return;
    }
    printf("  %-12s size=%-5u OK\n", label, want_size);
}

static void fill_payload(unsigned char *buf, unsigned int len,
                         unsigned int seed)
{
    unsigned int i;

    for (i = 0; i < len; i++) {
        buf[i] = (unsigned char) (seed + i * 17u);
    }
}

static void check_fsinfo(const test_layout *layout, int unknown,
                         const char *label)
{
    static unsigned char buf[SECTOR];
    unsigned int         free_count;
    unsigned int         next_free;
    int                  rc = fat_dev_read(layout->fsinfo_lba, buf);

    check(rc == 0, label);
    if (rc != 0) {
        return;
    }
    check(host_rd32(buf) == 0x41615252u
          && host_rd32(buf + 484) == 0x61417272u
          && host_rd32(buf + 508) == 0xaa550000u,
          "FSInfo signatures are valid");
    free_count = host_rd32(buf + 488);
    next_free  = host_rd32(buf + 492);
    if (unknown) {
        check(free_count == 0xffffffffu && next_free == 0xffffffffu,
              "FSInfo count and hint are unknown after FAT update");
    } else {
        check(free_count != 0xffffffffu && next_free >= 2,
              "generated FSInfo starts with known count and hint");
    }
}

static void check_active_fat(const test_layout *layout,
                             const unsigned char *readme,
                             unsigned int readme_len)
{
    static unsigned char saved[IMAGE_BYTES];
    static unsigned char bpb[SECTOR];
    static unsigned char payload[64];
    fat_file             f;
    unsigned int         inactive;
    unsigned int         active;
    int                  rc;
    int                  prepared;

    rc = snapshot_image(saved);
    check(rc == 0, "snapshot before active FAT test");
    if (rc != 0) {
        return;
    }

    prepared = fat_dev_read(layout->base, bpb) == 0;
    if (prepared) {
        host_wr16(bpb + 40, 0x0081u); /* mirroring off, FAT1 active */
        prepared = fat_dev_write(layout->base, bpb) == 0
                && set_fat_entry_copy(layout, 0, 3, 0x0ffffff8u) == 0
                && set_fat_entry_copy(layout, 0, 9, 0) == 0
                && set_fat_entry_copy(layout, 1, 9, 0x0ffffff8u) == 0;
    }
    check(prepared, "prepare non-mirrored active FAT1 image");
    if (prepared) {
        rc = fat_mount();
        check(rc == FAT_OK, "mount non-mirrored active FAT1 image");
        if (rc == FAT_OK) {
            check_file("README  TXT", readme_len, readme,
                       "README through active FAT1");
            fill_payload(payload, sizeof payload, 41);
            rc = fat_write_file("ACTIVE  BIN", payload, sizeof payload);
            check(rc == FAT_OK, "write through active FAT1");
            if (rc == FAT_OK) {
                check_file("ACTIVE  BIN", sizeof payload, payload,
                           "ACTIVE.BIN through FAT1");
                rc = fat_open("ACTIVE  BIN", &f);
                check(rc == FAT_OK && f.start_cluster != 9,
                      "allocator ignores stale free entry in FAT0");
                if (rc == FAT_OK) {
                    check(get_fat_entry_copy(layout, 0, f.start_cluster,
                                             &inactive) == 0
                          && get_fat_entry_copy(layout, 1, f.start_cluster,
                                               &active) == 0
                          && inactive == 0 && active >= 0x0ffffff8u,
                          "non-mirrored write updates only active FAT1");
                }
                check(get_fat_entry_copy(layout, 0, 9, &inactive) == 0
                      && get_fat_entry_copy(layout, 1, 9, &active) == 0
                      && inactive == 0 && active >= 0x0ffffff8u,
                      "active FAT allocation preserves live cluster 9");
            }
        }
    }

    rc = restore_image(saved);
    check(rc == 0, "restore image after active FAT test");
    if (rc == 0) {
        rc = fat_mount();
        check(rc == FAT_OK, "remount restored mirrored image");
    }
}

static void check_bpb_metadata_bounds(const test_layout *layout)
{
    static unsigned char saved[IMAGE_BYTES];
    static unsigned char bpb[SECTOR];
    unsigned int         metadata_sectors;
    int                  rc;
    int                  prepared;

    rc = snapshot_image(saved);
    check(rc == 0, "snapshot before malformed BPB test");
    if (rc != 0) {
        return;
    }

    metadata_sectors = layout->data_start - layout->base;
    prepared = metadata_sectors > 0
            && fat_dev_read(layout->base, bpb) == 0;
    if (prepared) {
        host_wr32(bpb + 32, metadata_sectors - 1);
        prepared = fat_dev_write(layout->base, bpb) == 0;
    }
    check(prepared, "prepare BPB with metadata beyond total sectors");
    if (prepared) {
        rc = fat_mount();
        check(rc == FAT_ERR_NO_FS,
              "reject BPB whose metadata exceeds total sectors");
    }

    rc = restore_image(saved);
    check(rc == 0, "restore image after malformed BPB test");
    if (rc == 0) {
        rc = fat_mount();
        check(rc == FAT_OK, "remount image after malformed BPB test");
    }
}

static void check_bpb_root_bounds(const test_layout *layout)
{
    static unsigned char saved[IMAGE_BYTES];
    static unsigned char bpb[SECTOR];
    int                  rc;
    int                  prepared;

    rc = snapshot_image(saved);
    check(rc == 0, "snapshot before out-of-range root cluster test");
    if (rc != 0) {
        return;
    }

    prepared = fat_dev_read(layout->base, bpb) == 0;
    if (prepared) {
        host_wr32(bpb + 44, 0x0fffffefu);
        prepared = fat_dev_write(layout->base, bpb) == 0;
    }
    check(prepared, "prepare BPB with out-of-range root cluster");
    if (prepared) {
        rc = fat_mount();
        check(rc == FAT_ERR_NO_FS,
              "reject BPB whose root cluster exceeds data range");
    }

    rc = restore_image(saved);
    check(rc == 0, "restore image after out-of-range root cluster test");
    if (rc == 0) {
        rc = fat_mount();
        check(rc == FAT_OK, "remount image after root cluster test");
    }
}

static int seed_late_directory_entry(const test_layout *layout)
{
    static unsigned char buf[SECTOR];
    unsigned int         root_lba = test_clus_lba(layout, layout->root_clus);
    unsigned int         s;

    for (s = 0; s < layout->sec_per_clus; s++) {
        unsigned int e;

        for (e = 0; e < SECTOR; e += 32) {
            make_dir_entry(buf + e, "FILLER     ", 0x08);
        }
        if (s == 0) {
            buf[0] = 0xe5;
        }
        if (fat_dev_write(root_lba + s, buf) != 0) {
            return 1;
        }
    }
    if (set_fat_entry(layout, layout->root_clus, BOUNDARY_CLUSTER) != 0
        || set_fat_entry(layout, BOUNDARY_CLUSTER, 0x0ffffff8u) != 0) {
        return 1;
    }
    memset(buf, 0, sizeof buf);
    make_dir_entry(buf, "LATE    BIN", 0x20);
    return fat_dev_write(test_clus_lba(layout, BOUNDARY_CLUSTER), buf);
}

static void check_directory_link_read_error(const test_layout *layout)
{
    static unsigned char saved[IMAGE_BYTES];
    fat_file             f;
    int                  rc;
    int                  prepared;

    rc = snapshot_image(saved);
    check(rc == 0, "snapshot before directory FAT read error test");
    if (rc != 0) {
        return;
    }
    prepared = seed_late_directory_entry(layout) == 0;
    check(prepared, "seed file in later root directory cluster");
    if (prepared) {
        prepared = snapshot_image(image_before) == 0;
        check(prepared, "snapshot seeded multi-cluster root directory");
    }
    if (prepared) {
        fail_read_lba = layout->fat_start
                      + layout->root_clus * 4 / SECTOR;
        rc = fat_write_file("LATE    BIN", image_before, 0);
        check(rc == FAT_ERR_IO,
              "directory FAT read failure returns FAT_ERR_IO");
        check(fail_read_lba == NO_FAIL_LBA,
              "directory FAT read failure was injected");
        if (fail_read_lba != NO_FAIL_LBA) {
            fail_read_lba = NO_FAIL_LBA;
        }
        check_image_unchanged(
            "directory FAT read failure leaves image unchanged");
        rc = fat_open("LATE    BIN", &f);
        check(rc == FAT_OK && f.size == 0,
              "later directory entry remains unique and visible");
    }

    rc = restore_image(saved);
    check(rc == 0, "restore image after directory FAT read error test");
    if (rc == 0) {
        rc = fat_mount();
        check(rc == FAT_OK, "remount image after directory FAT read test");
    }
}

static void run(const char *path)
{
    static unsigned char readme[2348];
    static unsigned char data[256];
    static unsigned char boot[2500];
    static unsigned char replacement[600];
    static unsigned char oversized[1024u * 1024u];
    const char           hello[] = "Hello, Veryl TF card!\r\n";
    fat_file             f;
    test_layout          layout;
    unsigned int         i;
    unsigned int         boot_start;
    int                  rc;
    int                  snap_ok;

    img = fopen(path, "rb+");
    if (img == NULL) {
        printf("ERROR: %s を開けない\n", path);
        fails++;
        return;
    }
    printf("---- %s ----\n", path);
    fail_writes_after         = -1;
    report_error_after_write = -1;
    fail_read_lba            = NO_FAIL_LBA;

    rc = fat_mount();
    check(rc == FAT_OK, "fat_mount");
    if (rc != FAT_OK) {
        printf("  rc=%d\n", rc);
        fclose(img);
        return;
    }

    for (i = 0; i < sizeof readme; i++) {
        readme[i] = (unsigned char) ((i * 31 + 7) & 0xff);
    }
    for (i = 0; i < sizeof data; i++) {
        data[i] = (unsigned char) i;
    }

    rc = load_layout(&layout);
    check(rc == 0, "load FAT layout");
    if (rc == 0) {
        check_bpb_metadata_bounds(&layout);
        check_bpb_root_bounds(&layout);
        check_directory_link_read_error(&layout);
        check_fsinfo(&layout, 0, "read generated FSInfo");
        check_active_fat(&layout, readme, sizeof readme);
    }

    /* 断片化チェーン 3 -> 5 -> 4 を追えること */
    check_file("README  TXT", sizeof readme, readme, "README.TXT");
    check_file("HELLO   TXT", sizeof hello - 1,
               (const unsigned char *) hello, "HELLO.TXT");
    check_file("DATA    BIN", sizeof data, data, "DATA.BIN");

    /* 存在しない名前 */
    rc = fat_open("NOPE    TXT", &f);
    check(rc == FAT_ERR_FOUND, "存在しない名前は FAT_ERR_FOUND");

    /* サブディレクトリとボリュームラベルは 8.3 名が一致しても拾わない */
    rc = fat_open("SUBDIR     ", &f);
    check(rc == FAT_ERR_FOUND, "サブディレクトリは拾わない");
    rc = fat_open("HELLOVERYL ", &f);
    check(rc == FAT_ERR_FOUND, "ボリュームラベルは拾わない");

    /* 削除済みエントリも拾わない */
    rc = fat_open("XELETED TXT", &f);
    check(rc == FAT_ERR_FOUND, "削除済みエントリは拾わない");

    /* 新規作成と cluster chain の伸縮を同じエントリで確認する．
     * 1 cluster = 1024 byte のテスト画像なので，3 -> 1 -> 0 -> 2 cluster となる． */
    fill_payload(boot, 2500, 3);
    snap_ok = snapshot_image(image_before) == 0;
    check(snap_ok, "snapshot before FSInfo write failure");
    fail_writes_after = 0;
    rc = fat_write_file("BOOT    BIN", boot, 2500);
    check(rc == FAT_ERR_IO, "FSInfo write failure returns FAT_ERR_IO");
    check(fail_writes_after == -1, "FSInfo write failure was injected");
    fail_writes_after = -1;
    if (snap_ok) {
        check_image_unchanged("FSInfo failure leaves image unchanged");
    }

    rc = fat_write_file("BOOT    BIN", boot, 2500);
    check(rc == FAT_OK, "BOOT.BIN を新規作成");
    if (rc == FAT_OK) {
        check_file("BOOT    BIN", 2500, boot, "BOOT.BIN new");
        if (load_layout(&layout) == 0) {
            check_fsinfo(&layout, 1, "read invalidated FSInfo");
        }
    }

    fill_payload(boot, 600, 7);
    rc = fat_write_file("BOOT    BIN", boot, 600);
    check(rc == FAT_OK, "BOOT.BIN を 1 cluster へ縮小");
    if (rc == FAT_OK) {
        check_file("BOOT    BIN", 600, boot, "BOOT.BIN shrink");
    }

    rc = fat_write_file("BOOT    BIN", boot, 0);
    check(rc == FAT_OK, "BOOT.BIN を 0 byte 化");
    if (rc == FAT_OK) {
        check_file("BOOT    BIN", 0, boot, "BOOT.BIN empty");
    }

    fill_payload(boot, 1700, 11);
    rc = fat_write_file("BOOT    BIN", boot, 1700);
    check(rc == FAT_OK, "BOOT.BIN を 2 cluster へ再拡張");
    if (rc == FAT_OK) {
        check_file("BOOT    BIN", 1700, boot, "BOOT.BIN final");
    }

    /* 書き込み後も既存 file の chain と内容が保たれること． */
    /* Capacity failures must not allocate or link any clusters. */
    snap_ok = snapshot_image(image_before) == 0;
    check(snap_ok, "snapshot before new-file FAT_ERR_FULL");
    rc = fat_write_file("HUGE    BIN", oversized, sizeof oversized);
    check(rc == FAT_ERR_FULL, "new file returns FAT_ERR_FULL");
    if (snap_ok) {
        check_image_unchanged("new-file FAT_ERR_FULL leaves image unchanged");
    }
    rc = fat_open("HUGE    BIN", &f);
    check(rc == FAT_ERR_FOUND, "failed new file has no directory entry");

    snap_ok = snapshot_image(image_before) == 0;
    check(snap_ok, "snapshot before extension FAT_ERR_FULL");
    rc = fat_write_file("BOOT    BIN", oversized, sizeof oversized);
    check(rc == FAT_ERR_FULL, "extension returns FAT_ERR_FULL");
    if (snap_ok) {
        check_image_unchanged("extension FAT_ERR_FULL leaves image unchanged");
    }
    check_file("BOOT    BIN", 1700, boot, "BOOT after FAT_ERR_FULL");

    /* The old file must remain intact when a staged payload write fails
     * after at least one sector has reached the card. */
    fill_payload(replacement, sizeof replacement, 23);
    rc = fat_open("BOOT    BIN", &f);
    check(rc == FAT_OK, "open BOOT before staged payload failure");
    boot_start = rc == FAT_OK ? f.start_cluster : 0;
    fail_writes_after = 3;
    rc = fat_write_file("BOOT    BIN", replacement, sizeof replacement);
    check(rc == FAT_ERR_IO, "staged payload failure returns FAT_ERR_IO");
    check(fail_writes_after == -1, "staged payload failure was injected");
    fail_writes_after = -1;
    check_file("BOOT    BIN", 1700, boot, "BOOT after payload I/O error");
    rc = fat_open("BOOT    BIN", &f);
    check(rc == FAT_OK && f.start_cluster == boot_start,
          "payload failure preserves BOOT cluster chain");

    /* A directory-sector error before any bytes reach the card must also
     * leave the old entry and chain intact. */
    fail_writes_after = 4;
    rc = fat_write_file("BOOT    BIN", replacement, sizeof replacement);
    check(rc == FAT_ERR_IO, "directory write failure returns FAT_ERR_IO");
    check(fail_writes_after == -1, "directory write failure was injected");
    fail_writes_after = -1;
    check_file("BOOT    BIN", 1700, boot, "BOOT after directory I/O error");
    rc = fat_open("BOOT    BIN", &f);
    check(rc == FAT_OK && f.start_cluster == boot_start,
          "directory failure preserves BOOT cluster chain");

    /* A card may persist the directory sector and still report an error.
     * Readback must recognize the committed entry before freeing old data. */
    report_error_after_write = 4;
    rc = fat_write_file("BOOT    BIN", replacement, sizeof replacement);
    check(rc == FAT_OK, "committed directory write error is recovered");
    check(report_error_after_write == -1,
          "committed directory write error was injected");
    check_file("BOOT    BIN", sizeof replacement, replacement,
               "BOOT after committed directory error");
    rc = fat_write_file("BOOT    BIN", boot, 1700);
    check(rc == FAT_OK, "restore BOOT after directory error test");
    check_file("BOOT    BIN", 1700, boot, "BOOT restored after error test");

    /* Cleanup starts after five successful writes: two mirrored FAT writes
     * for the staged cluster, two payload sectors, and the directory sector.
     * Failing after seven writes targets FAT0 of the second old cluster,
     * after the old head has already been released. */
    fail_writes_after = 7;
    rc = fat_write_file("BOOT    BIN", replacement, sizeof replacement);
    check(rc == FAT_OK, "old-chain FAT0 cleanup failure is recovered");
    check(fail_writes_after == -1,
          "old-chain FAT0 cleanup failure was injected");
    fail_writes_after = -1;
    check_file("BOOT    BIN", sizeof replacement, replacement,
               "BOOT after FAT0 cleanup retry");
    rc = fat_write_file("BOOT    BIN", boot, 1700);
    check(rc == FAT_OK, "restore BOOT after FAT0 cleanup retry");

    /* The following write is FAT1 for the same tail cluster. The successful
     * FAT0 update must be rolled back and both mirrors must converge on retry. */
    fail_writes_after = 8;
    rc = fat_write_file("BOOT    BIN", replacement, sizeof replacement);
    check(rc == FAT_OK, "old-chain FAT1 cleanup failure is recovered");
    check(fail_writes_after == -1,
          "old-chain FAT1 cleanup failure was injected");
    fail_writes_after = -1;
    check_file("BOOT    BIN", sizeof replacement, replacement,
               "BOOT after FAT1 cleanup retry");
    rc = fat_write_file("BOOT    BIN", boot, 1700);
    check(rc == FAT_OK, "restore BOOT after FAT1 cleanup retry");

    /* FAT1 may reach the medium and still report an error. fat_set rolls the
     * touched mirrors back, then the cleanup retry releases the cluster. */
    report_error_after_write = 8;
    rc = fat_write_file("BOOT    BIN", replacement, sizeof replacement);
    check(rc == FAT_OK, "committed FAT1 cleanup error is recovered");
    check(report_error_after_write == -1,
          "committed FAT1 cleanup error was injected");
    report_error_after_write = -1;
    check_file("BOOT    BIN", sizeof replacement, replacement,
               "BOOT after committed FAT1 cleanup error");
    rc = fat_write_file("BOOT    BIN", boot, 1700);
    check(rc == FAT_OK, "restore BOOT after committed FAT1 cleanup error");
    check_file("BOOT    BIN", 1700, boot,
               "BOOT restored after cleanup recovery tests");

    /* Consuming an end marker must create a new one in the following slot. */
    rc = seed_stale_after_end(0);
    check(rc == 0, "seed stale same-sector directory entry");
    rc = fat_write_file("MARK    BIN", boot, 0);
    check(rc == FAT_OK, "create file at same-sector end marker");
    rc = fat_open("STALE   BIN", &f);
    check(rc == FAT_ERR_FOUND, "same-sector stale entry stays hidden");

    /* Move the next end marker to the sector boundary. FAIL.BIN is created
     * below after the injected allocation error, exercising that boundary. */
    rc = seed_stale_after_end(1);
    check(rc == 0, "seed stale next-sector directory entry");

    /* Fail the second FAT copy while linking the second newly allocated
     * cluster. The entry update and all earlier allocations must roll back. */
    snap_ok = snapshot_image(image_before) == 0;
    check(snap_ok, "snapshot before injected FAT write failure");
    fail_writes_after = 5;
    rc = fat_write_file("FAIL    BIN", boot, sizeof boot);
    check(rc == FAT_ERR_IO, "mirrored FAT write failure returns FAT_ERR_IO");
    check(fail_writes_after == -1, "mirrored FAT write failure was injected");
    fail_writes_after = -1;
    if (snap_ok) {
        check_image_unchanged("FAT write failure rolls allocation back");
    }
    rc = fat_open("FAIL    BIN", &f);
    check(rc == FAT_ERR_FOUND, "failed allocation has no directory entry");
    check_file("BOOT    BIN", 1700, boot, "BOOT after FAT I/O error");

    rc = fat_write_file("FAIL    BIN", boot, sizeof boot);
    check(rc == FAT_OK, "retry succeeds after FAT I/O error");
    if (rc == FAT_OK) {
        check_file("FAIL    BIN", sizeof boot, boot, "FAIL.BIN retry");
    }
    rc = fat_open("STALE   BIN", &f);
    check(rc == FAT_ERR_FOUND, "next-sector stale entry stays hidden");
    rc = fat_write_file("FAIL    BIN", boot, 0);
    check(rc == FAT_OK, "retry file clusters are released");
    if (rc == FAT_OK) {
        check_file("FAIL    BIN", 0, boot, "FAIL.BIN empty");
    }

    rc = load_layout(&layout);
    check(rc == 0, "load FAT layout for directory boundary tests");
    if (rc == 0) {
        rc = seed_next_cluster_end(&layout);
        check(rc == 0, "seed stale next-cluster directory entry");
        rc = set_fat_entry(&layout, FAT_SCAN_POISON_CLUSTER, 1);
        check(rc == 0, "poison shared FAT scan buffer");
        rc = fat_write_file("CLUSTER BIN", boot, 0);
        check(rc == FAT_OK, "create file at next-cluster end marker");
        rc = set_fat_entry(&layout, FAT_SCAN_POISON_CLUSTER, 0);
        check(rc == 0, "clear shared FAT scan buffer poison");
        rc = fat_open("CLUSTER BIN", &f);
        check(rc == FAT_OK && f.size == 0,
              "next-cluster marker file remains visible");
        rc = fat_open("STALE   BIN", &f);
        check(rc == FAT_ERR_FOUND, "next-cluster stale entry stays hidden");

        rc = seed_chain_tail(&layout);
        check(rc == 0, "seed directory chain-tail boundary");
        rc = fat_write_file("TAIL    BIN", boot, 0);
        check(rc == FAT_OK, "create file at directory chain tail");
        check(check_sentinel(&layout),
              "directory chain tail does not touch next physical sector");
        rc = fat_open("NOPE2   BIN", &f);
        check(rc == FAT_ERR_FOUND, "full directory scan stops at chain EOC");
    }

    check_file("README  TXT", sizeof readme, readme, "README after write");
    check_file("HELLO   TXT", sizeof hello - 1,
               (const unsigned char *) hello, "HELLO after write");
    check_file("DATA    BIN", sizeof data, data, "DATA after write");

    fclose(img);
}

int main(int argc, char **argv)
{
    int i;

    if (argc < 2) {
        printf("usage: %s <image>...\n", argv[0]);
        return 2;
    }
    for (i = 1; i < argc; i++) {
        run(argv[i]);
    }
    if (fails != 0) {
        printf("FAIL: %d 件\n", fails);
        return 1;
    }
    printf("OK: FAT32 読み書きはすべての確認を通過\n");
    return 0;
}
