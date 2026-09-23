/* ovl_spin.c — 撃沈の直後、**いま出ている画面そのもの**を粗くして、きりもみしながら遠ざける。
   『A-JAX』の「戦艦撃破 → きりもみ急上昇」の絵。撃沈オーバレイ(OVL7)に同居する。

   ★仕掛け: V9938/V9958 に背景の拡大レジスタは無い(R#1 の MAG はスプライト専用)。
     だが SCREEN3(マルチカラー)は 1 ブロック = 4x4 ドットなので、モードを選ぶこと自体が
     「背景を 4 倍に拡大した状態」になる。全画面を毎フレーム書き換えてもパターン(色)テーブルの
     1,536B で済む(SCREEN5 の全画面 27,136B = 4 フレーム分に対して 1/18)。
     画面の 4x4 ドットを 1 テクセルとして吸い出せば、切替の瞬間に変わるのは「粗さ」だけになる。

   ★置き場: 元絵 64x64 と内側ループは hot_ram を借りる(演出中はゲームの RAM 実行コードを使わない)。
     終わったら常駐側が hot_load() で戻す。番地は Makefile が範囲検査する。
   ★内側ループは RAM 実行。ROM 実行だと 7fps しか出ない(turboR 実測 532 jiffy/64frame)。

   ★倍率の上限は 1 ブロック 3 テクセル。座標が 8.8 の 16bit なので、画面端で ±256 テクセルを
     超えると折り返して元絵が何枚も出る(試作で踏んだ)。そこまで小さくしたら白で飛ばす。 */
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "hotcode.h"
#include "overlay.h"

#define TEX_ADDR  0xC600     /* hot_ram の中の 256 境界(Makefile が hot_ram の範囲を検査する) */
#define TEX       ((u8 *)TEX_ADDR)
#define RAMF      ((u8 *)0xD600)   /* 内側ループの RAM 実行先(TEX の直後) */

#define S3_PAT   0x0000      /* SCREEN3 パターン(色)テーブル: 1,536B */
#define S3_NAME  0x0800      /* 名前テーブル: 768B */
#define S3_SATR  0x1B00
#define S3_SPAT  0x3800

#define SCL_MAX  48          /* 1 ブロック 3 テクセル。これ以上は折り返して像が並ぶ */
#define SEQ_END  52          /* 約 2.6 秒(RAM 実行 18fps 前提) */

static u8  ang;
static u16 scl;
static u16 seq_t;
/* 内側ループが読む値(asm から直接参照する) */
static s16 sA, sC, sBA, sDC;
static u16 u0, v0;
static u8  cnt, nib;

static const s8 cos64[64] = {
     64, 63, 62, 60, 57, 53, 49, 45, 39, 34, 28, 21, 15,  8,  2, -4,
    -11, -17, -24, -30, -36, -41, -46, -51, -55, -58, -61, -63, -64, -64, -64, -63,
    -62, -60, -57, -53, -49, -45, -39, -34, -28, -21, -15, -8, -2,  4, 11, 17,
     24, 30, 36, 41, 46, 51, 55, 58, 61, 63, 64, 64, 64, 63, 62, 60,
};
static s8 cs(u8 a) { return cos64[a & 63]; }
static s8 sn(u8 a) { return cos64[(u8)(a + 48) & 63]; }

/* ───────── いま出ている SCREEN5 の画面を 64x48 テクセルへ(4x4 ドットを 1 つに) ─────────
   ★縦スクロール(R#23=g_vscroll)ぶんずらして読む。リングは 256 行で回っている。
   ★上下 8 行ぶんは海の斑で埋めて 64x64 にする(回したとき角が欠けないように)。 */
