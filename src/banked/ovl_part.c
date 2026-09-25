/* ovl_part.c — 1面の轟沈: 火の粉と破片(パーティクル)。撃沈オーバレイ(OVL7)に同居する。

   ★画面は SCREEN5 のまま(精緻)。粒だけを背景へ差分描きする。
     いったん SCREEN3(1ブロック 4x4 ドット)で 400 粒まで出す版を作ったが、
     「なぜ画面が粗くなるのか」＝火の粉に粗くする理由は無い(ユーザー指摘)。精緻なまま行く。
   ★turboR 実測(WebMSX): 1 粒 2x2 ドットで約 200 粒 / 2x1 ドットで約 320 粒が 30fps。
     粒ごとに VRAM の番地設定(ポート4回=約20µs)が要るのが効く。ここは 2x1 で 260 粒。

   ★演出: 噴き口が**艦の上の中心から下の中心へ**下りていき、通ったところの艦は海に戻る
     (海テンプレートを VDP コピーで貼る)。粒は重力で落ち、**海面(噴き口より下)で消える**。
   ★粒の消去は「海テンプレートの同じ位置の色」で塗り戻す。VRAM を読み戻すと番地設定が倍に
     なるので、テンプレート 16 行を RAM に写しておき、そこから引く。
   ★RAM は hot_ram(演出中だけ借りる。終わったら常駐が hot_load() で戻す):
     0xC600 海テンプレート 16行x128B = 2,048B / 0xCE00 粒 6B×260 / 0xD500 内側ループ
   ★粒 1 個 6B: x(8.8) y(8.8) vx(s8) vy(s8)。y_hi=0xFF は死んだ粒。 */
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "overlay.h"
#include "scroll.h"

#define TMPL    ((u8 *)0xC600)     /* 海テンプレート 16 行(1 行 128B)。256 境界に置くこと */
#define PRT     ((u8 *)0xCE00)
#define PRT_N   260
#define CODE    ((u8 *)0xD500)

#define SEQ_FALL 70                /* 噴き口が上から下まで下りるフレーム数 */
#define SEQ_END  100               /* 全体の尺 */

static u16 seed;
static u8  emit_y;                 /* 噴き口の画面Y(ドット) */
static u16 cnt16;
static u8  vscroll_now;            /* asm から見る縦スクロール(g_vscroll の写し) */

__sfr __at(0x98) PDAT;
__sfr __at(0x99) PCTL;

static u16 rnd16(void) { seed = (u16)(seed * 25173 + 13849); return seed; }

/* ★VRAM の 行512 以降は 16bit 番地を超える。R#14 は A14-A16(16KB 単位)なので row>>7。 */
static void read_row(u16 row) {
    u8  a16 = (u8)(row >> 7);
    u16 lo  = (u16)((row & 127) << 7);
    __asm di __endasm;
    PCTL = (u8)(a16 & 7);   PCTL = 0x80 | 14;
    PCTL = (u8)(lo & 0xFF); PCTL = (u8)((lo >> 8) & 0x3F);
    __asm ei __endasm;
}

/* 海テンプレート(VRAM 512行〜の 16 行)を RAM へ。粒の消去に使う。 */
static void grab_tmpl(void) {
    u8 r;
    u16 i;
    for (r = 0; r < 16; r++) {
        u8 *d = TMPL + ((u16)r << 7);
        read_row((u16)(SC_SEATMPL_Y + r));
        for (i = 0; i < 128; i++) d[i] = PDAT;
    }
}

/* ───────── 粒を 1 フレーム進めて SCREEN5 の背景へ差分描き(asm・RAM実行) ─────────
   ★番地: リング行 = 縦スクロール + 画面Y。表示リングは page1(VRAM 行 256〜511)なので
     上位 = 0x80 + (row>>1) / 下位 = ((row&1)<<7) | (x>>1)。桁上がりしない。
   ★消去は TMPL[(row & 15)*128 + (x>>1)] の色で塗り戻す。
   ★__naked で IX を使うので push/pop。相対ジャンプだけ(RAM 実行のため)。 */
