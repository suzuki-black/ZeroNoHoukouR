/* scene_s3test.c — ★検証用: SCREEN5(精緻) → SCREEN3(マルチカラー 64x48=粗い) の動的切替と、
   SCREEN3 での全画面 回転＋縮小(ロトズーム)。『A-JAX』の「戦艦撃破 → きりもみ急上昇」の絵が
   本機で作れるかを見るための試作。
   `make clean && make S3TEST=1` のときだけ起動シーンとしてリンクされる(通常 ROM には入らない)。

   ★なぜ SCREEN3 か: V9938/V9958 には**背景の拡大レジスタが無い**(R#1 の MAG はスプライト専用)。
     だが SCREEN3 は 1ブロック=4x4 ドットなので、モードを選ぶこと自体が「BG を 4 倍に拡大した状態」
     になる。全画面を毎フレーム書き換えてもパターン(色)テーブルの 1,536B で済む
     (SCREEN5 の全画面 27,136B=4フレーム分に対して 1/18)。

   ★絵は 1 面の戦艦(ビスマルク)。開始カードの事前ベイク画像(64x48, bank4)を使う:
     - SCREEN5 では 4 倍に拡大して 256x192 ＝ 画面いっぱい
     - SCREEN3 では 1 テクセル = 1 ブロック(4x4 ドット)＝ **同じ大きさ**
     つまり切替の瞬間に変わるのは「粗さ」だけ。そこから倍率を上げて遠ざける。
     ★カードの読み込み(data_read)は常駐側で済ませてある(バンクシーンから窓を差替えると自分が消える)。

   ★SCREEN3 の VRAM(このROMが自分で設定する。BIOS 既定に依存しない):
     パターン(色)テーブル R#4 → 0x0000 (1,536B 使用)
     名前テーブル        R#2 → 0x0800 (768B)
     スプライト属性      R#5 → 0x1B00 / スプライトパターン R#6 → 0x3800 (スプライトはモード1)
   ★名前テーブルの決まり: 文字セル行 y(0..23)・列 x(0..31) に対し name = 32*(y/4) + x。
     セル行 y はパターンの (y&3)*2 と +1 の 2 バイトだけを表示する(4 行ぶんで 8 バイトを使い切る)。
     1 バイト = 横に並ぶ 2 ブロック(上位ニブル=左, 下位=右)。結果 64x48 ブロック = 1,536B。
     ゆえにパターン表の並びは「帯 g(8行) → セル列 cx → 帯の中の行 b」の順＝縦に 8 個ずつ。

   操作: SPACE=開始/戻る  B(M)=計測(約3秒)  左右=画質 FINE/FAST  上下=手動ズーム(自動を止める) */
#include "vdp.h"
#include "input.h"
#include "scene.h"
#include "raster.h"
#include "hotcode.h"
#include "ship.h"

/* SCREEN3 のテーブル(自分で設定する) */
#define S3_PAT   0x0000      /* パターン(色)テーブル: 1,536B。ここを毎フレーム書き換える */
#define S3_NAME  0x0800      /* 名前テーブル: 768B。最初の1回だけ */
#define S3_SATR  0x1B00      /* スプライト属性(モード1: y,x,pattern,color の 4B x 32) */
#define S3_SPAT  0x3800      /* スプライトパターン */

/* 元絵 64x64 は 256 境界に置く。テクセル番地が「上位 = 0xC6 + (v>>2) / 下位 = ((v&3)<<6) + u」で
   作れる＝内側ループから 16bit 演算が消える。番地は asm の即値なので固定で書く。
   検証ROMではゲーム本体が動かない＝RAM実行コード(hot_ram)は使わないので借りてよい。 */
#define TEX_ADDR 0xC600
#define TEX      ((u8 *)TEX_ADDR)

#define ST_FINE   0          /* SCREEN5: 戦艦(4倍) */
#define ST_FLASH  1          /* 切替を隠す白フラッシュ(SCREEN5 のまま数フレーム) */
#define ST_COARSE 2          /* SCREEN3: 回転＋縮小 */
#define ST_RESULT 3          /* SCREEN5: 計測結果 */

