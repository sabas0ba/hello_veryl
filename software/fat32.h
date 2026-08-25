/* FAT32 の読み出し (docs/riscv.md「TF カードからのブート」)
 *
 * セクタの読み出しだけを外へ出し (fat_dev_read)，MBR/BPB の解釈・
 * ルートディレクトリの探索・クラスタチェーンの追跡はここで行う．
 * ハードウェアの Fat32Reader (src/tfcard/fat32_reader.veryl) と同じ判定規則
 * にそろえてあるが，こちらは RV32IM のソフトウェアとして動く．
 *
 * 読み出し専用．LFN は解釈せず 8.3 名だけを見る．
 */
#ifndef FAT32_H
#define FAT32_H

enum {
    FAT_OK        = 0,
    FAT_ERR_IO    = 1, /* セクタ入出力に失敗した */
    FAT_ERR_NO_FS = 2, /* MBR/BPB が FAT32 として妥当でない */
    FAT_ERR_FOUND = 3, /* 8.3 名が一致するエントリがない */
    FAT_ERR_CHAIN = 4, /* クラスタチェーンが壊れている */
    FAT_ERR_FULL  = 5, /* 空きクラスタがない */
    FAT_ERR_DIR   = 6  /* ルートディレクトリに空きエントリがない */
};

typedef struct {
    unsigned int start_cluster;
    unsigned int size; /* バイト数 */
} fat_file;

/* 1 セクタ (512 byte) を buf へ読む．0 = 成功．環境ごとに実装する
 * (実機は software/tfdev.c，ホストは verif/riscv/fat32_host.c) */
int fat_dev_read(unsigned int lba, unsigned char *buf);

/* 1 セクタ (512 byte) を buf から書く．0 = 成功．同上 */
int fat_dev_write(unsigned int lba, const unsigned char *buf);

/* MBR/BPB を解釈してレイアウトを決める */
int fat_mount(void);

/* ルートディレクトリから 8.3 名 (11 文字，空白詰め) を探す */
int fat_open(const char *name11, fat_file *f);

/* ファイルの先頭から max_len バイトまでを dst へ読む．
 * 実際に読めたバイト数を *out_len へ返す */
int fat_read(const fat_file *f, void *dst, unsigned int max_len, unsigned int *out_len);

/* ルートディレクトリの 8.3 名へ len バイトを書く．
 * 既存なら上書きし (クラスタチェーンを伸縮する)，無ければ作る．
 *
 * 制限 (docs/riscv.md「TF カードからのブート」):
 *   - ルートディレクトリは既存のクラスタ内にしか作らない (伸長しない)
 *   - LFN は作らない．属性は archive (0x20)，日時は 0
 *   - 空き容量情報 (FSInfo) は更新しない */
int fat_write_file(const char *name11, const void *src, unsigned int len);

#endif
