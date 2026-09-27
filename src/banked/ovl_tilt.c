/* ovl_tilt.c — 5面(米戦艦 IOWA・夜戦)の轟沈: **見下ろしの画面が奥へ倒れ**、地平線が現れ、
   燃えている艦が遠ざかって夜の海へ沈む。いわゆる Mode 7 風のパース。

   ★狙い(設計メモ §2 の「これ MSX!?」枠。未着手だった C の延長):
     2D の見下ろしシューティングが、撃破の瞬間だけ**3D に見える**。turboR の CPU で
     行ごとの遠近を計算し、出力は SCREEN3 の 1,536B/フレームだけ＝VDP 帯域は軽い。

   ★元絵は 3面のきりもみと同じ土台(spinfx.h)。撃破の瞬間の**実画面**を 4 ドット 1 テクセルで
     吸い出し、画面外の艦首・艦尾はバッファ B で足す。だから燃えている艦がそのまま倒れる。

   ★仕掛け: SCREEN3 のパターン表は「帯 g(8行) → セル列 cx → 帯の中の行 b」の順で並ぶので、
     VRAM へ順番に出すにはこの順で書くしかない。回転(ovl_spin)は 1 本の直線で足りたが、
     パースは**行ごとに倍率が違う**ので、帯の 8 行ぶんのパラメータを表に置き、
     セル列を回しながら 8 行を代わる代わる出す(内側ループ tl_band)。
       行 y の奥行き z = ZK / (y - 地平線)   … 手前(下)ほど z が小さい＝拡大
       1 ブロックあたりのテクセル数 A = z / F  … 奥ほど大きい＝詰まって見える
       元絵の行 V = vcam + (z(中央) - z(y))   … 下へ行くほど手前(艦尾側)
     倒れ方は「真上から(等倍)」と「パース」を p で混ぜる。p=0 で撃破の画面そのもの。

   ★地平線より上は夜空。行の絵を「空の 16 バイト」に差し替えるだけで済ませる。 */
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "overlay.h"
#include "scroll.h"
#include "spinfx.h"
#include "aa_hot.h"        /* cam(縦スクロールカメラ=世界Y) */

#define SEQ_HOLD  8        /* 最初は倒さない(撃破の画面が粗くなっただけ) */
#define SEQ_TILT  40       /* 倒れきるまで */
#define SEQ_END   96       /* 全体の尺 */
#define RELIEF_N  5        /* 上部構造を持ち上げる段数(1 段 = 元絵 1 行 = 4 ドット) */

/* ★奥行きの定数は **8.8 で持つ**(ZK<<8 と書くと u16 を溢れる)。z = ZK8 / d。 */
#define ZK8     61440u     /* = 240 テクセル・ブロック */
#define FOCAL_S 4          /* A = z / 16 → 右シフト 4 */
#define HOR_MAX 14         /* 倒れきったときの地平線(ブロック行) */
#define DBASE   2          /* 地平線のすぐ下の行の奥行き(0 除算よけ) */

/* 1 行ぶんのパラメータ。内側ループ(asm)が IX で引く。
     +0 uacc  いま見ているテクセル位置(8.8)。セル列が進むたびに astep×2 足す
     +2 astep 1 ブロックあたりのテクセル数(8.8)
     +4 rowp  元絵の行の先頭(0 = 絵の外＝海か空)
     +6 seap  海(または空)の 16 バイトの行 */
static u8  rowtab[8 * 8];
static u8  sky[16];        /* 夜空の 1 行(たまに星) */
static u16 vcam8;          /* 画面中央に置く元絵の行(8.8) */

/* 元絵の外を指す行は rowp=0 にして、海(または空)の 16 バイトから引く。 */

/* ───────── 内側ループ: 帯 1 本(セル列 32 × 帯の中の行 8 = 256 バイト)を VRAM へ ─────────
   ★VRAM のアドレスは呼ぶ前に vdp_write_addr で合わせてある(帯の先頭)。以降は自動で +1。 */