static u8  st;
static u8  ang;              /* 角度 0..63 (1周=64) */
static u8  fast;             /* 0=FINE(1バイトに2テクセル) / 1=FAST(1バイト1テクセル=横32) */
static u8  autoseq;          /* 1=A-JAX風の自動シーケンス */
static u8  flash_t;
static u16 seq_t;            /* シーケンスの経過フレーム */
static u16 scl;              /* 倍率(16=等倍。大きいほど遠ざかる) */
u16 g_s3_tick;               /* 計測: ROM 実行で 64 フレーム相当の JIFFY。★openMSX/WebMSX から読む */
u16 g_s3_ram;                /* 計測: RAM 実行(hot_ram へ写して実行) */
u16 g_s3_xfer;               /* 計測: 転送だけ */
#define RAMF ((u8 *)0xD800)  /* RAM 実行の置き場(hot_ram の中。TEX 0xC600-0xD5FF とは重ならない) */
#define RAMQ ((u8 *)0xD900)  /* ★各 256B 枠。写す長さは関数の実測(下の s3test_init)で決める */
static void (*stripf)(void);
static void (*stripq)(void);
static u8  inram;            /* 1=内側ループを hot_ram から実行(計測用) */

/* 内側ループが読む値(asm から直接参照する) */
static s16 sA, sC;           /* ブロック1つ右への u,v の増分(8.8) */
static s16 sBA, sDC;         /* 「1つ下へ」−「1つ右へ」の差 */
static s16 sB, sD;           /* ブロック1つ下への u,v の増分(8.8) */
static u16 u0, v0;           /* いまの帯の開始座標(8.8) */
static u8  cnt;              /* strip_* のループ回数 */
static u8  nib;              /* strip_fine の左ニブル退避 */

/* cos(θ) を 64 分割・±64 で。sin は +48 ずらす。 */
static const s8 cos64[64] = {
     64, 63, 62, 60, 57, 53, 49, 45, 39, 34, 28, 21, 15,  8,  2, -4,
    -11, -17, -24, -30, -36, -41, -46, -51, -55, -58, -61, -63, -64, -64, -64, -63,
    -62, -60, -57, -53, -49, -45, -39, -34, -28, -21, -15, -8, -2,  4, 11, 17,
     24, 30, 36, 41, 46, 51, 55, 58, 61, 63, 64, 64, 64, 63, 62, 60,
};
static s8 cs(u8 a) { return cos64[a & 63]; }
static s8 sn(u8 a) { return cos64[(u8)(a + 48) & 63]; }

static void num5(u8 x, u8 y, u16 v) {
    char s[6];
    s[0] = (char)('0' + v / 10000);      s[1] = (char)('0' + (v / 1000) % 10);
    s[2] = (char)('0' + (v / 100) % 10); s[3] = (char)('0' + (v / 10) % 10);
    s[4] = (char)('0' + v % 10);         s[5] = 0;
    vdp_text(x, y, 15, 1, s);
}

/* ───────── SCREEN5: 開始カードの戦艦を 4 倍(256x192)で ───────── */
static void draw_ship5(void) {
    u8 r, q, x;
    vdp_fill(0, 0, 256, 212, 1);
    for (r = 0; r < 48; r++) {
        const u8 *src = &g_card_ram[(u16)r * 32];
        for (q = 0; q < 4; q++) {                    /* 縦 4 倍 */
            vdp_write_addr((u16)((u16)(10 + (u16)r * 4 + q) * 128));
            for (x = 0; x < 32; x++) {               /* 横 4 倍: 1 バイト(2 ドット)→ 4 バイト */
                u8 b = src[x], l = (u8)(b & 0xF0), n = (u8)(b & 0x0F);
                l = (u8)(l | (l >> 4));
                n = (u8)(n | (n << 4));
                vdp_data(l); vdp_data(l); vdp_data(n); vdp_data(n);
            }
        }
    }
    vdp_text(2, 2, 15, 1, "SCREEN5 256X212 FINE");
}

static void draw_menu(void) {
    vdp_fill(0, 180, 256, 32, 1);
    vdp_text(2, 184, 14, 1, "SPACE:SPIN AWAY  M:BENCH");
    vdp_text(2, 196, 14, 1, "LR:FINE-FAST  UD:MANUAL ZOOM");
}

