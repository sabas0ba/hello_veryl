/* 回転するトーラスの ASCII レンダリング (docs/riscv.md「LCD デモ」)
 *
 * UART へ 1 フレームぶんの文字を送る．TopRv は UART TX を傍受して
 * TextConsole へ流すため，LCD にそのまま表示される．
 *
 * コアは RV32IM で FPU を持たないため，すべて整数の固定小数点 (Q10) で扱う．
 * 三角関数は起動時に 256 分割のテーブルを作って引く．
 *
 * 描画は陰面消去つきの点群描画とする．トーラス面上の格子点を
 * 順に投影し，同じ文字セルに来たものは視点に近いほうを残す．
 * 明るさは面法線と光源方向の内積から決め，濃度順に並べた文字へ写す．
 */

#define MMIO_UART 0x20000010 /* W: 送信バイト / R: bit0 = 送信可 */

/* 固定小数点のスケール */
#define QS 10
#define Q  (1 << QS)

/* 画面 (TextConsole の桁数と一致させる．行を埋めると自動折り返しで改行される) */
#define W 100
#define H 29

/* 形状と視点 */
#define R1 (1 * Q) /* 管の半径 */
#define R2 (2 * Q) /* 輪の半径 */
#define K2 (6 * Q) /* 視点から輪の中心までの距離 */
#define KX 44      /* 水平方向の投影倍率 [文字] */
#define KY 22      /* 垂直方向．文字セルが縦長 (8x16) なので水平の半分 */

/* 面上の格子の粗さ (256 を 1 周とする)．粗いと面に隙間が空き，
 * 細かいと 1 フレームの描画時間が延びる (ホストで verif/riscv/render_check.sh
 * にかけて詰めた値)． */
#ifndef TSTEP
#define TSTEP 4 /* 管まわり: 64 点 */
#endif
#ifndef PSTEP
#define PSTEP 1 /* 輪まわり: 256 点 */
#endif

/* 光源方向 (0, 1, -1)/sqrt(2) の Q10 表現 */
#define LY 724
#define LZ (-724)

static short      sintab[256];
static char       fb[W * H];
static const char shade[] = ".,-~:;=!*#$@";

/* z バッファ (5.8 KB) だけはオンチップ RAM (8 KB) に入らないので PSRAM へ置く．
 * 1 点あたりの参照は 1〜2 回で，命令フェッチ (1 点あたり数十回) に比べれば
 * 少ないため，ここが PSRAM でも描画時間への影響は小さい． */
#ifdef HOST
static short zbuf[W * H];
#define ZB zbuf
#else
#define ZB ((short *) 0x10010000)
#endif

/* sin テーブルを作る．第 1 象限を Taylor 展開で求め，残りは対称性で埋める．
 * sin(x) = x(1 - x^2/6(1 - x^2/20(1 - x^2/42))) を Q12 で評価する． */
static void build_sintab(void)
{
    short q[65];
    int   j;

    for (j = 0; j <= 64; j++) {
        int x  = (j * 6434) >> 6; /* x = j * (pi/2)/64  [Q12], pi/2 = 6434 */
        int x2 = (x * x) >> 12;
        int t;
        t    = 4096 - x2 / 42;
        t    = 4096 - ((x2 * t) >> 12) / 20;
        t    = 4096 - ((x2 * t) >> 12) / 6;
        q[j] = (short) (((x * t) >> 12) >> 2); /* Q12 -> Q10 */
    }
    for (j = 0; j < 64; j++) {
        sintab[j]       = q[j];
        sintab[64 + j]  = q[64 - j];
        sintab[128 + j] = (short) -q[j];
        sintab[192 + j] = (short) -q[64 - j];
    }
}

#define SIN(i) ((int) sintab[(i) & 255])
#define COS(i) ((int) sintab[((i) + 64) & 255])

/* HOST を定義するとホスト上で同じ描画を実行できる (verif/riscv/render_check.sh)．
 * 実機へ持ち込む前に絵柄と塗り分けを確認するための足場． */
#ifdef HOST
#include <stdio.h>
static void put_char(int c)
{
    putchar(c);
}
#else
static void put_char(int c)
{
    volatile unsigned int *u = (volatile unsigned int *) MMIO_UART;
    while ((*u & 1u) == 0u) {
    }
    *u = (unsigned int) c;
}
#endif