static void grab(void) {
    u8 ty, tx;
    for (ty = 0; ty < 64; ty++) {
        u8 *d = &TEX[(u16)ty << 6];
        if (ty < 8 || ty >= 56) {
            for (tx = 0; tx < 64; tx++) d[tx] = (u8)(((tx ^ ty) & 3) ? 1 : 2);
            continue;
        }
        {
            u8 row = (u8)(g_vscroll + (u8)((ty - 8) << 2));   /* 画面の 4 行ごと */
            vdp_read_addr((u16)((u16)row << 7));              /* SCREEN5: 1 行 128B */
            for (tx = 0; tx < 64; tx++) {
                u8 b = vdp_read_data();                        /* 偶数ドット = 上位ニブル */
                (void)vdp_read_data();                         /* 4 ドットおき＝1 バイト飛ばす */
                d[tx] = (u8)(b >> 4);
            }
        }
    }
}

/* ───────── 1 帯(縦8ブロック=8バイト)を作って VDP へ直接流す ─────────
   ★テクセル番地(TEX は 256 境界): 上位 = >TEX + ((v>>8)&63)>>2 / 下位 = (((v>>8)&3)<<6) + ((u>>8)&63)
   ★元絵の外(整数部に bit6/7)は海の斑にする。無いと遠ざかったときに元絵が何枚も並ぶ。
   ★相対ジャンプだけで書く(そのまま hot_ram へ写して RAM 実行するため)。折り返しは中継を経由。
   ★OTIR は使わない(R800 では VDP に速すぎる＝既知の地雷)。 */
static void strip(void) __naked {
    __asm
        ld   hl, (_u0)
        ld   de, (_v0)
        ld   a, #8
        ld   (_cnt), a
    sp_loop:
        ld   a, d
        or   h
        and  #0xC0
        jr   nz, sp_sea1
        ld   a, d
        and  #63
        ld   c, a
        srl  a
        srl  a
        add  a, #0xC6
        ld   b, a
        ld   a, c
        and  #3
        rrca
        rrca
        ld   c, a
        ld   a, h
        and  #63
        add  a, c
        ld   c, a
        ld   a, (bc)
        jr   sp_got1
    sp_sea1:
        ld   a, h
        xor  d
        and  #3
        jr   z, sp_sea1b
        ld   a, #1
        jr   sp_got1
    sp_sea1b:
        ld   a, #2
    sp_got1:
        rlca
        rlca
        rlca
        rlca
        and  #0xF0
        ld   (_nib), a
        ld   bc, (_sA)
        add  hl, bc
        ex   de, hl
        ld   bc, (_sC)
        add  hl, bc
        ex   de, hl
        jr   sp_over
    sp_tramp:
        jr   sp_loop
    sp_over:
        ld   a, d
        or   h
        and  #0xC0
        jr   nz, sp_sea2
        ld   a, d
        and  #63
        ld   c, a
        srl  a
        srl  a
        add  a, #0xC6
        ld   b, a
        ld   a, c
        and  #3
        rrca
        rrca
        ld   c, a
        ld   a, h
        and  #63
        add  a, c
        ld   c, a
        ld   a, (bc)
        jr   sp_got2
    sp_sea2:
        ld   a, h
        xor  d
        and  #3
        jr   z, sp_sea2b
        ld   a, #1
        jr   sp_got2
    sp_sea2b:
        ld   a, #2
    sp_got2:
        and  #0x0F
        ld   b, a
        ld   a, (_nib)
        or   b
        out  (0x98), a
        ld   bc, (_sBA)
        add  hl, bc
        ex   de, hl
        ld   bc, (_sDC)
        add  hl, bc
        ex   de, hl
        ld   a, (_cnt)
        dec  a
        ld   (_cnt), a
        jr   nz, sp_tramp
        ret
    __endasm;
}
/* strip() の終わり(写す長さを測るための目印)。★この関数は strip の直後に置くこと。 */
static void strip_end(void) __naked { __asm ret __endasm; }

static void (*stripr)(void);   /* hot_ram へ写した strip */

/* 1 画面ぶん(1,536B)。パターン表の並び＝帯 g(8行) → セル列 cx → 帯の中の行。
   ★掛け算は帯の頭だけ(SDCC の 16bit 乗算を内側に置くと 1 フレームの半分を食う)。 */