static void draw_result(void) {
    u16 f1 = (u16)(g_s3_tick ? (u16)((64UL * 600UL) / g_s3_tick) : 0);
    u16 f2 = (u16)(g_s3_ram  ? (u16)((64UL * 600UL) / g_s3_ram)  : 0);
    u16 f3 = (u16)(g_s3_xfer ? (u16)((64UL * 600UL) / g_s3_xfer) : 0);
    vdp_fill(0, 0, 256, 212, 1);
    vdp_text(2, 10, 11, 1, "SCREEN3 ROTOZOOM BENCH");
    vdp_text(2, 28, 14, 1, fast ? "MODE FAST 32X48" : "MODE FINE 64X48");
    vdp_text(2, 48, 15, 1, "ROM EXEC JIFFY");   num5(150, 48, g_s3_tick);
    vdp_text(2, 60, 15, 1, "FPS X10");          num5(150, 60, f1);
    vdp_text(2, 72, 15, 1, "RAM EXEC JIFFY");   num5(150, 72, g_s3_ram);
    vdp_text(2, 84, 15, 1, "FPS X10");          num5(150, 84, f2);
    vdp_text(2, 100, 15, 1, "XFER ONLY JIFFY"); num5(150, 100, g_s3_xfer);
    vdp_text(2, 112, 15, 1, "FPS X10");         num5(150, 112, f3);
    vdp_text(2, 132, 14, 1, "1536 BYTE PER FRAME");
    vdp_text(2, 144, 14, 1, "SCALED TO 64 FRAMES");
    vdp_text(2, 166, 12, 1, "SPACE:BACK");
}

/* ───────── 元絵: 開始カード(64x48)を 64x64 のテクセルへ。上下の余りは海の斑 ───────── */
static void make_tex(void) {
    u8 ty, tx;
    for (ty = 0; ty < 64; ty++) {
        u8 *d = &TEX[(u16)ty << 6];
        if (ty >= 8 && ty < 56) {
            const u8 *src = &g_card_ram[(u16)(ty - 8) * 32];
            for (tx = 0; tx < 64; tx++)
                d[tx] = (u8)((tx & 1) ? (src[tx >> 1] & 15) : (src[tx >> 1] >> 4));
        } else {
            for (tx = 0; tx < 64; tx++) d[tx] = (u8)(((tx ^ ty) & 3) ? 1 : 2);  /* 海(斑) */
        }
    }
}

/* ───────── SCREEN3 へ切替。テーブルは自分で設定する ───────── */
static void enter_s3(void) {
    u8 i, y, x;
    raster_off();
    __asm
        ld   a, #3
        ld   (0xFCAF), a       ; SCRMOD_W = 3 (MULTI COLOUR)
        call 0x005F            ; CHGMOD
    __endasm;
    vdp_wreg(25, 0x00);        /* ★R#25 は BIOS が面倒を見ない。YJK/MSK の残留を落とす */
    vdp_wreg(2, S3_NAME / 0x400);
    vdp_wreg(4, S3_PAT / 0x800);
    vdp_wreg(5, S3_SATR / 0x80);
    vdp_wreg(6, S3_SPAT / 0x800);
    vdp_palette_game();        /* SCREEN5 と同じ 16 色にする(CHGMOD が既定色へ戻すため) */

    vdp_write_addr(S3_NAME);   /* 名前テーブル: name = 32*(y/4) + x */
    for (y = 0; y < 24; y++)
        for (x = 0; x < 32; x++) vdp_data((u8)(((y >> 2) << 5) + x));

    vdp_write_addr(S3_SPAT);
    for (i = 0; i < 8; i++) vdp_data(0x00);
    vdp_write_addr(S3_SATR);
    vdp_data(208);             /* スプライトは出さない(モード1は1ライン4枚・単色) */

    /* ★R#1 = 0x68: bit6 画面ON / bit5 VBLANK割込みON / **bit3 = M2 = 1(MULTI COLOUR)** /
       bit1 SI=0(スプライト 8x8) / bit0 MAG=0。
       ★ここで M2 を落とすと GRAPHIC1 になり、パターン表に正しく書けていても画面は一様に見える
         (実際に 0x60 と書いて「灰色一色」になった。VRAM もレジスタも他は正しいので気づきにくい)。 */
    vdp_wreg(1, 0x68);
}