static void render(int ia, int ib)
{
    int ca = COS(ia), sa = SIN(ia);
    int cb = COS(ib), sb = SIN(ib);
    int i, t, p;

    for (i = 0; i < W * H; i++) {
        fb[i] = ' ';
        ZB[i] = 0x7fff;
    }

    for (t = 0; t < 256; t += TSTEP) {
        int ct = COS(t), st = SIN(t);
        int cx = R2 + ((R1 * ct) >> QS); /* 管の断面円上の点 */
        int cy = (R1 * st) >> QS;

        for (p = 0; p < 256; p += PSTEP) {
            int cp = COS(p), sp = SIN(p);
            int x0, y0, z0, nx0, ny0, nz0;
            int y1, z1, ny1, nz1;
            int x2, z2, nz2, zv;
            int sx, sy, idx, lum, si;

            /* y 軸まわりに revolve してトーラス面上の点と法線を得る */
            x0  = (cx * cp) >> QS;
            y0  = cy;
            z0  = -((cx * sp) >> QS);
            nx0 = (ct * cp) >> QS;
            ny0 = st;
            nz0 = -((ct * sp) >> QS);

            /* x 軸まわりに角 A */
            y1  = ((y0 * ca) >> QS) - ((z0 * sa) >> QS);
            z1  = ((y0 * sa) >> QS) + ((z0 * ca) >> QS);
            ny1 = ((ny0 * ca) >> QS) - ((nz0 * sa) >> QS);
            nz1 = ((ny0 * sa) >> QS) + ((nz0 * ca) >> QS);

            /* y 軸まわりに角 B (y 成分は変わらない) */
            x2  = ((x0 * cb) >> QS) + ((z1 * sb) >> QS);
            z2  = -((x0 * sb) >> QS) + ((z1 * cb) >> QS);
            nz2 = -((nx0 * sb) >> QS) + ((nz1 * cb) >> QS);

            /* 光源に背を向けた面はここで捨てる．投影の除算と z バッファ
             * (PSRAM) の読み出しを省けるので，先に判定する． */
            lum = ((ny1 * LY) >> QS) + ((nz2 * LZ) >> QS);
            if (lum <= 0) {
                continue;
            }
            zv = K2 + z2; /* 視点からの距離．K2 > R1+R2 なので常に正 */
            sx = W / 2 + (KX * x2) / zv;
            sy = H / 2 - (KY * y1) / zv;
            if (sx < 0 || sx >= W || sy < 0 || sy >= H) {
                continue;
            }
            idx = sy * W + sx;
            if (zv >= ZB[idx]) { /* 手前にあるものだけ残す */
                continue;
            }
            si = (lum * 12) >> QS;
            if (si > 11) {
                si = 11;
            }
            ZB[idx] = (short) zv;
            fb[idx] = shade[si];
        }
    }
}

static void show(void)
{
    int i;

#ifdef HOST
    for (i = 0; i < W * H; i++) {
        put_char(fb[i]);
        if (i % W == W - 1) {
            put_char('\n');
        }
    }
#else
    /* 桁数ぶんを隙間なく送るので，折り返しで改行される．
     * FF でカーソルだけ左上へ戻し，前フレームの上から書き直す． */
    put_char(0x0c);
    for (i = 0; i < W * H; i++) {
        put_char(fb[i]);
    }
#endif
}

#ifndef HOST
/* 起動時に一度だけ画面全体を空白で埋める．描画は 29 行しか書かないため，
 * 最下行にモニタの出力が残ったままになるのを防ぐ．
 * TextConsole の行数 (30) ぶんを埋める． */
#define CONSOLE_ROWS 30
static void clear_screen(void)
{
    int i;

    put_char(0x0c);
    for (i = 0; i < W * CONSOLE_ROWS; i++) {
        put_char(' ');
    }
}
#endif

int main(void)
{
    int ia = 0, ib = 0;

    build_sintab();
#ifndef HOST
    clear_screen();
#endif
#ifdef HOST
    {
        int n;
        for (n = 0; n < HOST_FRAMES; n++) {
            render(ia, ib);
            show();
            ia = (ia + 4) & 255;
            ib = (ib + 7) & 255;
        }
    }
    return 0;
#else
    for (;;) {
        render(ia, ib);
        show();
        ia = (ia + 4) & 255;
        ib = (ib + 7) & 255;
    }
#endif
}
