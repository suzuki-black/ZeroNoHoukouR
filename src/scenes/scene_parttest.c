/* scene_parttest.c — ★検証用: SCREEN3(64x48 ブロック)で「火の粉・破片」のパーティクルを
   何粒まで飛ばせるかを測る。`make clean && make PARTTEST=1` のときだけ起動シーンになる。

   ★なぜ SCREEN3 か: SCREEN5 では 1 粒ごとに VRAM の番地設定(ポート4回=約20µs)が要るので、
     描画＋消去で 1 粒 60µs＝1,000 粒で 60ms(2フレーム分)。番地設定が律速で 100 粒も出ない。
     SCREEN3 は画面全体がパターン(色)テーブル 1,536B なので、**RAM 上で画面を作って最後に
     一度だけ流す**＝粒ごとの番地設定がゼロになる。転送は実測 12.5ms(turboR)。

   ★1 粒 6 バイト: x(8.8) y(8.8) vx(s8) vy(s8)。座標の整数部がそのままブロック位置(64x48)。
     速度は 5.3 固定小数として ×8 して足す(最大 約4ブロック/フレーム)。
   ★パターン表の並びは「帯 g(8行) → セル列 cx → 帯の中の行 b」なので、ブロック(bx,by)の番地は
     上位 = >BUF + (by>>3) / 下位 = ((bx>>1)<<3) | (by&7)、ニブルは bx の偶奇。桁上がりしない。
   ★置き場は hot_ram(検証ROMではゲームの RAM 実行コードを使わないので借りてよい):
     0xC600-0xCBFF 画面バッファ(1,536B) / 0xCC00- 粒(6B×最大640)

   操作: 上下=粒数 ±64  SPACE=噴き直し  M=計測(16フレーム×2本。SCREEN5 に戻って数字を出す) */
#include "vdp.h"
#include "input.h"
#include "scene.h"
#include "raster.h"
#include "hotcode.h"

#define BUF_ADDR  0xC600          /* 1,536B。256 境界に置くこと(番地の作り方の都合) */
#define BUF       ((u8 *)BUF_ADDR)
#define PRT_ADDR  0xCC00
#define PRT       ((u8 *)PRT_ADDR)
#define PRT_MAX   576             /* 6B×576 = 3,456B。hot_ram の残りに内側ループの RAM 実行枠も置くため */
#define CODE_ADDR 0xD980          /* 内側ループを RAM 実行する場所(粒の直後) */
#define CODE      ((u8 *)CODE_ADDR)

#define S3_PAT   0x0000
#define S3_NAME  0x0800
#define S3_SATR  0x1B00
#define S3_SPAT  0x3800

#define SEA_COL  0x11             /* 背景(海)の 2 ブロック分 */

static u16 pn;                    /* いまの粒数 */
static u8  st;                    /* 0=SCREEN3 で噴いている / 1=SCREEN5 で結果表示 */
static u8  grav;                  /* 重力の位相(2フレームに1回 vy を増やす) */
u16 g_pt_jiffy;                   /* 計測: 16フレームに要した JIFFY(★エミュから読むため非 static) */
u16 g_pt_xfer;                    /* 計測: 転送だけ 16フレーム */
u16 g_pt_n;                       /* 計測時の粒数 */
u16 g_pt_frames;                  /* ★描いたフレーム数。JIFFY が止まる(ラスタ割込みのフックが
                                     VBLANK フラグを食う)ので、外(エミュ)から一定時間の増分を見て
                                     fps を出すための素直なカウンタ。 */

static u16 seed;   /* ★初期値を書くと _INITIALIZED が 0xE000 台に置かれ、rompack が
                      「bank31 コードがリンク範囲外」で弾く。初期化は pt_init で行う。 */
static u16 rnd16(void) {          /* 粒の初速用の簡単な乱数 */
    seed = (u16)(seed * 25173 + 13849);
    return seed;
}

