/* ovl_spin.c — 撃沈の直後、**戦艦を丸ごと**粗くして、きりもみしながら遠ざける。
   『A-JAX』の「戦艦撃破 → きりもみ急上昇」の絵。撃沈オーバレイ(OVL7)に同居する。

   ★仕掛け: V9938/V9958 に背景の拡大レジスタは無い(R#1 の MAG はスプライト専用)。
     だが SCREEN3(マルチカラー)は 1 ブロック = 4x4 ドットなので、モードを選ぶこと自体が
     「背景を 4 倍に拡大した状態」になる。全画面を毎フレーム書き換えてもパターン(色)テーブルの
     1,536B で済む(SCREEN5 の全画面 27,136B = 4 フレーム分に対して 1/18)。

   ★元絵は「画面」ではなく**戦艦バッファ B(VRAM 528行〜, 256x496 = 艦の全長)**から取る。
     画面(192行)だけを取ると、遠ざかったときに**切り取った長方形が回っているだけ**になり、
     艦の船首・船尾が直線で切れて角が見える(ユーザー指摘)。B なら艦の全体が入る。
     4 ドットを 1 テクセルにして 64x124 テクセル。1 テクセル = 1 ブロックなので、
     切り替えた瞬間の見かけの大きさは画面と同じ＝つながる。
   ★テクセルは 4bit 詰め(1 バイトに 2 つ)。64x128 で 4,096B に収める(RAM は hot_ram を借りる)。
   ★艦の外側は「海のタイル 16x16」で埋める。これが無いと、遠ざかったときに元絵の縁が見える。

   ★置き場: 元絵(0xC600-0xD5FF)・海タイル(0xD600-0xD6FF)・内側ループ(0xD700-0xD7FF)は hot_ram を
     借りる。終わったら常駐側が hot_load() で戻す。番地は Makefile が範囲検査する。
   ★内側ループは RAM 実行。ROM 実行だと 7fps しか出ない(turboR 実測)。
   ★倍率の上げすぎは禁物: 座標が 8.8 の 16bit なので、画面端で ±256 テクセルを超えると
     折り返して元絵が何枚も出る(試作で踏んだ)。 */
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "hotcode.h"
#include "overlay.h"
#include "scroll.h"
#include "aa_hot.h"        /* cam(縦スクロールカメラ=世界Y) */

#include "spinfx.h"        /* TEX / SEA / S3_* と、5面のパース(ovl_tilt.c)と共有する土台 */

#define RAMF      ((u8 *)0xD700)   /* 内側ループの RAM 実行先 */
#define S3_SATR  0x1B00
#define S3_SPAT  0x3800

#define SCL_MAX  40        /* 1 ブロック 2.5 テクセル。これ以上は折り返して像が並ぶ */
#define HOLD_T   10        /* 最初は止めておくフレーム数(撃破の画面のまま) */
#define SEQ_END  64        /* 約 3.5 秒 */

static u8  ang;
static u16 scl;
static u16 seq_t;
static u16 cy;             /* 元絵のどの行を画面中央に置くか(8.8) */
static u16 cx8;            /* 元絵のどの列を画面中央に置くか(8.8)。元絵を半分に潰すと半分になる */
/* 内側ループが読む値(asm から直接参照する) */
static s16 sA, sC, sBA, sDC;
static u16 u0, v0;
static u8  cnt, nib;

/* ★64 分割の cos(振幅 64)。**1 周 = 64 要素ちょうど**にすること。
   以前の表は 1 周が 58 要素ほどしかない歪んだ表で、
     ・sn(0) = cos64[48] が 0 でなく 24 になり、**演出の1枚目からいきなり 20 度傾いて**いた
       (「撃破のスクリーンショットから始まらない」の正体)。
     ・(cos,sin) の長さが 64 からずれるので、回りながら大きさが数%脈打っていた。 */
static const s8 cos64[64] = {
     64,  64,  63,  61,  59,  56,  53,  49,  45,  41,  36,  30,  24,  19,  12,   6,
      0,  -6, -12, -19, -24, -30, -36, -41, -45, -49, -53, -56, -59, -61, -63, -64,
    -64, -64, -63, -61, -59, -56, -53, -49, -45, -41, -36, -30, -24, -19, -12,  -6,
      0,   6,  12,  19,  24,  30,  36,  41,  45,  49,  53,  56,  59,  61,  63,  64,
};
static s8 cs(u8 a) { return cos64[a & 63]; }
static s8 sn(u8 a) { return cos64[(u8)(a + 48) & 63]; }

