/* ovl_part.c — 1面の轟沈: 火の粉と破片(パーティクル)。撃沈オーバレイ(OVL7)に同居する。

   ★絵の作り: SCREEN3(マルチカラー 64x48 ブロック=1ブロック 4x4 ドット)。画面全体が
     パターン(色)テーブル 1,536B なので、**RAM 上で 1 画面を組み立てて一度だけ流す**。
     粒ごとの VRAM 番地設定(ポート4回=約20µs)が要らないのが肝。SCREEN5 だと 1 粒 60µs で
     100 粒も出ない。WebMSX の turboR 実測で **464 粒まで 30fps**(粒 0 なら 60fps)。

   ★演出: 噴き口が**艦の上の中心から下の中心へ**下りていき、通ったところの艦は消えて海に戻る。
     粒は重力で落ち、**海面(画面下端)に着いたら消える**。

   ★RAM(演出中だけ hot_ram と開始カードの枠を借りる。終わったら常駐が hot_load() で戻す):
     0xC600 背景 1,536B(パターン表と同じ並び) / 0xCC00 画面 1,536B / 0xD200 粒 6B×400
     0xE100 内側ループの RAM 実行先(ROM 実行では 3.8 倍遅い)
   ★粒 1 個 6B: x(8.8) y(8.8) vx(s8) vy(s8)。座標の整数部がそのままブロック位置。死んだ粒は y_hi=0xFF。 */
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "overlay.h"
#include "scroll.h"

#define BG      ((u8 *)0xC600)     /* 背景(艦)。パターン表と同じ並び */
#define BUF     ((u8 *)0xCC00)     /* 組み立て中の画面。256 境界に置くこと */
#define PRT     ((u8 *)0xD200)
#define PRT_N   400                /* 6B×400 = 2,400B。30fps を保てる粒数(実測 464 まで) */
#define CODE    ((u8 *)0xE100)     /* 開始カードの枠(面の開始前にしか使わない)を借りる */

#define S3_PAT   0x0000
#define S3_NAME  0x0800
#define S3_SATR  0x1B00
#define S3_SPAT  0x3800

#define SEQ_FALL 66                /* 噴き口が上から下まで下りるフレーム数 */
#define SEQ_END  96                /* 全体の尺(残りは落ちきるまで) */

static u8  seaq[16];               /* 海の見本(4x4 ブロック分。背景を消すのに使う) */
static u16 seed;
static u8  emit_y;                 /* 噴き口のブロック行 */
static u8  cnt;                    /* asm のループ用 */
static u16 cnt16;

__sfr __at(0x98) PDAT;
__sfr __at(0x99) PCTL;

/* ★VRAM の 行512 以降は 16bit 番地を超える。R#14 は A14-A16(16KB 単位)なので row>>7。 */
static void read_row(u16 row) {
    u8  a16 = (u8)(row >> 7);
    u16 lo  = (u16)((row & 127) << 7);
    __asm di __endasm;
    PCTL = (u8)(a16 & 7);   PCTL = 0x80 | 14;
    PCTL = (u8)(lo & 0xFF); PCTL = (u8)((lo >> 8) & 0x3F);
    __asm ei __endasm;
}

static u16 rnd16(void) { seed = (u16)(seed * 25173 + 13849); return seed; }

/* ───────── パターン表の並びでの番地: (bx,by) → BG/BUF + (by>>3)*256 + ((bx>>1)<<3) + (by&7) ─────────
   1 バイトに横 2 ブロック(上位ニブル=左)。 */
static u16 blkoff(u8 bx, u8 by) {
    return (u16)(((u16)(by >> 3) << 8) | (u16)(((bx >> 1) << 3) | (by & 7)));
}

/* ───────── いま画面に出ている絵(艦＋海＋炎)を背景として取り込む ─────────
   4x4 ドットを 1 ブロックにする。縦スクロール(R#23=g_vscroll)ぶんずらして読む。 */
static void grab_bg(void) {
    u8 by, bx;
    for (by = 0; by < 48; by++) {
        u8 row = (u8)(g_vscroll + (u8)(by << 2));
        u8 *d;
        /* ★表示リングは page1(VRAM 行 256〜511)。page0 には開始カードの絵が残っており、
           そこを読むと「BISMARCK」の文字が背景に出る(実際に踏んだ)。
           行 256+row のバイト番地 = 0x8000 + row*128 で 16bit に収まる。 */
        vdp_read_addr((u16)(0x8000 + ((u16)row << 7)));
        d = BG + blkoff(0, by);
        for (bx = 0; bx < 64; bx += 2) {
            u8 a = vdp_read_data(); (void)vdp_read_data();
            {
                u8 b = vdp_read_data(); (void)vdp_read_data();
                *d = (u8)((a & 0xF0) | (b >> 4));
            }
            d += 8;                                /* 横 2 ブロック = 1 バイト。次は 8 バイト先 */
        }
    }
    /* 海の見本: 海テンプレート(VRAM 512行)の左 16 ドットから 4x4 ブロック分 */
    {
        u8 i, j;
        for (i = 0; i < 4; i++) {
            read_row((u16)(SC_SEATMPL_Y + (u16)(i << 2)));
            for (j = 0; j < 4; j++) {
                u8 a = PDAT; (void)PDAT;
                { u8 b = PDAT; (void)PDAT; seaq[(i << 2) + j] = (u8)((a & 0xF0) | (b >> 4)); }
            }
        }
    }
}

/* 噴き口が通った行の艦を消す(海に戻す) */
static void erase_row(u8 by) {
    u8 *d = BG + blkoff(0, by);
    u8 bx;
    for (bx = 0; bx < 32; bx++) { *d = seaq[((by & 3) << 2) | (bx & 3)]; d += 8; }
}