static void step_plot(void) __naked {
    __asm
        push ix
        ld   ix, #0xCE00
        ld   hl, #260
        ld   (_cnt16), hl
        jr   pq_first
    pq_loop:
        ld   bc, #6
        add  ix, bc
    pq_first:
        ld   hl, (_cnt16)
        dec  hl
        ld   (_cnt16), hl
        ld   a, h
        or   l
        jr   nz, pq_body
        pop  ix
        ret
    pq_body:
        ld   a, 3(ix)             ; y_hi。0xFF は死んだ粒
        inc  a
        jr   z, pq_loop
        ;; ---- 古い位置を海の色で塗り戻す ----
        call pq_addr
        ld   a, (_vscroll_now)
        add  a, 3(ix)
        and  #15
        add  a, #0xC6             ; TMPL の行(1 行 128B → 上位 = 0xC6 + (row&15))
        ld   h, a
        ld   a, 1(ix)
        srl  a
        ld   l, a
        ld   a, (hl)
        out  (0x98), a
        jr   pq_move
    pq_back:
        jr   pq_loop
    pq_move:
        ;; ---- x += vx*2 / y += vy*2 / vy += 1(重力) ----
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
        ;; ---- 噴き口より下(海面)/画面外なら消す ----
        ld   c, 3(ix)
        ld   a, (_emit_y)
        add  a, #6
        cp   c
        jr   c, pq_kill
        ld   a, c
        cp   #200
        jr   nc, pq_kill
        ld   a, 1(ix)
        cp   #250
        jr   nc, pq_kill
        ;; ---- 新しい位置に打つ ----
        call pq_addr
        ld   a, 4(ix)
        and  #3
        jr   nz, pq_spark
        ld   a, #0x44             ; 破片(中灰)
        out  (0x98), a
        jr   pq_back
    pq_spark:
        dec  a
        jr   z, pq_s1
        dec  a
        jr   z, pq_s2
        ld   a, #0xFF             ; 白
        out  (0x98), a
        jr   pq_back
    pq_s1:
        ld   a, #0xCC             ; 橙
        out  (0x98), a
        jr   pq_back
    pq_s2:
        ld   a, #0xBB             ; 赤
        out  (0x98), a
        jr   pq_back
    pq_kill:
        ld   3(ix), #0xFF
        jr   pq_back
        ;; --- 粒(IX)の位置から VRAM 書込み番地を設定する(page1 = 行256〜) ---
    pq_addr:
        ld   a, (_vscroll_now)
        add  a, 3(ix)             ; リング行
        ld   b, a
        srl  a
        add  a, #0x80             ; 上位(page1)
        ld   d, a
        ld   a, b
        and  #1
        rrca                      ; (row&1)<<7
        ld   e, a
        ld   a, 1(ix)
        srl  a
        add  a, e
        ld   e, a
        di
        xor  a
        out  (0x99), a
        ld   a, #0x80 + 14
        out  (0x99), a
        ld   a, e
        out  (0x99), a
        ld   a, d
        or   #0x40
        out  (0x99), a
        ei
        ret
    __endasm;
}
static void step_plot_end(void) __naked { __asm ret __endasm; }
static void (*stepr)(void);

/* 噴き口から粒を出す */
static void emit(u8 n) {
    u8 *p = PRT;
    u16 i;
    for (i = 0; i < PRT_N && n; i++, p += 6) {
        if (p[3] != 0xFF) continue;
        {
            u16 r = rnd16();
            p[0] = (u8)(r & 0xFF);
            p[1] = (u8)(116 + ((r >> 8) & 31));        /* x(ドット): 艦の中心付近 */
            p[2] = (u8)(r >> 5);
            p[3] = emit_y;
            r = rnd16();
            p[4] = (u8)((s8)((r & 0x7F) - 64));        /* 横 ±64 */
            p[5] = (u8)((s8)(((r >> 8) & 0x3F) - 60)); /* 上向き */
        }
        n--;
    }
}

/* 噴き口が通ったところの艦を海に戻す(海テンプレートを 1 行ずつ貼る) */
static void erase_rows(u8 y0, u8 n) {
    u8 i;
    for (i = 0; i < n; i++) {
        u16 ring = (u16)(256 + (u8)(vscroll_now + y0 + i));
        vdp_copy(0, (u16)(SC_SEATMPL_Y + (u8)(ring & 15)), 0, ring, 256, 1);
    }
}

/* 撃沈の直後に常駐から呼ばれる(OVL_SLOT_PART)。戻るまで数秒ここに居る。 */
void ovl_part(void) {
    u16 k, t;
    const u8 *sp = (const u8 *)step_plot;
    u16 len = (u16)((const u8 *)step_plot_end - sp);
    if (len > 640) return;
    seed = 0x2468;
    vscroll_now = g_vscroll;
    grab_tmpl();
    for (k = 0; k < PRT_N; k++) PRT[k * 6 + 3] = 0xFF;
    for (k = 0; k < len; k++) CODE[k] = sp[k];
    stepr = (void (*)(void))CODE;
    emit_y = 0;
    raster_off();                       /* 分割は止める(演出中は帯を使わない) */
    for (t = 0; t < SEQ_END; t++) {
        if (t < SEQ_FALL) {
            u8 ny = (u8)(((u16)t * 196) / SEQ_FALL);   /* 噴き口: 上 → 下(画面Y 0..196) */
            if (ny > emit_y) { erase_rows(emit_y, (u8)(ny - emit_y)); emit_y = ny; }
            emit(9);
        }
        stepr();
        vdp_wait_frame();
    }
}