static void white_pal(void);

static void enter_s5(void) {
    vdp_screen5();
    vdp_palette_game();
}

/* ───────── 1 帯(縦8ブロック=8バイト)を作って VDP へ直接流す ─────────
   ★中間バッファを置かない: 計算が律速なので、1 バイトできるたびに 0x98 へ出す方が速く RAM も要らない。
   ★テクセル番地(TEX は 0xC600 = 256 境界):
       上位 = 0xC6 + ((v>>8)&63)>>2   下位 = (((v>>8)&3)<<6) + ((u>>8)&63)
     下位は最大 192+63=255 で桁上がりしない＝加算 1 回で済む。
   ★元絵の外(u,v の整数部に bit6/7 が立つ)は海の斑にする。これが無いと、遠ざかったときに
     元絵が何枚も繰り返して見えてしまう。
   ★OTIR は使わない(R800 では VDP に速すぎる＝既知の地雷)。 */
static void strip_fine(void) __naked {
    __asm
        ld   hl, (_u0)
        ld   de, (_v0)
        ld   a, #8
        ld   (_cnt), a
    sf_loop:
        ; ★ここへ戻る jr が 127 バイトを超えたので、折り返しは中継(sf_tramp)を経由する。
        ;   RAM へ写して実行するため、絶対番地の jp は使えない(相対ジャンプだけで書く)。
        ; ---- 左ブロック ----
        ld   a, d
        or   h
        and  #0xC0
        jr   nz, sf_sea1
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
        jr   sf_got1
    sf_sea1:
        ld   a, h
        xor  d
        and  #3
        jr   z, sf_sea1b
        ld   a, #1
        jr   sf_got1
    sf_sea1b:
        ld   a, #2
    sf_got1:
        rlca
        rlca
        rlca
        rlca
        and  #0xF0
        ld   (_nib), a
        ; ---- 1 ブロック右へ ----
        ld   bc, (_sA)
        add  hl, bc
        ex   de, hl
        ld   bc, (_sC)
        add  hl, bc
        ex   de, hl
        jr   sf_over
    sf_tramp:
        jr   sf_loop
    sf_over:
        ; ---- 右ブロック ----
        ld   a, d
        or   h
        and  #0xC0
        jr   nz, sf_sea2
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
        jr   sf_got2
    sf_sea2:
        ld   a, h
        xor  d
        and  #3
        jr   z, sf_sea2b
        ld   a, #1
        jr   sf_got2
    sf_sea2b:
        ld   a, #2
    sf_got2:
        and  #0x0F
        ld   b, a
        ld   a, (_nib)
        or   b
        out  (0x98), a
        ; ---- 次の行へ(右へ出た分を差し引いて1つ下) ----
        ld   bc, (_sBA)
        add  hl, bc
        ex   de, hl
        ld   bc, (_sDC)
        add  hl, bc
        ex   de, hl
        ld   a, (_cnt)
        dec  a
        ld   (_cnt), a
        jr   nz, sf_tramp
        ret
    __endasm;
}

/* FAST: 1 バイト＝1 テクセル(左右のブロックに同じ色)。横 32 相当だが計算が半分。 */
static void strip_fast(void) __naked {
    __asm
        ld   hl, (_u0)
        ld   de, (_v0)
        ld   a, #8
        ld   (_cnt), a
    sq_loop:
        ld   a, d
        or   h
        and  #0xC0
        jr   nz, sq_sea
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
        jr   sq_got
    sq_sea:
        ld   a, h
        xor  d
        and  #3
        jr   z, sq_seab
        ld   a, #1
        jr   sq_got
    sq_seab:
        ld   a, #2
    sq_got:
        ld   b, a
        rlca
        rlca
        rlca
        rlca
        or   b
        out  (0x98), a
        ld   bc, (_sB)
        add  hl, bc
        ex   de, hl
        ld   bc, (_sD)
        add  hl, bc
        ex   de, hl
        ld   a, (_cnt)
        dec  a
        ld   (_cnt), a
        jr   nz, sq_loop
        ret
    __endasm;
}

