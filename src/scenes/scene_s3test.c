/* scene_s3test.c — ★検証用: SCREEN5(精緻) → SCREEN3(マルチカラー 64x48=粗い) の動的切替と、
   SCREEN3 での全画面 回転＋拡大縮小(ロトズーム)が何 fps 出るか。
   `make clean && make S3TEST=1` のときだけ起動シーンとしてリンクされる(通常 ROM には入らない)。

   ★なぜ SCREEN3 か: V9938/V9958 には**背景の拡大レジスタが無い**(R#1 の MAG はスプライト専用)。
     だが SCREEN3 は 1ブロック=4x4 ドットなので、モードを選ぶこと自体が「BG を 4 倍に拡大した状態」
     になる。しかもスプライトはドット等倍のまま乗る＝「BG 粗い・スプライト精緻」が成立する。
   ★数字の根拠: SCREEN5 の全画面書換は 256x212=27,136B=4フレーム分で不可能。SCREEN3 は
     パターン(色)テーブル 1,536B だけで全画面が変わる(名前テーブル 768B は最初の1回)。
     転送だけなら turboR 実測の 4.97µs/byte で 7.6ms。**律速は転送ではなく回転の計算**。

   ★SCREEN3 の VRAM(このROMが自分で設定する。BIOS 既定に依存しない):
     パターン(色)テーブル R#4 → 0x0000 (1,536B 使用)
     名前テーブル        R#2 → 0x0800 (768B)
     スプライト属性      R#5 → 0x1B00 / スプライトパターン R#6 → 0x3800 (スプライトはモード1)
   ★名前テーブルの決まり: 文字セル行 y(0..23)・列 x(0..31) に対し name = 32*(y/4) + x。
     セル行 y はパターンの (y&3)*2 と +1 の 2 バイトだけを表示する(4 行ぶんで 8 バイトを使い切る)。
     1 バイト = 横に並ぶ 2 ブロック(上位ニブル=左, 下位=右)。結果 64x48 ブロック = 1,536B。
     ゆえにパターン表の並びは「帯 g(8行) → セル列 cx → 帯の中の行 b」の順＝縦に 8 個ずつ。

   ★見るところ:
     1. SCREEN5 ⇄ SCREEN3 を実行中に往復できるか(BIOS CHGMOD は VRAM を広く消す。戻ったら描き直す)。
     2. 粗くなった絵が「拡大された」ように見えるか。
     3. 回転縮小が何 fps で回るか(B で 64 フレーム計測 → SCREEN5 に戻って数字を出す)。
     4. スプライトが等倍のまま乗るか(下に 2 枚出す。BG の 4x4 ブロックと見比べる)。
   操作: SPACE=SCREEN5/SCREEN3 切替  B(M)=計測  上下=倍率  左右=画質(FINE/FAST)  */
#include "vdp.h"
#include "input.h"
#include "scene.h"
#include "raster.h"
#include "hotcode.h"

/* SCREEN3 のテーブル(自分で設定する) */
#define S3_PAT   0x0000      /* パターン(色)テーブル: 1,536B。ここを毎フレーム書き換える */
#define S3_NAME  0x0800      /* 名前テーブル: 768B。最初の1回だけ */
#define S3_SATR  0x1B00      /* スプライト属性(モード1: y,x,pattern,color の 4B x 32) */
#define S3_SPAT  0x3800      /* スプライトパターン */

/* 元絵 64x64 は hot_ram[] の中の 256 境界へ置く(検証ROMではRAM実行コードを使わないので借りてよい)。
   ★256 境界に置くと、テクセル番地が「上位 = 0xC6 + (v>>2) / 下位 = ((v&3)<<6) + u」で作れる＝
     内側ループから 16bit 演算が消える。番地は固定で書く(asm の即値にするため)。
     hot_ram の実番地がずれたら init で検出して赤い画面を出す。 */
#define TEX_ADDR 0xC600
#define TEX      ((u8 *)TEX_ADDR)

#define ST_FINE   0          /* SCREEN5: 精緻な絵 */
#define ST_COARSE 1          /* SCREEN3: 回転縮小 */
#define ST_RESULT 2          /* SCREEN5: 計測結果 */