static void frame(void) {
    u8 g, cx;
    s16 A = (s16)(((s16)cs(ang) * (s16)scl) >> 2);
    s16 C = (s16)(((s16)sn(ang) * (s16)scl) >> 2);
    s16 B = (s16)(-C), D = A;
    s16 U0, V0;
    sA = A; sC = C;
    sBA = (s16)(-C - A); sDC = (s16)(A - C);
    U0 = (s16)(0x2000 - (s16)(32 * A) - (s16)(24 * B));   /* 画面中央が元絵の中央に来る＝中心で回る */
    V0 = (s16)(0x2000 - (s16)(32 * C) - (s16)(24 * D));
    vdp_write_addr(S3_PAT);
    for (g = 0; g < 6; g++) {
        u16 gu = (u16)(U0 + (s16)(g << 3) * B);
        u16 gv = (u16)(V0 + (s16)(g << 3) * D);
        for (cx = 0; cx < 32; cx++) {
            u0 = gu; v0 = gv;
            stripr();
            gu = (u16)(gu + A + A);
            gv = (u16)(gv + C + C);
        }
    }
}

/* SCREEN3 へ。テーブルは自分で設定する(BIOS 既定に依存しない)。 */
static void enter_s3(void) {
    u8 y, x, i;
    raster_off();
    __asm
        ld   a, #3
        ld   (0xFCAF), a       ; SCRMOD_W = 3 (MULTI COLOUR)
        call 0x005F            ; CHGMOD
    __endasm;
    vdp_wreg(25, 0x00);        /* ★R#25 は BIOS が面倒を見ない */
    vdp_wreg(2, S3_NAME / 0x400);
    vdp_wreg(4, S3_PAT / 0x800);
    vdp_wreg(5, S3_SATR / 0x80);
    vdp_wreg(6, S3_SPAT / 0x800);
    vdp_palette_game();
    vdp_write_addr(S3_NAME);   /* name = 32*(y/4) + x */
    for (y = 0; y < 24; y++)
        for (x = 0; x < 32; x++) vdp_data((u8)(((y >> 2) << 5) + x));
    vdp_write_addr(S3_SPAT);
    for (i = 0; i < 8; i++) vdp_data(0x00);
    vdp_write_addr(S3_SATR);
    vdp_data(208);             /* スプライトは出さない(モード1は1ライン4枚・単色) */
    /* ★R#1 = 0x68: 画面ON / VBLANK割込みON / **bit3 = M2 = 1(MULTI COLOUR)** / スプライト8x8。
       ここで M2 を落とすと GRAPHIC1 になり、パターン表が正しくても画面が一様になる(試作で踏んだ)。 */
    vdp_wreg(1, 0x68);
}

/* 撃沈の直後に常駐から呼ばれる(OVL_SLOT_SPIN)。戻るまで数秒ここに居る。 */
void ovl_spin(void) {
    u16 k;
    const u8 *s = (const u8 *)strip;
    u16 len = (u16)((const u8 *)strip_end - s);
    grab();                                   /* いまの画面を吸い出してから */
    if (len > 512) return;                    /* 枠に入らない＝やらない(安全側) */
    for (k = 0; k < len; k++) RAMF[k] = s[k]; /* 内側ループを RAM へ(ROM 実行では 7fps) */
    stripr = (void (*)(void))RAMF;
    ang = 0; scl = 16; seq_t = 0;
    enter_s3();
    while (seq_t <= SEQ_END) {
        u16 t = seq_t++;
        ang = (u8)(ang + 1 + (u8)(t >> 3));   /* だんだん速く回る */
        if (t >= 8) {
            u16 d = (u16)(t - 8);
            u16 v = (u16)(16 + (u16)((d * d) >> 3));
            scl = (v > SCL_MAX) ? SCL_MAX : v;
        }
        frame();
        vdp_wait_frame();
    }
    for (k = 0; k < 16; k++) vdp_set_pal((u8)k, 7, 7, 7);   /* 白で飛ばして終わる */
    vdp_wait_frame();
    vdp_wait_frame();
}