/* 1 画面ぶん(1,536B)。パターン表の並び＝帯 g(8行) → セル列 cx → 帯の中の行。
   ★掛け算は帯の頭だけ。SDCC の 16bit 乗算を内側に置くと 1 フレームの半分を食う(実測)。 */
static void frame(void) {
    u8 g, cx;
    s16 A = (s16)(((s16)cs(ang) * (s16)scl) >> 2);   /* ブロック1つ右への u の増分(8.8) */
    s16 C = (s16)(((s16)sn(ang) * (s16)scl) >> 2);
    s16 U0, V0;
    sA = A; sC = C;
    sB = (s16)(-C); sD = A;
    sBA = (s16)(-C - A); sDC = (s16)(A - C);
    /* 画面中央(ブロック 32,24)が元絵の中央(32,32)に来るように原点をずらす＝中心で回る */
    U0 = (s16)(0x2000 - (s16)(32 * A) - (s16)(24 * sB));
    V0 = (s16)(0x2000 - (s16)(32 * C) - (s16)(24 * sD));
    vdp_write_addr(S3_PAT);
    for (g = 0; g < 6; g++) {
        u16 gu = (u16)(U0 + (s16)(g << 3) * sB);
        u16 gv = (u16)(V0 + (s16)(g << 3) * sD);
        for (cx = 0; cx < 32; cx++) {
            u0 = gu; v0 = gv;
            if (inram) { if (fast) stripq(); else stripf(); }
            else       { if (fast) strip_fast(); else strip_fine(); }
            gu = (u16)(gu + A + A);                  /* セル1つ右＝ブロック2つ右 */
            gv = (u16)(gv + C + C);
        }
    }
}

/* 転送だけ(計算ゼロ)の基準値: TEX の先頭 1,536B をそのまま流す。 */
static void xfer_only(void) {
    vdp_write_addr(S3_PAT);
    __asm
        ld   hl, #0xC600
        ld   bc, #1536
    xo_loop:
        ld   a, (hl)
        out  (0x98), a
        inc  hl
        dec  bc
        ld   a, b
        or   c
        jp   nz, xo_loop
    __endasm;
}

/* ★計測は 16 フレーム×3本(約3秒)。64 フレーム×3本にしていたときは turboR で 12 秒間
   まったく反応が無くなり「フリーズした」と誤解された(実測 738 jiffy)。
   計測中はボーダー(R#7)の色を変えて走っていることを見せる。 */
#define BENCH_N 16
static void bench(void) {
    volatile u16 *j = (volatile u16 *)0xFC9E;
    u16 t0;
    u8 i;
    vdp_wreg(7, 0x08);
    inram = 0;
    t0 = *j;
    for (i = 0; i < BENCH_N; i++) { ang++; frame(); }
    g_s3_tick = (u16)((*j - t0) * (64 / BENCH_N));
    vdp_wreg(7, 0x06);
    inram = 1;
    t0 = *j;
    for (i = 0; i < BENCH_N; i++) { ang++; frame(); }
    g_s3_ram = (u16)((*j - t0) * (64 / BENCH_N));
    vdp_wreg(7, 0x04);
    inram = 0;
    t0 = *j;
    for (i = 0; i < BENCH_N; i++) xfer_only();
    g_s3_xfer = (u16)((*j - t0) * (64 / BENCH_N));
    inram = 1;                 /* 演出は RAM 実行が既定 */
    vdp_wreg(7, 0x00);
}

/* ───────── A-JAX 風シーケンス: 回転を速めながら遠ざける ───────── */
/* ★倍率の上限は 48(=1ブロック3テクセル)。座標は 8.8 の 16bit なので、画面の端で
   ±256 テクセルを超えると**折り返して元絵が何枚も見える**(実際に拡大したように見えた)。
   64ブロック×3テクセル=192 テクセル < 256 に収めるのが安全圏。
   遠ざかりきったら白で飛ばして戻る(本番では結果画面へ繋ぐ想定)。 */
#define SCL_MAX  48
#define SEQ_END  52          /* 約 2.6 秒(RAM 実行 19.6fps 前提) */
static void seq_step(void) {
    u16 t = seq_t;
    seq_t = (u16)(t + 1);
    ang = (u8)(ang + 1 + (u8)(t >> 3));              /* だんだん速く回る */
    if (t < 8) return;                               /* 最初の数フレームは等倍＝「粗くなった」を見せる */
    {
        u16 k = (u16)(t - 8);
        u16 s = (u16)(16 + (u16)((k * k) >> 3));     /* 加速しながら遠ざかる(約1秒で最小へ) */
        scl = (s > SCL_MAX) ? SCL_MAX : s;
    }
}