__sfr __at(0x98) SPIN_DAT;    /* VRAM データ   */
__sfr __at(0x99) SPIN_CTRL;   /* アドレス/レジスタ */

/* ★VRAM の 行512 以降は バイト番地が 0x10000 を超える。常駐の vdp_read_addr(u16) では
   届かず、下位16bitだけが効いて**画面(page0)を読んでしまう**(実際に踏んだ: 艦の代わりに
   HUD と甲板が混ざった絵になった)。ここは A16 を含めて自分で設定する。 */
static void read_row(u16 row) {
    /* ★R#14 は A14-A16(=16KB 単位)。下位のラッチは 14bit しか無い。
       1 行 128B なので A14-16 = row>>7、ラッチ = (row & 127) * 128。
       ここを row>>9 / (row&511)*128 と書いて 16KB 単位を取り違え、
       **開始カードの絵を 128 行ごとに 4 回読む**という結果になった(実際に踏んだ)。 */
    u8  a16 = (u8)(row >> 7);
    u16 lo  = (u16)((row & 127) << 7);
    __asm di __endasm;
    SPIN_CTRL = (u8)(a16 & 7);   SPIN_CTRL = 0x80 | 14;
    SPIN_CTRL = (u8)(lo & 0xFF); SPIN_CTRL = (u8)((lo >> 8) & 0x3F);
    __asm ei __endasm;
}
#define spin_read() SPIN_DAT

/* ───────── 戦艦バッファ B(256x496)を 64x124 テクセル(4bit詰め)へ ─────────
   VRAM の 1 行は 128B(1B=2 ドット)。4 ドットおき＝2 バイトおきに上位ニブルを拾う。
   ついでに左端(海しか無い)から 16x16 の海タイルを作る。 */
void spinfx_grab(void) {
    u8 ty, tx;
    /* ★海のタイルは海テンプレート(VRAM 512行〜, 16px)から取る。艦バッファの端から取ると
       船首の絵を「海」として撒いてしまい、背景が縞になる(実際に踏んだ)。 */
    for (ty = 0; ty < 16; ty++) {
        read_row((u16)(SC_SEATMPL_Y + ty));
        for (tx = 0; tx < 8; tx++) {
            u8 a = spin_read(), b = spin_read();
            SEA[((u16)ty << 4) | (u8)(tx << 1)]       = (u8)(a >> 4);
            SEA[((u16)ty << 4) | (u8)((tx << 1) + 1)] = (u8)(b >> 4);
        }
    }
    for (ty = 0; ty < 128; ty++) {
        u8 *d = &TEX[(u16)ty << 5];            /* 1 行 32B(64 テクセル) */
        if (ty >= TEX_H) {                     /* 絵の下の余り: 海タイルで埋める */
            for (tx = 0; tx < 32; tx++) d[tx] = (u8)((SEA[((ty & 15) << 4) | ((tx << 1) & 15)] << 4)
                                                    | SEA[((ty & 15) << 4) | (((tx << 1) + 1) & 15)]);
            continue;
        }
        /* ★元絵は「撃破の瞬間の画面そのもの」。画面に映っている行は**表示リング**から取る
           (燃えている艦・爆発・海のいまの姿がそのまま入る)。画面から外れている艦首・艦尾だけを
           バッファ B で足す。B だけから作ると、燃えていない素の艦が回り出して
           「撃破の画面と繋がらない」(ユーザー指摘。これで 3 回作り直した)。
           ★対応: 元絵の行 ty = 世界Y(BOW_Y + ty*4) / 画面Y = 世界Y - cam /
                   リング行 = 256 + ((g_vscroll + 画面Y) & 255)。 */
        { s16 sy = (s16)((s16)(SC_SHIP_R0 * 16) + (s16)((u16)ty << 2) - (s16)cam);
          if (sy >= 0 && sy < 212) read_row((u16)(256 + (u8)(g_vscroll + (u8)sy)));
          else                     read_row((u16)(SC_SHIPBUF_Y + (u16)ty * 4)); }
        for (tx = 0; tx < 32; tx++) {
            u8 a, b, l, r;
            a = spin_read(); (void)spin_read();   /* 偶数テクセル(4 ドットおき) */
            b = spin_read(); (void)spin_read();   /* 奇数テクセル */
            l = (u8)(a >> 4);
            r = (u8)(b >> 4);
            /* ★B は「艦の絵＋色0(透明)の背景」。色0 のままだと黒い帯が回ってしまうので、
               背景は海のタイルで埋める(撃沈の演出では B を透明コピーで海へ重ねている)。 */
            if (!l) l = SEA[((u16)(ty & 15) << 4) | (u8)((tx << 1) & 15)];
            if (!r) r = SEA[((u16)(ty & 15) << 4) | (u8)(((tx << 1) + 1) & 15)];
            d[tx] = (u8)((l << 4) | r);
        }
    }
}