/* ───────── 粒を 1 フレーム進めて画面へ打つ(asm・RAM実行) ─────────
   ★__naked で IX を使うときは push/pop すること(SDCC のフレームポインタ)。
   ★相対ジャンプだけで書く(RAM へ写して実行するため)。飛距離が足りないので中継を 2 段入れる。 */
static void step_plot(void) __naked {
    __asm
        push ix
        ld   ix, #0xD200
        ld   hl, #400
        ld   (_cnt16), hl
        jr   pp_first
    pp_loop:                      ; ★次の粒へ進む処理をループの先頭に置く。こうすると
        ld   bc, #6               ;   「死んだ粒を飛ばす」分岐が近くなり jr が届く
        add  ix, bc
    pp_first:
        ld   hl, (_cnt16)
        dec  hl
        ld   (_cnt16), hl
        ld   a, h
        or   l
        jr   nz, pp_body
        pop  ix
        ret
    pp_body:
        ld   a, 3(ix)             ; y_hi。0xFF は死んだ粒
        inc  a
        jr   z, pp_loop
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
        ld   b, h                 ; B = bx
        ;; ---- y += vy*8, vy += 1(重力) ----
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
        ld   c, h                 ; C = by
        inc  5(ix)
        jr   pp_bounds
    pp_back:                      ; ★末尾からループ先頭へ戻る中継(jr の飛距離 ±127)
        jr   pp_loop
    pp_bounds:
        ;; ---- 画面外/海面下なら消す(y_hi = 0xFF) ----
        ld   a, b
        cp   #64
        jr   nc, pp_kill
        ld   a, c
        cp   #48
        jr   c, pp_draw
    pp_kill:
        ld   3(ix), #0xFF
        jr   pp_back
    pp_draw:
        ;; ---- 番地: 上位 = 0xCC + (by>>3) / 下位 = ((bx>>1)<<3) | (by&7) ----
        ld   a, c
        rrca
        rrca
        rrca
        and  #0x0F
        add  a, #0xCC
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
        ;; ---- 色: 火の粉(11 赤/12 橙/15 白)と破片(4 中灰) ----
        ld   a, 4(ix)
        and  #3
        jr   nz, pp_c1
        ld   a, #4
        jr   pp_col
    pp_c1:
        add  a, #10
        cp   #13
        jr   c, pp_col
        ld   a, #15
    pp_col:
        ld   c, a
        ld   a, (de)
        bit  0, b
        jr   nz, pp_lo
        and  #0x0F
        ld   b, a
        ld   a, c
        rlca
        rlca
        rlca
        rlca
        or   b
        jr   pp_put
    pp_lo:
        and  #0xF0
        or   c
    pp_put:
        ld   (de), a
        jr   pp_back
    __endasm;
}
static void step_plot_end(void) __naked { __asm ret __endasm; }
static void (*stepr)(void);

/* 背景を画面へ写す(1,536B) */
static void copy_bg(void) __naked {
    __asm
        ld   hl, #0xC600
        ld   de, #0xCC00
        ld   bc, #1536
        ldir
        ret
    __endasm;
}

/* 画面を VRAM のパターン表へ(1,536B)。★OTIR は使わない(R800 では VDP に速すぎる) */
static void blast(void) __naked {
    __asm
        di
        xor  a
        out  (0x99), a
        ld   a, #0x80 + 14
        out  (0x99), a
        xor  a
        out  (0x99), a
        ld   a, #0x40
        out  (0x99), a
        ei
        ld   hl, #0xCC00
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

/* 噴き口から粒を出す(1 フレームぶん) */
static void emit(u8 n) {
    u8 *p = PRT;
    u16 i;
    for (i = 0; i < PRT_N && n; i++, p += 6) {
        if (p[3] != 0xFF) continue;                 /* 生きている枠は飛ばす */
        {
            u16 r = rnd16();
            p[0] = (u8)(r & 0xFF);
            p[1] = (u8)(30 + ((r >> 8) & 3));       /* 艦の中心付近(横) */
            p[2] = (u8)(r >> 5);
            p[3] = emit_y;
            r = rnd16();
            p[4] = (u8)((s8)((r & 0x3F) - 32));     /* 横 ±32 */
            p[5] = (u8)((s8)(((r >> 8) & 0x3F) - 58)); /* 上向き */
        }
        n--;
    }
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
    vdp_wreg(0, 0x00);
    vdp_wreg(1, 0x68);         /* ★bit3=M2=1(MULTI COLOUR)。落とすと画面が一様になる */
}

/* 撃沈の直後に常駐から呼ばれる(OVL_SLOT_PART)。戻るまで数秒ここに居る。 */
void ovl_part(void) {
    u16 k, t;
    const u8 *sp = (const u8 *)step_plot;
    u16 len = (u16)((const u8 *)step_plot_end - sp);
    if (len > 512) return;
    seed = 0x2468;
    grab_bg();
    for (k = 0; k < PRT_N; k++) PRT[k * 6 + 3] = 0xFF;   /* 全部「死んでいる」から始める */
    for (k = 0; k < len; k++) CODE[k] = sp[k];
    stepr = (void (*)(void))CODE;
    emit_y = 0;
    enter_s3();
    for (t = 0; t < SEQ_END; t++) {
        if (t < SEQ_FALL) {
            u8 ny = (u8)((t * 48) / SEQ_FALL);           /* 噴き口: 上の中心 → 下の中心 */
            while (emit_y < ny) { erase_row(emit_y); emit_y++; }
            emit(14);                                    /* 1 フレームあたりの粒数 */
        }
        copy_bg();
        stepr();
        blast();
        vdp_wait_frame();
    }
}