static u8  st;
static u8  ang;              /* 角度 0..63 (1周=64) */
static u8  scl;              /* 倍率 16=等倍(1/16 固定小数)。小さいほど拡大 */
static u8  fast;             /* 0=FINE(1バイトに2テクセル) / 1=FAST(1バイト1テクセル=横32) */
static u8  bad;              /* 1=hot_ram の番地が想定外 */
u16 g_s3_tick;               /* 計測: ROM 実行で 64 フレームに要した JIFFY。★openMSX から読むため非 static */
u16 g_s3_ram;                /* 計測: 同じものを RAM 実行(hot_ram)で */
u16 g_s3_xfer;               /* 計測: 転送だけ 64 回ぶんの JIFFY */
/* ★RAM 実行の置き場(hot_ram の中。TEX 0xC600-0xD5FF とは重ならない)。
   内側ループは相対ジャンプだけ・参照する番地はすべて絶対なので、そのまま写せば RAM で動く。
   本作の実測では ROM 実行 : RAM 実行 = 3.84 : 1(性能と高速化 §0-0)。ここが効くかを見る。 */
#define RAMF ((u8 *)0xD800)
#define RAMQ ((u8 *)0xD980)
static void (*stripf)(void);
static void (*stripq)(void);
#define bench_tick g_s3_tick
#define bench_xfer g_s3_xfer

/* 内側ループが読む値(asm から直接参照する) */
static s16 sA, sC;           /* ブロック1つ右への u,v の増分(8.8) */
static s16 sBA, sDC;         /* 「1つ下へ」−「1つ右へ」の差(右のテクセルを採った後の戻し込み) */
static s16 sB, sD;           /* ブロック1つ下への u,v の増分(8.8) */
static u16 u0, v0;           /* いまの帯の開始座標(8.8) */

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
    s[0] = (char)('0' + v / 10000);     s[1] = (char)('0' + (v / 1000) % 10);
    s[2] = (char)('0' + (v / 100) % 10); s[3] = (char)('0' + (v / 10) % 10);
    s[4] = (char)('0' + v % 10);        s[5] = 0;
    vdp_text(x, y, 15, 1, s);
}

/* ───────── SCREEN5: 細かい絵。粗くなったときの落差が分かるものを描く ───────── */
static void draw_fine(void) {
    u16 x, y;
    vdp_fill(0, 0, 256, 212, 1);
    for (x = 0; x < 256; x = (u16)(x + 4)) vdp_fill(x, 24, 1, 120, 12);      /* 4 ドット間隔の縦線(1ドット幅) */
    for (y = 24; y < 144; y = (u16)(y + 4)) vdp_fill(0, y, 256, 1, 4);       /* 4 ドット間隔の横線 */
    for (y = 0; y < 60; y++) {                                               /* 斜めの帯(回すと分かる) */
        vdp_fill((u16)(40 + y), (u16)(30 + y), 40, 1, 8);
        vdp_fill((u16)(190 - y), (u16)(30 + y), 40, 1, 10);
    }
    for (y = 0; y < 32; y++) {                                               /* 中央の菱形(回転の目印) */
        u8 w = (u8)((y < 16) ? y : 31 - y);
        vdp_fill((u16)(128 - w * 2), (u16)(56 + y * 2), (u16)(w * 4 + 2), 2, 15);
    }
    vdp_fill(16, 150, 224, 18, 0);
    vdp_text(20, 154, 11, 0, "FINE 256X212 DOT");
    vdp_text(2, 176, 15, 1, "SPACE:SCREEN5 - SCREEN3");
    vdp_text(2, 188, 15, 1, "B:BENCH 64 FRAMES");
    vdp_text(2, 200, 14, 1, "UD:ZOOM LR:FINE-FAST");
    if (bad) vdp_text(2, 164, 9, 1, "WARN: HOT_RAM MOVED");
}