/* ───────── 粒をまきなおす(画面中央＝艦の位置から噴き上げる) ───────── */
static void spawn(void) {
    u16 i;
    u8 *p = PRT;
    for (i = 0; i < PRT_MAX; i++) {
        u16 r = rnd16();
        p[0] = (u8)(r & 0xFF);            /* x 小数 */
        p[1] = (u8)(28 + (r >> 13));      /* x ブロック(中央付近) */
        p[2] = (u8)(r >> 8);              /* y 小数 */
        p[3] = (u8)(20 + ((r >> 11) & 3));/* y ブロック */
        r = rnd16();
        p[4] = (u8)((s8)(r & 0x3F) - 32); /* vx: ±32 */
        p[5] = (u8)((s8)((r >> 8) & 0x3F) - 56);  /* vy: 上向き中心 */
        p += 6;
    }
    grav = 0;
}

/* ───────── 画面バッファを海で埋める ───────── */
static void clear_buf(void) __naked {
    __asm
        ld   hl, #0xC600
        ld   de, #0xC601
        ld   bc, #1535
        ld   (hl), #0x11
        ldir
        ret
    __endasm;
}

/* ───────── 粒を 1 フレーム進めて画面バッファへ打つ ─────────
   IX = 粒、_pn = 粒数、_grav = 重力の位相。
   ★相対ジャンプだけで書く(RAM 実行のため)。 */
static void step_plot(void) __naked {
    __asm
        push ix               ; ★IX は SDCC のフレームポインタ。__naked で壊すと呼び元が飛ぶ(実際に踏んだ)
        ld   ix, #0xCC00
        ld   hl, (_pn)
        ld   a, h
        or   l
        jr   nz, pt_go
        pop  ix
        ret
    pt_go:
        ld   (_cnt16), hl
    pt_loop:
        ;; ---- x += vx*8 ----
        ld   e, 4(ix)
        ld   a, e
        rla
        sbc  a, a
        ld   d, a
        sla  e
        rl   d
        sla  e
        rl   d
        sla  e
        rl   d
        ld   l, 0(ix)
        ld   h, 1(ix)
        add  hl, de
        ld   0(ix), l
        ld   1(ix), h
        ld   b, h                 ; B = bx(ブロック)
        ;; ---- y += vy*8 ----
        ld   e, 5(ix)
        ld   a, e
        rla
        sbc  a, a
        ld   d, a
        sla  e
        rl   d
        sla  e
        rl   d
        sla  e
        rl   d
        ld   l, 2(ix)
        ld   h, 3(ix)
        add  hl, de
        ld   2(ix), l
        ld   3(ix), h
        ld   c, h                 ; C = by(ブロック)
        jr   pt_skip
    pt_tramp:                     ; ★jr の飛距離(±127)が足りないので折り返しは中継を経由する
        jr   pt_loop
    pt_skip:
        ;; ---- 重力(2フレームに1回 vy を +1) ----
        ld   a, (_grav)
        or   a
        jr   z, pt_nog
        inc  5(ix)
    pt_nog:
        ;; ---- 画面の外へ出たら噴き口へ戻す(★分岐先は近くに置く。jr の飛距離が足りなくなるため) ----
        ld   a, b
        cp   #64
        jr   nc, pt_resp
        ld   a, c
        cp   #48
        jr   c, pt_draw
        jr   pt_resp
    pt_tramp2:                    ; ★ループ末尾から pt_tramp まで届かないので、もう1段中継する
        jr   pt_tramp
    pt_resp:                      ; 速度を振り直して噴き口へ(粒ごとの簡易乱数)
        ld   a, 4(ix)
        add  a, a
        add  a, #37
        and  #0x3F
        sub  #32                  ; vx = -32..+31
        ld   4(ix), a
        ld   a, 5(ix)
        add  a, a
        xor  #0x5B
        and  #0x3F
        or   #0xC0                ; vy = -64..-1(上向き)
        ld   5(ix), a
        ld   1(ix), #32           ; 噴き口(画面中央やや下)
        ld   3(ix), #40
        jr   pt_next
    pt_draw:
        ;; ---- 番地: 上位 = 0xC6 + (by>>3) / 下位 = ((bx>>1)<<3) | (by&7) ----
        ld   a, c
        rrca
        rrca
        rrca
        and  #0x0F
        add  a, #0xC6
        ld   d, a
        ld   a, b
        srl  a
        rlca
        rlca
        rlca
        and  #0xF8
        ld   e, a
        ld   a, c
        and  #7
        add  a, e
        ld   e, a
        ;; ---- 色: 粒ごとに 11(赤)/12(橙)/15(白)/14(淡灰) ----
        ld   a, 4(ix)
        and  #3
        add  a, #11
        cp   #14
        jr   c, pt_col
        add  a, #1                ; 14 → 15(白)
    pt_col:
        ld   c, a
        ;; ---- ニブルへ書く(もう片方は残す) ----
        ld   a, (de)
        bit  0, b
        jr   nz, pt_lo
        and  #0x0F
        ld   b, a
        ld   a, c
        rlca
        rlca
        rlca
        rlca
        or   b
        jr   pt_put
    pt_lo:
        and  #0xF0
        or   c
    pt_put:
        ld   (de), a
    pt_next:
        ld   bc, #6
        add  ix, bc
        ld   hl, (_cnt16)
        dec  hl
        ld   (_cnt16), hl
        ld   a, h
        or   l
        jr   nz, pt_tramp2
        pop  ix
        ret
    __endasm;
}
/* step_plot の終わり(写す長さを測るための目印)。★この関数は step_plot の直後に置くこと。 */
static void step_plot_end(void) __naked { __asm ret __endasm; }
static void (*stepr)(void);   /* hot_ram へ写した step_plot */
static u16 cnt16;   /* step_plot のループ回数(asm から参照) */

