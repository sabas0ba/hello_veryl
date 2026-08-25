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

static FILE *img;
static int   fail_writes_after = -1;
static unsigned char image_before[IMAGE_BYTES];
static unsigned char image_after[IMAGE_BYTES];

int fat_dev_read(unsigned int lba, unsigned char *buf)
{
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
    if (fail_writes_after == 0) {
        fail_writes_after = -1;
        return 1;
    }
    if (fail_writes_after > 0) {
        fail_writes_after--;
    }
    if (fseek(img, (long) lba * SECTOR, SEEK_SET) != 0) {
        return 1;
    }
    if (fwrite(buf, 1, SECTOR, img) != SECTOR) {
        return 1;
    }
    return 0;
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

static void check_image_unchanged(const char *label)
{
    int rc = snapshot_image(image_after);

    check(rc == 0, "snapshot after failed write");
    if (rc == 0) {
        check(memcmp(image_before, image_after, IMAGE_BYTES) == 0, label);
    }
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

static void run(const char *path)
{
    static unsigned char readme[2348];
    static unsigned char data[256];
    static unsigned char boot[2500];
    static unsigned char oversized[1024u * 1024u];
    const char           hello[] = "Hello, Veryl TF card!\r\n";
    fat_file             f;
    unsigned int         i;
    int                  rc;
    int                  snap_ok;

    img = fopen(path, "rb+");
    if (img == NULL) {
        printf("ERROR: %s を開けない\n", path);
        fails++;
        return;
    }
    printf("---- %s ----\n", path);
    fail_writes_after = -1;

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
    rc = fat_write_file("BOOT    BIN", boot, 2500);
    check(rc == FAT_OK, "BOOT.BIN を新規作成");
    if (rc == FAT_OK) {
        check_file("BOOT    BIN", 2500, boot, "BOOT.BIN new");
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
    rc = fat_write_file("FAIL    BIN", boot, 0);
    check(rc == FAT_OK, "retry file clusters are released");
    if (rc == FAT_OK) {
        check_file("FAIL    BIN", 0, boot, "FAIL.BIN empty");
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