/* ───────── 1 帯(縦8ブロック=8バイト)を作って VDP へ直接流す ─────────
   ★テクセル番地(TEX は 256 境界・1 行 32B): 上位 = >TEX + (v>>3) / 下位 = ((v&7)<<5) + (u>>1)
     ニブルは u の偶奇で選ぶ。
   ★元絵の外(u が 64 以上 / v が 128 以上)は海タイル(SEA, 16x16)へ落とす。
   ★相対ジャンプだけで書く(そのまま hot_ram へ写して RAM 実行するため)。折り返しは中継を経由。
   ★OTIR は使わない(R800 では VDP に速すぎる＝既知の地雷)。 */
static void strip(void) __naked {
    __asm
        ld   hl, (_u0)
        ld   de, (_v0)
        ld   a, #8
        ld   (_cnt), a
    sp_loop:
        ;; ---- 左ブロックのテクセル ----
        ld   a, d
        and  #0x80
        jr   nz, sp_sea1
        ld   a, h
        and  #0xC0
        jr   nz, sp_sea1
        ld   a, d
        rrca
        rrca
        rrca
        and  #0x0F
        add  a, #0xC6
        ld   b, a
        ld   a, d
        and  #7
        rlca
        rlca
        rlca
        rlca
        rlca
        ld   c, a
        ld   a, h
        srl  a
        add  a, c
        ld   c, a
        ld   a, (bc)
        bit  0, h
        jr   z, sp_hi1
        and  #0x0F
        jr   sp_got1
    sp_hi1:
        rrca
        rrca
        rrca
        rrca
        and  #0x0F
        jr   sp_got1
    sp_sea1:
        ld   a, d
        and  #15
        rlca
        rlca
        rlca
        rlca
        ld   c, a
        ld   a, h
        and  #15
        add  a, c
        ld   c, a
        ld   b, #0xD6
        ld   a, (bc)
    sp_got1:
        rlca
        rlca
        rlca
        rlca
        and  #0xF0
        ld   (_nib), a
        ;; ---- 1 ブロック右へ ----
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
        ;; ---- 右ブロックのテクセル ----
        ld   a, d
        and  #0x80
        jr   nz, sp_sea2
        ld   a, h
        and  #0xC0
        jr   nz, sp_sea2
        ld   a, d
        rrca
        rrca
        rrca
        and  #0x0F
        add  a, #0xC6
        ld   b, a
        ld   a, d
        and  #7
        rlca
        rlca
        rlca
        rlca
        rlca
        ld   c, a
        ld   a, h
        srl  a
        add  a, c
        ld   c, a
        ld   a, (bc)
        bit  0, h
        jr   z, sp_hi2
        and  #0x0F
        jr   sp_got2
    sp_hi2:
        rrca
        rrca
        rrca
        rrca
        and  #0x0F
        jr   sp_got2
    sp_sea2:
        ld   a, d
        and  #15
        rlca
        rlca
        rlca
        rlca
        ld   c, a
        ld   a, h
        and  #15
        add  a, c
        ld   c, a
        ld   b, #0xD6
        ld   a, (bc)
    sp_got2:
        and  #0x0F
        ld   b, a
        ld   a, (_nib)
        or   b
        out  (0x98), a
        ;; ---- 次の行へ(右へ出た分を差し引いて1つ下) ----
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

/* 海のタイルから 1 行ぶん(32B=64テクセル)作る。 */
static void sea_row(u8 *d, u8 ty) {
    u8 tx;
    for (tx = 0; tx < 32; tx++)
        d[tx] = (u8)((SEA[((u16)(ty & 15) << 4) | (u8)((tx << 1) & 15)] << 4)
                     | SEA[((u16)(ty & 15) << 4) | (u8)(((tx << 1) + 1) & 15)]);
}

/* ★元絵を半分の解像度に作り直す(1 テクセル = 4 → 8 ドット)。
   座標が 8.8 の 16bit なので、倍率は SCL_MAX(≒2.7倍)より上げられない
   (画面端で s16 を溢れ、元絵が折り返して何枚も出る)。そこで**元絵の方を半分に潰し**、
   倍率を半分に戻して続ける＝見かけは滑らかに縮み続ける。中心(cy/cx8)も半分にする。
   ★詰め方: テクセルは 4bit。出力テクセル tx = 入力テクセル 2tx = 入力バイト tx の上位ニブル。
     出力バイト tx には入力バイト 2tx・2tx+1 の上位ニブルが入る。出力は入力より手前なので
     同じ行を上書きしながら進んでも壊れない。 */
static void tex_halve(void) {
    u8 ty, tx;
    for (ty = 0; ty < 64; ty++) {
        u8 *d = &TEX[(u16)ty << 5];
        const u8 *sp = &TEX[(u16)((u16)ty << 1) << 5];
        for (tx = 0; tx < 16; tx++) {
            u8 a = sp[(u8)(tx << 1)], b = sp[(u8)((tx << 1) + 1)];
            d[tx] = (u8)((a & 0xF0) | (b >> 4));
        }
        { u8 sea[32]; sea_row(sea, ty); for (tx = 16; tx < 32; tx++) d[tx] = sea[tx]; }
    }
    for (ty = 64; ty < 128; ty++) sea_row(&TEX[(u16)ty << 5], ty);
}

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
    /* 画面中央(ブロック 32,24)を元絵の (32, cy) に合わせる＝そこを中心に回る */
    U0 = (s16)((s16)cx8 - (s16)(32 * A) - (s16)(24 * B));
    V0 = (s16)((s16)cy - (s16)(32 * C) - (s16)(24 * D));
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
void spinfx_enter_s3(void) {
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
    if (len > 256) return;                    /* 枠に入らない＝やらない(安全側) */
    spinfx_grab();                            /* 撃破の画面(＋画面外の艦)を吸い出す */
    for (k = 0; k < len; k++) RAMF[k] = s[k]; /* 内側ループを RAM へ(ROM 実行では 7fps) */
    stripr = (void (*)(void))RAMF;
    /* ★回転の中心は**いま画面に見えている中心**のまま動かさない。艦の中心へ寄せると
       「別の場所へカメラが飛んでから回る」ことになり、撃沈の画面との繋がりが切れる(ユーザー指摘)。
       B の行 = 世界Y - BOW_Y なので、画面中央の世界Y(cam+106)に対応する元絵の行は
       (cam + 106 - 32) / 4。画面の外側は B の続き(艦の残り)と海タイルが埋める。 */
    cy  = (u16)((u16)(((cam + 74) >> 2) & 127) << 8);
    cx8 = 0x2000;                             /* 元絵の横の中心(テクセル32) */
    ang = 0; scl = 16; seq_t = 0;
    spinfx_enter_s3();
    /* ★台本: 撃破の画面が**そのまま粗くなっただけ**の状態で一度止まり(HOLD_T)、
       そこから回転も縮小も加速する。最後まで縮み続ける(倍率の頭打ちは元絵を半分に
       潰して回避)。いきなり回して縮めると「別の絵に切り替わった」ように見える。 */
    { u8 halves = 0;
      while (seq_t <= SEQ_END) {
        u16 t = seq_t++;
        if (t >= HOLD_T) {
            u16 d = (u16)(t - HOLD_T);
            ang = (u8)(ang + 1 + (u8)(d >> 3));   /* だんだん速く回る */
            scl = (u16)(scl + 1 + (d >> 4));      /* だんだん速く縮む */
            if (scl > SCL_MAX) {
                if (halves < 2) { tex_halve(); halves++; scl >>= 1; cy >>= 1; cx8 >>= 1; }
                else scl = SCL_MAX;
            }
        }
        frame();
        vdp_wait_frame();
      }
    }
    for (k = 0; k < 16; k++) vdp_set_pal((u8)k, 7, 7, 7);   /* 白で飛ばして終わる */
    vdp_wait_frame();
    vdp_wait_frame();
}