/* ───────── 画面バッファを VRAM のパターン表へ ───────── */
static void blast(void) __naked {
    __asm
        ld   a, #0
        out  (0x99), a
        ld   a, #0x80 + 14
        out  (0x99), a
        ld   a, #0
        out  (0x99), a
        ld   a, #0x40
        out  (0x99), a
        ld   hl, #0xC600
        ld   bc, #1536
    pb_loop:
        ld   a, (hl)
        out  (0x98), a
        inc  hl
        dec  bc
        ld   a, b
        or   c
        jr   nz, pb_loop
        ret
    __endasm;
}

static void enter_s3(void) {
    u8 y, x, i;
    raster_off();
    __asm
        ld   a, #3
        ld   (0xFCAF), a
        call 0x005F            ; CHGMOD (MULTI COLOUR)
    __endasm;
    vdp_wreg(25, 0x00);
    vdp_wreg(2, S3_NAME / 0x400);
    vdp_wreg(4, S3_PAT / 0x800);
    vdp_wreg(5, S3_SATR / 0x80);
    vdp_wreg(6, S3_SPAT / 0x800);
    vdp_palette_game();
    vdp_write_addr(S3_NAME);
    for (y = 0; y < 24; y++)
        for (x = 0; x < 32; x++) vdp_data((u8)(((y >> 2) << 5) + x));
    vdp_write_addr(S3_SPAT);
    for (i = 0; i < 8; i++) vdp_data(0x00);
    vdp_write_addr(S3_SATR);
    vdp_data(208);
    vdp_wreg(1, 0x68);         /* ★bit3=M2=1(MULTI COLOUR)。ここを落とすと画面が一様になる */
}

static void num5(u8 x, u8 y, u16 v) {
    char s[6];
    s[0] = (char)('0' + v / 10000);      s[1] = (char)('0' + (v / 1000) % 10);
    s[2] = (char)('0' + (v / 100) % 10); s[3] = (char)('0' + (v / 10) % 10);
    s[4] = (char)('0' + v % 10);         s[5] = 0;
    vdp_text(x, y, 15, 1, s);
}