static void draw_result(void) {
    /* fps x10 = 64 フレーム / (jiffy/60) = 64*600/jiffy */
    u16 fps10 = (u16)(bench_tick ? (u16)((64UL * 600UL) / bench_tick) : 0);
    u16 xf10  = (u16)(bench_xfer ? (u16)((64UL * 600UL) / bench_xfer) : 0);
    vdp_fill(0, 0, 256, 212, 1);
    vdp_text(2, 10, 11, 1, "SCREEN3 ROTOZOOM BENCH");
    vdp_text(2, 28, 14, 1, fast ? "MODE FAST 32X48" : "MODE FINE 64X48");
    vdp_text(2, 48, 15, 1, "ROM EXEC JIFFY");  num5(150, 48, bench_tick);
    vdp_text(2, 60, 15, 1, "FPS X10");         num5(150, 60, fps10);
    vdp_text(2, 72, 15, 1, "RAM EXEC JIFFY");  num5(150, 72, g_s3_ram);
    vdp_text(2, 84, 15, 1, "FPS X10");         num5(150, 84, (u16)(g_s3_ram ? (u16)((64UL * 600UL) / g_s3_ram) : 0));
    vdp_text(2, 100, 15, 1, "XFER ONLY JIFFY"); num5(150, 100, bench_xfer);
    vdp_text(2, 112, 15, 1, "FPS X10");         num5(150, 112, xf10);
    vdp_text(2, 130, 14, 1, "1536 BYTE PER FRAME");
    vdp_text(2, 142, 14, 1, "JIFFY 60 PER SEC");
    vdp_text(2, 162, 12, 1, "SPACE:BACK TO SCREEN3");
}

/* ───────── SCREEN5 の絵を 64x64 のテクセルへ吸い出す(切替の瞬間に1回) ───────── */
static void grab_tex(void) {
    u8 ty, tx;
    for (ty = 0; ty < 64; ty++) {
        u16 sy = (u16)(ty * 3);                 /* 0..189 行をたどる */
        vdp_read_addr((u16)(sy * 128));         /* SCREEN5: 1 行 128B (1B=2 ドット) */
        for (tx = 0; tx < 64; tx++) {
            u8 b = vdp_read_data();             /* 偶数ドット=上位ニブル */
            (void)vdp_read_data();              /* 4 ドットおきに拾う＝1 バイト読み飛ばす */
            TEX[((u16)ty << 6) | tx] = (u8)(b >> 4);
        }
    }
}

/* ───────── SCREEN3 へ切替。テーブルは自分で設定する ───────── */
static void enter_s3(void) {
    u16 i;
    u8 y, x;
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

    vdp_write_addr(S3_SPAT);   /* スプライト(モード1)の絵: 8x8 の枠 */
    for (i = 0; i < 8; i++) vdp_data((u8)((i == 0 || i == 7) ? 0xFF : 0x81));
    vdp_write_addr(S3_SATR);   /* 等倍のまま乗ることの確認用に 2 枚 */
    vdp_data(150); vdp_data(40);  vdp_data(0); vdp_data(15);
    vdp_data(150); vdp_data(200); vdp_data(0); vdp_data(11);
    vdp_data(208);                                        /* Y=208 = 以降表示しない */
    vdp_wreg(1, 0x60);         /* 画面 ON・VBLANK 割込み ON・スプライト 8x8・MAG=0 */
}

static void enter_s5(void) {
    vdp_screen5();
    vdp_palette_game();
}

/* ───────── 1 帯(縦8ブロック分=8バイト)を作って VDP へ直接流す ─────────
   ★中間バッファを置かない: 計算が律速なので、1 バイトできるたびに 0x98 へ出す方が速く RAM も要らない。
   ★テクセル番地の作り方(TEX は 0xC600 = 256 境界):
       上位 = 0xC6 + ((v>>8)&63)>>2   下位 = (((v>>8)&3)<<6) + ((u>>8)&63)
     下位は最大 192+63=255 で桁上がりしない＝加算 1 回で済む。
   ★OTIR は使わない(R800 では VDP に速すぎる＝既知の地雷)。 */