static void tl_band(void) __naked {
    __asm
        push ix
        ld   c, #32                ; セル列
    tlb_col:
        ld   ix, #_rowtab
        ld   b, #8                 ; 帯の中の行
    tlb_row:
        call tlb_texel             ; 左のブロック
        rlca
        rlca
        rlca
        rlca
        and  #0xF0
        ld   e, a
        push de
        call tlb_texel             ; 右のブロック
        pop  de
        and  #0x0F
        or   e
        out  (0x98), a
        ld   de, #8                ; 次の行の表へ
        add  ix, de
        djnz tlb_row
        dec  c
        jr   nz, tlb_col
        pop  ix
        ret

        ;; --- いまの uacc のテクセルを取り、uacc を 1 ブロック進める。戻り A = 色(下位 4bit) ---
    tlb_texel:
        ld   l, 0(ix)
        ld   h, 1(ix)              ; HL = uacc(8.8) → H = テクセル列
        push hl
        ld   e, 2(ix)
        ld   d, 3(ix)
        add  hl, de                ; uacc += astep
        ld   0(ix), l
        ld   1(ix), h
        pop  hl
        ld   a, h
        cp   #64                   ; 0..63 の外(負も含む)は海/空
        jr   nc, tlb_out
        ld   e, 4(ix)
        ld   d, 5(ix)
        ld   a, d
        or   e
        jr   z, tlb_out            ; rowp=0 = 絵の外の行
        ld   a, h
        srl  a
        add  a, e
        ld   e, a
        jr   nc, tlb_ne
        inc  d
    tlb_ne:
        ld   a, (de)
        bit  0, h                  ; テクセルが偶数なら上位ニブル
        jr   nz, tlb_lo
        rrca
        rrca
        rrca
        rrca
    tlb_lo:
        and  #0x0F
        ret
    tlb_out:
        ld   e, 6(ix)
        ld   d, 7(ix)
        ld   a, h
        and  #15
        add  a, e
        ld   e, a
        jr   nc, tlb_se
        inc  d
    tlb_se:
        ld   a, (de)
        and  #0x0F
        ret
    __endasm;
}

/* ───────── 上部構造に高さを出す ─────────
   ★見下ろしの絵をそのまま寝かせると、艦橋も砲塔も甲板に描いた模様にしか見えない(ユーザー指摘)。
     パースにした以上、高さのあるものは手前側へせり上がって見えないと嘘になる。
   ★やり方: 元絵を「1 行うしろ(v+1)が上部構造なら、その色で上に塗り重ねる」パスで持ち上げる。
     これを数回かけると、上部構造が奥(艦首側=画面の上)へ伸びて**壁**のように見える＝高さになる。
     見えている面の向きは正しい(高いものは、その向こう側の甲板を隠す)。
   ★内側ループは 1 テクセル 1 回の取り出しのままなので、**実行時のコストはゼロ**。持ち上げは
     倒れ始めの数フレームで 1 パスずつ行う(1 パス約 4,000 バイトの塗り替え)。
   ★高さを持つ色 = 上部構造の灰・ドームの淡灰と白(4/5/14/15)。甲板(6/9)・海(1/2/7)・
     炎(11/12)・落ち影(13)は平ら。面が変わっても色番号の意味は同じなので、この表でよい。 */
static const u8 tallc[16] = { 0,0,0,0, 1,1,0,0, 0,0,0,0, 0,0,1,1 };

static void relief_pass(void) {
    u8 v, i;
    for (v = 0; v < TEX_H; v++) {
        u8 *d = &TEX[(u16)v << 5];
        const u8 *s = &TEX[(u16)(v + 1) << 5];      /* 1 行うしろ(手前側) */
        for (i = 0; i < 32; i++) {
            u8 sv = s[i], dv = d[i];
            u8 hi = (u8)(sv >> 4), lo = (u8)(sv & 15);
            u8 nh = tallc[hi] ? hi : (u8)(dv >> 4);
            u8 nl = tallc[lo] ? lo : (u8)(dv & 15);
            d[i] = (u8)((nh << 4) | nl);
        }
    }
}