static void draw_result(void) {
    /* 16 フレームの JIFFY → fps x10 = 16*600/jiffy */
    u16 f1 = (u16)(g_pt_jiffy ? (u16)((16UL * 600UL) / g_pt_jiffy) : 0);
    u16 f2 = (u16)(g_pt_xfer  ? (u16)((16UL * 600UL) / g_pt_xfer)  : 0);
    vdp_screen5();
    vdp_palette_game();
    vdp_fill(0, 0, 256, 212, 1);
    vdp_text(2, 10, 11, 1, "SCREEN3 PARTICLE BENCH");
    vdp_text(2, 30, 15, 1, "PARTICLES");      num5(130, 30, g_pt_n);
    vdp_text(2, 50, 15, 1, "16 FRAME JIFFY"); num5(130, 50, g_pt_jiffy);
    vdp_text(2, 62, 15, 1, "FPS X10");        num5(130, 62, f1);
    vdp_text(2, 82, 15, 1, "XFER ONLY JIFFY");num5(130, 82, g_pt_xfer);
    vdp_text(2, 94, 15, 1, "FPS X10");        num5(130, 94, f2);
    vdp_text(2, 118, 14, 1, "1536 BYTE PER FRAME");
    vdp_text(2, 130, 14, 1, "6 BYTE PER PARTICLE");
    vdp_text(2, 154, 12, 1, "SPACE:BACK  UD:COUNT");
}

static void bench(void) {
    volatile u16 *j = (volatile u16 *)0xFC9E;
    u16 t0;
    u8 i;
    g_pt_n = pn;
    t0 = *j;
    for (i = 0; i < 16; i++) { clear_buf(); stepr(); blast(); grav ^= 1; }   /* 計測には assert を入れない */
    g_pt_jiffy = (u16)(*j - t0);
    t0 = *j;
    for (i = 0; i < 16; i++) blast();
    g_pt_xfer = (u16)(*j - t0);
}

static void pt_init(void) {
    /* ★内側ループは hot_ram へ写して RAM 実行する。ROM 実行では 3.8 倍遅く、
       本番(オーバレイ＋RAM実行)の数字にならない。 */
    const u8 *sp = (const u8 *)step_plot;
    seed = 0x1234;
    u16 len = (u16)((const u8 *)step_plot_end - sp), k;
    stepr = step_plot;
    if (len <= 512) {
        for (k = 0; k < len; k++) CODE[k] = sp[k];
        stepr = (void (*)(void))CODE;
    }
    pn = 320; st = 0;
    g_pt_jiffy = 0; g_pt_xfer = 0; g_pt_n = 0; g_pt_frames = 0;
    vdp_set_vscroll(0);
    vdp_set_display_page(0);
    vdp_sprite_init();
    vdp_sprite_hide_from(0);
    spawn();
    enter_s3();
}

/* ★毎フレーム モードレジスタを張り直す。init で設定しただけでは SCREEN1・画面OFF に
   戻されていた(R#0=0 / R#1=0x22 / R#2=6)。計測ROMなのでコストの小さい安全側で押し切る。 */
static void assert_s3(void) {
    vdp_wreg(0, 0x00);
    vdp_wreg(1, 0x68);   /* 画面ON / VBLANK割込みON / M2=1(MULTI COLOUR) / スプライト8x8 */
    vdp_wreg(2, S3_NAME / 0x400);
    vdp_wreg(4, S3_PAT / 0x800);
}

static u8 pt_update(void) {
    if (st == 1) {                                  /* 結果表示 */
        if (g_input_edge & INP_TRIG) { enter_s3(); spawn(); st = 0; }
        return SCENE_NONE;
    }
    if (g_input_edge & INP_TRIG)  { spawn(); return SCENE_NONE; }
    if (g_input_edge & INP_TRIGB) { bench(); draw_result(); st = 1; return SCENE_NONE; }
    if ((g_input_edge & INP_UP)   && pn + 64 <= PRT_MAX) pn = (u16)(pn + 64);
    if ((g_input_edge & INP_DOWN) && pn > 64)            pn = (u16)(pn - 64);
    assert_s3();
    clear_buf();
    stepr();
    blast();
    grav ^= 1;
    g_pt_frames++;
    return SCENE_NONE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) pt_init();
    else g_scene_ret = pt_update();
}