static void strip_fine(void) __naked {
    __asm
        ld   hl, (_u0)
        ld   de, (_v0)
        ld   a, #8
        ld   (_cnt), a
    sf_loop:
        ; ---- 左ブロックのテクセル ----
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
        ; ---- 右ブロックのテクセル ----
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
        jr   nz, sf_loop
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
static u8 cnt;               /* strip_* のループ回数(asm から参照) */
static u8 nib;               /* strip_fine の左ニブル退避 */

static u8 inram;   /* 1=内側ループを hot_ram から実行して測る */

/* 1 画面ぶん(1,536B)。パターン表の並び＝帯 g(8行) → セル列 cx → 帯の中の行。 */
static void frame(void) {
    u8 g, cx;
    s16 A = (s16)(((s16)cs(ang) * scl) >> 2);     /* ブロック1つ右への u の増分(8.8) */
    s16 C = (s16)(((s16)sn(ang) * scl) >> 2);     /* 同 v */
    sA = A; sC = C;
    sB = (s16)(-C); sD = A;                        /* 1つ下へ */
    sBA = (s16)(-C - A); sDC = (s16)(A - C);
    vdp_write_addr(S3_PAT);
    /* ★掛け算を使わない: 帯の開始座標は加算で送る。SDCC の 16bit 乗算(1 帯あたり4回×192帯)だけで
       1 フレームの半分を食っていた(実測 451ms → 掛け算を外して再測)。 */
    for (g = 0; g < 6; g++) {
        u16 gu = (u16)(2048 + (s16)(g << 3) * sB);   /* 帯の左上。ここだけ 6 回の掛け算 */
        u16 gv = (u16)(2048 + (s16)(g << 3) * sD);
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

static void bench(void) {
    volatile u16 *j = (volatile u16 *)0xFC9E;
    u16 t0;
    u8 i;
    inram = 0;
    t0 = *j;
    for (i = 0; i < 64; i++) { ang++; frame(); }
    bench_tick = (u16)(*j - t0);
    inram = 1;
    t0 = *j;
    for (i = 0; i < 64; i++) { ang++; frame(); }
    g_s3_ram = (u16)(*j - t0);
    inram = 0;
    t0 = *j;
    for (i = 0; i < 64; i++) xfer_only();
    bench_xfer = (u16)(*j - t0);
}

static void s3test_init(void) {
    st = ST_FINE; ang = 0; scl = 16; fast = 0;
    bench_tick = 0; bench_xfer = 0;
    /* ★asm が 0xC600 固定で TEX を読む。hot_ram がそこを含まないビルドでは警告を出す。 */
    bad = (u8)(((u16)hot_ram > TEX_ADDR) || ((u16)hot_ram + HOT_CAP < 0xDA00));
    {   /* 内側ループを hot_ram へ写して、RAM 実行でも測れるようにする */
        u16 k;
        const u8 *sf = (const u8 *)strip_fine, *sq = (const u8 *)strip_fast;
        for (k = 0; k < 128; k++) { RAMF[k] = sf[k]; RAMQ[k] = sq[k]; }
        stripf = (void (*)(void))RAMF;
        stripq = (void (*)(void))RAMQ;
    }
    vdp_set_vscroll(0);
    vdp_set_display_page(0);
    vdp_sprite_init();
    vdp_sprite_hide_from(0);
    enter_s5();
    draw_fine();
}

static u8 s3test_update(void) {
    if (st == ST_COARSE) {
        if (g_input_edge & INP_TRIG)  { enter_s5(); draw_fine(); st = ST_FINE; return SCENE_NONE; }
        if (g_input_edge & INP_TRIGB) { bench(); enter_s5(); draw_result(); st = ST_RESULT; return SCENE_NONE; }
        if (g_input & INP_UP)    { if (scl > 4)  scl--; }
        if (g_input & INP_DOWN)  { if (scl < 64) scl++; }
        if (g_input_edge & INP_LEFT)  fast = 0;
        if (g_input_edge & INP_RIGHT) fast = 1;
        ang++;
        frame();
        return SCENE_NONE;
    }
    if (g_input_edge & INP_TRIG) {
        if (st == ST_FINE) grab_tex();         /* いま出ている絵を 64x64 に吸い出してから粗くする */
        enter_s3();
        st = ST_COARSE;
    }
    return SCENE_NONE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) s3test_init();
    else g_scene_ret = s3test_update();
}
