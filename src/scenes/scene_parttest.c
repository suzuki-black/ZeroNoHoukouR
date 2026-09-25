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
        p[0] = (u8)(r & 0xFF);
        p[1] = (u8)(120 + ((r >> 8) & 15));   /* x(ドット): 画面中央付近 */
        p[2] = (u8)(r >> 5);
        p[3] = (u8)(150 + ((r >> 12) & 7));   /* y(ドット): 画面下寄り */
        r = rnd16();
        p[4] = (u8)((s8)((r & 0x3F) - 32));
        p[5] = (u8)((s8)(((r >> 8) & 0x3F) - 58));
        p += 6;
    }
    grav = 0;
}


/* ───────── SCREEN5(精緻なまま)で粒を打つ ─────────
   ★1 粒 = 2x2 ドット(1 バイト × 2 行)。VRAM 番地は y*128 + (x>>1) で、
     上位 = y>>1 / 下位 = ((y&1)<<7) | (x>>1)。桁上がりしない。
   ★消去は「海の色で塗り戻す」。演出中、粒が飛ぶのは艦を消した後の海の上だけなので、
     背景を読み戻さなくてよい(読み戻すと番地設定が倍になる)。
   ★番地設定はポート4回(約20µs)。これが粒ごとに要るので SCREEN5 では粒数が効く。
   ★相対ジャンプだけで書く(RAM 実行のため)。 */
static void step_plot(void) __naked {
    __asm
        push ix
        ld   ix, #0xCC00
        ld   hl, (_pn)
        ld   (_cnt16), hl
        jr   p5_first
    p5_loop:
        ld   bc, #6
        add  ix, bc
    p5_first:
        ld   hl, (_cnt16)
        dec  hl
        ld   (_cnt16), hl
        ld   a, h
        or   l
        jr   nz, p5_body
        pop  ix
        ret
    p5_body:
        ;; ---- 古い位置を海で塗り戻す(2 行) ----
        ld   a, 3(ix)             ; y
        cp   #210
        jr   nc, p5_move          ; 画面外なら消さない
        ld   c, 1(ix)             ; x
        call p5_addr
        ld   a, #0x11
        out  (0x98), a
    p5_move:
        ;; ---- x += vx*2 / y += vy*2, vy += 1 ----
        ld   e, 4(ix)
        ld   a, e
        rla
        sbc  a, a
        ld   d, a
        sla  e
        rl   d
        ld   l, 0(ix)
        ld   h, 1(ix)
        add  hl, de
        ld   0(ix), l
        ld   1(ix), h
        ld   e, 5(ix)
        ld   a, e
        rla
        sbc  a, a
        ld   d, a
        sla  e
        rl   d
        ld   l, 2(ix)
        ld   h, 3(ix)
        add  hl, de
        ld   2(ix), l
        ld   3(ix), h
        inc  5(ix)
        jr   p5_chk
    p5_back:
        jr   p5_loop
    p5_chk:
        ;; ---- 画面外なら噴き口へ戻す(計測用。本番では消す) ----
        ld   a, 3(ix)
        cp   #200
        jr   c, p5_draw
        ld   1(ix), #128
        ld   3(ix), #150
        ld   a, 4(ix)
        add  a, a
        add  a, #37
        and  #0x3F
        sub  #32
        ld   4(ix), a
        ld   a, 5(ix)
        add  a, a
        xor  #0x5B
        and  #0x3F
        or   #0xC0
        ld   5(ix), a
        jr   p5_back
    p5_draw:
        ;; ---- 新しい位置に打つ(2 行) ----
        ld   a, 3(ix)
        ld   c, 1(ix)
        call p5_addr
        ld   a, #0xCC
        out  (0x98), a
        jr   p5_back
        ;; --- A=y, C=x → VRAM 書込み番地を設定する ---
    p5_addr:
        srl  a                    ; y>>1 → 上位
        ld   b, a
        ld   a, 3(ix)
        and  #1
        rrca                      ; (y&1)<<7
        ld   d, a
        ld   a, c
        srl  a                    ; x>>1
        add  a, d
        ld   e, a                 ; 下位
        di
        xor  a
        out  (0x99), a
        ld   a, #0x80 + 14
        out  (0x99), a
        ld   a, e
        out  (0x99), a
        ld   a, b
        or   #0x40
        out  (0x99), a
        ei
        ret
    __endasm;
}
static void step_plot_end(void) __naked { __asm ret __endasm; }
static void (*stepr)(void);
static u16 cnt16;



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
    vdp_fill(0, 0, 256, 212, 1);
    vdp_text(2, 10, 11, 1, "SCREEN5 PARTICLE BENCH");
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
    for (i = 0; i < 16; i++) { stepr(); grav ^= 1; }
    g_pt_jiffy = (u16)(*j - t0);
    g_pt_xfer = 0;
}

static void draw_bg5(void) {
    vdp_fill(0, 0, 256, 212, 1);          /* 海(単色)。計測用の背景 */
    vdp_text(2, 2, 15, 1, "SCREEN5 PARTICLE TEST");
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
    draw_bg5();
}


static u8 pt_update(void) {
    if (st == 1) {                                  /* 結果表示 */
        if (g_input_edge & INP_TRIG) { draw_bg5(); spawn(); st = 0; }
        return SCENE_NONE;
    }
    if (g_input_edge & INP_TRIG)  { draw_bg5(); spawn(); return SCENE_NONE; }
    if (g_input_edge & INP_TRIGB) { bench(); draw_result(); st = 1; return SCENE_NONE; }
    if ((g_input_edge & INP_UP)   && pn + 64 <= PRT_MAX) pn = (u16)(pn + 64);
    if ((g_input_edge & INP_DOWN) && pn > 64)            pn = (u16)(pn - 64);
    stepr();
    grav ^= 1;
    g_pt_frames++;
    return SCENE_NONE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) pt_init();
    else g_scene_ret = pt_update();
}