static void s3test_init(void) {
    u16 k;
    /* ★写す長さは「次の関数との番地差」で取る。固定 128B で写していたら、境界チェックを足して
       154B に伸びた strip_fine が途中で切れ、RAM 実行だけ画面が一様になった(実際に踏んだ)。 */
    const u8 *sf = (const u8 *)strip_fine, *sq = (const u8 *)strip_fast;
    const u8 *se = (const u8 *)frame;
    u16 lf = (u16)(sq - sf), lq = (u16)(se - sq);
    st = ST_FINE; ang = 0; scl = 16; fast = 0; autoseq = 1; seq_t = 0;
    inram = 1;   /* ★既定で RAM 実行(hot_ram)。ROM 実行では 8fps しか出ず演出にならない */
    g_s3_tick = 0; g_s3_ram = 0; g_s3_xfer = 0;
    if (lf > 256 || lq > 256) { lf = 0; lq = 0; }                    /* 枠に入らないなら RAM 実行を諦める */
    for (k = 0; k < lf; k++) RAMF[k] = sf[k];
    for (k = 0; k < lq; k++) RAMQ[k] = sq[k];
    stripf = lf ? (void (*)(void))RAMF : strip_fine;
    stripq = lq ? (void (*)(void))RAMQ : strip_fast;
    make_tex();
    vdp_set_vscroll(0);
    vdp_set_display_page(0);
    vdp_sprite_init();
    vdp_sprite_hide_from(0);
    enter_s5();
    draw_ship5();
    draw_menu();
}

static void white_pal(void) {
    u8 i;
    for (i = 0; i < 16; i++) vdp_set_pal(i, 7, 7, 7);
}

static u8 s3test_update(void) {
    if (st == ST_FLASH) {                            /* 切替の瞬間を白で隠す */
        if (--flash_t == 0) {
            enter_s3();
            st = ST_COARSE;
        }
        return SCENE_NONE;
    }
    if (st == ST_COARSE) {
        if (g_input_edge & INP_TRIG)  { enter_s5(); draw_ship5(); draw_menu(); st = ST_FINE; return SCENE_NONE; }
        if (g_input_edge & INP_TRIGB) { bench(); enter_s5(); draw_result(); st = ST_RESULT; return SCENE_NONE; }
        if (g_input & INP_UP)         { autoseq = 0; if (scl > 8)   scl = (u16)(scl - 4); }
        if (g_input & INP_DOWN)       { autoseq = 0; if (scl < 512) scl = (u16)(scl + 4); }
        if (g_input_edge & INP_LEFT)  fast = 0;
        if (g_input_edge & INP_RIGHT) fast = 1;
        if (autoseq) {
            seq_step();
            if (seq_t > SEQ_END) {                   /* 遠ざかりきった: 白で飛ばして戻る */
                enter_s5(); white_pal(); draw_ship5(); draw_menu();
                vdp_palette_game();
                st = ST_FINE;
                return SCENE_NONE;
            }
        } else ang++;
        frame();
        return SCENE_NONE;
    }
    /* SCREEN5 側(戦艦 / 計測結果) */
    if (g_input_edge & INP_TRIGB) {       /* ★計測は SCREEN5 からも始められる(演出は 2.6 秒で終わるので) */
        enter_s3();
        bench();
        enter_s5();
        draw_result();
        st = ST_RESULT;
        return SCENE_NONE;
    }
    if (g_input_edge & INP_TRIG) {
        if (st == ST_RESULT) { draw_ship5(); draw_menu(); st = ST_FINE; return SCENE_NONE; }
        ang = 0; scl = 16; seq_t = 0; autoseq = 1;
        white_pal();                                 /* 白フラッシュ → 数フレーム後に SCREEN3 へ */
        flash_t = 4;
        st = ST_FLASH;
    }
    return SCENE_NONE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) s3test_init();
    else g_scene_ret = s3test_update();
}