/* 行 y(0..47)のパラメータを表の b 番目へ。p=倒れ具合(0..256)。 */
static void row_setup(u8 b, u8 y, u16 p, u16 zc8) {
    u8 *e = &rowtab[(u16)b << 3];
    s16 hor = (s16)((s16)(HOR_MAX * (s16)p) >> 8);       /* 地平線(ブロック行) */
    s16 d   = (s16)((s16)y - hor + DBASE);
    u16 a8, v8;
    if (d < 2) {                                          /* 地平線より上 = 夜空 */
        /* ★1 ブロック = 1 バイト進める(16 ブロック周期で星が並ぶ)。行ごとに位相をずらして
           星が縦に揃わないようにする。歩幅を小さくすると同じ色が延々と続き、
           星が「白い大きな四角」になる(実際に踏んだ)。 */
        u16 u = (u16)((u16)((y * 5) & 15) << 8);
        e[0] = (u8)(u & 0xFF); e[1] = (u8)(u >> 8);
        e[2] = 0; e[3] = 1;                               /* astep = 1.0 テクセル/ブロック */
        e[4] = 0; e[5] = 0;
        e[6] = (u8)((u16)sky & 0xFF); e[7] = (u8)((u16)sky >> 8);
        return;
    }
    {
        u16 z8 = (u16)(ZK8 / (u16)d);                     /* 奥行き(8.8) */
        u16 ap = (u16)(z8 >> FOCAL_S);                    /* パースのときの倍率 */
        s16 vp = (s16)((s16)vcam8 + (s16)zc8 - (s16)z8);  /* パースのときの元絵の行 */
        u16 ao = 256;                                     /* 真上から見たときは 1 テクセル/ブロック */
        s16 vo = (s16)((s16)vcam8 + (((s16)y - 24) << 8));
        /* ★p は 0..256。差が大きいので 32bit 乗算を避け、先に 1/16 にしてから掛ける
           (SDCC z80 の long 乗算は内側で回すには重い)。 */
        a8 = (u16)((s16)ao + (s16)((((s16)ap - (s16)ao) >> 4) * (s16)(p >> 4)));
        v8 = (u16)((s16)vo + (s16)(((vp - vo) >> 4) * (s16)(p >> 4)));
    }
    {
        u8  vt = (u8)(v8 >> 8);                           /* 元絵の行(テクセル) */
        u16 u0 = (u16)(0x2000 - (u16)((u16)a8 << 5));     /* 中央(テクセル32)を画面の真ん中に */
        e[0] = (u8)(u0 & 0xFF);  e[1] = (u8)(u0 >> 8);
        e[2] = (u8)(a8 & 0xFF);  e[3] = (u8)(a8 >> 8);
        if ((s16)v8 < 0 || vt >= 128) {                   /* 元絵の外 = 海 */
            e[4] = 0; e[5] = 0;
        } else {
            u16 rp = (u16)(TEX_ADDR + ((u16)vt << 5));
            e[4] = (u8)(rp & 0xFF); e[5] = (u8)(rp >> 8);
        }
        { u16 sp = (u16)(SEA_ADDR + ((u16)(vt & 15) << 4));
          e[6] = (u8)(sp & 0xFF); e[7] = (u8)(sp >> 8); }
    }
}

/* 1 画面(6 帯 × 256B = 1,536B)。 */
static void frame(u16 p) {
    u8 g, b;
    u16 zc8 = (u16)(ZK8 / (u16)(24 - (u16)((HOR_MAX * p) >> 8) + DBASE));
    vdp_write_addr(S3_PAT);
    for (g = 0; g < 6; g++) {
        for (b = 0; b < 8; b++) row_setup(b, (u8)((g << 3) + b), p, zc8);
        tl_band();
    }
}

/* 撃破演出の最後に常駐から呼ばれる(OVL_SLOT_TILT)。戻るまで数秒ここに居る。 */
void ovl_tilt(void) {
    u16 t;
    u8  i;
    spinfx_grab();                     /* 撃破の画面(＋画面外の艦)を元絵にする */
    /* 夜空: ほぼ黒に星を 2 つ。海の行と同じ 16 バイトの形にしておく。 */
    for (i = 0; i < 16; i++) sky[i] = 13;
    sky[5] = 14;                       /* 星は 1 テクセルだけ(行ごとに位置がずれる) */
    /* 画面中央に置く元絵の行(3面のきりもみと同じ勘定)。 */
    vcam8 = (u16)((u16)(((cam + 74) >> 2) & 127) << 8);
    raster_off();
    spinfx_enter_s3();
    for (t = 0; t < SEQ_END; t++) {
        u16 p;
        if (t < SEQ_HOLD) p = 0;                                   /* 撃破の画面のまま止める */
        else if (t < SEQ_HOLD + SEQ_TILT) {
            /* ★倒れ始めの数フレームで上部構造をせり上げる(1 フレーム 1 段)。
               静止している間にやると「止まった画面」が崩れるので、倒れ出してから。 */
            if (t < SEQ_HOLD + RELIEF_N) relief_pass();
            u16 d = (u16)(t - SEQ_HOLD);
            /* ★じわっと倒れて後半で一気に(d^2)。32bit 除算を使うと SDCC が __divulong を
               引き込み、それが _HOME(0xEE00 の後ろ)へ置かれてオーバレイの枠から出る(実際に踏んだ)。
               16bit に収まる形にしておくこと: d<=40 なので d*d*16 <= 25600。 */
            p = (u16)(((u16)(d * d) * 16) / 100);
            vcam8 = (u16)(vcam8 + 160);                            /* 倒れながら少しずつ遠ざかる */
        } else {
            p = 256;
            /* ★元絵の行を進める = 艦が地平線の側へ上がっていく(遠ざかる)。
               艦は 124 テクセルあるので、これくらい動かさないと「艦の上を飛んでいる」だけに見える。 */
            vcam8 = (u16)(vcam8 + 640);                            /* 2.5 テクセル/フレーム */
        }
        frame(p);
        vdp_wait_frame();
    }
    for (i = 0; i < 16; i++) vdp_set_pal(i, 0, 0, 0);              /* 夜の海へ暗転して終わる */
    vdp_wait_frame();
    vdp_wait_frame();
}
