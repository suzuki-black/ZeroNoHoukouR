/* ovl_part.c — 1面の轟沈: 火の粉と破片(パーティクル)。撃沈オーバレイ(OVL7)に同居する。

   ★画面は SCREEN5 のまま(精緻)。粒だけを背景へ差分描きする。
     SCREEN3(1ブロック 4x4 ドット)で 400 粒出す版も作ったが、火の粉に画面を粗くする理由は無い。
   ★最初の版は「2x1 ドットの粒を 260 個」だったが、細かすぎて**艦が消えただけに見えた**
     (ユーザー指摘)。粒を 2x2 ドットに太らせ、噴出を山なりの塊にし、白フラッシュと
     ぎざぎざの崩れ方を足して「爆ぜて散る」絵にする。

   ★速度の勘所: 粒 1 個につき VRAM の番地設定(ポート4回=約20µs)が要る。2x2 は 2 行なので
     消去 2 回＋描画 2 回 = 4 回。turboR 実測で **2x2 は約 200 粒 / 2x1 は約 320 粒が 30fps**。
   ★消去は「海テンプレートの同じ位置の色」で塗り戻す(VRAM を読み戻すと番地設定が倍になる)。
     テンプレート 16 行(2KB)を RAM へ写して引く。
   ★RAM は hot_ram(演出中だけ借りる。終わったら常駐が hot_load() で戻す):
     0xC600 海テンプレート 2,048B / 0xCE00 粒 6B×200 / 0xD400 内側ループ
   ★粒 1 個 6B: x(8.8) y(8.8) vx(s8) vy(s8)。y_hi=0xFF は死んだ粒。
   ★y は「画面Y + 48」で持つ。画面の上へ飛び出した粒が 0 を下回って回り込み、
     「即死して撒いた場所に少し残るだけ」になっていたため(実際に踏んだ)。48 ドットぶん上に
     居られるようにして、重力で戻ってくるまで生かす。 */
#define YBIAS 48
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "overlay.h"
#include "scroll.h"

#define TMPL    ((u8 *)0xC600)     /* 海テンプレート 16 行(1 行 128B)。256 境界に置くこと */
#define PRT     ((u8 *)0xCE00)
#define PRT_N   200                /* 2x2 ドットで 30fps に収まる数(実測) */
#define PRT_SZ  8                  /* x(2) y(2) vx vy px py。px=0xFF は「前回描いていない」 */
#define CODE    ((u8 *)0xD400)

#define SEQ_FALL 64                /* 噴き口が上から下まで下りるフレーム数 */
#define SEQ_END  104               /* 全体の尺(約 3.5 秒) */

static u16 seed;
static u8  emit_y;                 /* 噴き口の画面Y(ドット) */
static u16 cnt16;
static u8  vscroll_now;            /* asm から見る縦スクロール(g_vscroll の写し) */
static u8  pcol;                   /* いま描く粒の色(2 ドット分) */

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
   ★番地: リング行 = 縦スクロール + 画面Y + 行オフセット。表示リングは page1(VRAM 行 256〜)
     なので 上位 = 0x80 + (row>>1) / 下位 = ((row&1)<<7) | (x>>1)。桁上がりしない。
   ★__naked で IX を使うので push/pop。相対ジャンプだけ(RAM 実行のため)。 */
static void step_plot(void) __naked {
    __asm
        push ix
        ld   ix, #0xCE00
        ld   hl, #200
        ld   (_cnt16), hl
        jr   pq_first
    pq_loop:
        ld   bc, #8
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
        ;; ---- 前回描いた場所を消す(描いていなければ px=0xFF) ----
        ld   a, 6(ix)
        inc  a
        jr   z, pq_move
        jr   pq_er
    pq_back0:                     ; ★中継(jr の飛距離 ±127)
        jr   pq_loop
    pq_er:
        ld   c, 6(ix)             ; px
        ld   a, 7(ix)             ; py(画面行)
        call pq_erase
        ld   c, 6(ix)
        ld   a, 7(ix)
        inc  a
        call pq_erase
    pq_move:
        ;; ---- x += vx*16 / y += vy*16 / vy += 2(重力) ----
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
        ld   a, 5(ix)
        add  a, #2
        ld   5(ix), a
        jr   pq_chk
    pq_back:
        jr   pq_back0
    pq_chk:
        ;; ---- 海面(画面の下)・左右の外は消滅 ----
        ld   a, 3(ix)
        cp   #246                 ; YBIAS(48) + 198
        jr   nc, pq_kill
        ld   a, 1(ix)
        cp   #250
        jr   nc, pq_kill
        ;; ---- まだ画面の上なら「描かなかった」印を付けて次へ ----
        ld   a, 3(ix)
        cp   #48
        jr   nc, pq_color
        ld   6(ix), #0xFF
        jr   pq_back
    pq_kill:
        ld   3(ix), #0xFF
        ld   6(ix), #0xFF
        jr   pq_back
    pq_color:
        ;; ---- 色: 破片=中灰 / 火の粉=橙・赤・白 ----
        ld   a, 4(ix)
        and  #3
        jr   nz, pq_spark
        ld   a, #0x44
        jr   pq_have
    pq_spark:
        dec  a
        jr   z, pq_s1
        dec  a
        jr   z, pq_s2
        ld   a, #0xFF
        jr   pq_have
    pq_s1:
        ld   a, #0xCC
        jr   pq_have
    pq_s2:
        ld   a, #0xBB
    pq_have:
        ld   (_pcol), a
        ;; ---- 描く(2 行)。描いた場所を覚える ----
        ld   a, 3(ix)
        sub  #48
        ld   7(ix), a             ; py = 画面行
        ld   b, a                 ; 行を退避
        ld   a, 1(ix)
        ld   6(ix), a             ; px = x(★ld 6(ix),1(ix) は不可。A を介す)
        ld   c, a
        ld   a, b
        call pq_draw
        ld   c, 6(ix)
        ld   a, 7(ix)
        inc  a
        call pq_draw
        jr   pq_back

        ;; --- C=x(ドット) A=画面行: その位置を海の色で塗り戻す ---
    pq_erase:
        push af
        call pq_addr
        pop  af                   ; A = 画面行
        ld   b, a
        ld   a, (_vscroll_now)
        add  a, b
        and  #15
        add  a, #0xC6             ; TMPL の行(1 行 128B)
        ld   h, a
        ld   a, 6(ix)
        srl  a
        ld   l, a
        ld   a, (hl)
        out  (0x98), a
        ret

        ;; --- C=x(ドット) A=画面行: 粒の色を打つ ---
    pq_draw:
        call pq_addr
        ld   a, (_pcol)
        out  (0x98), a
        ret

        ;; --- C=x(ドット) A=画面行 → VRAM 書込み番地を設定 ---
        ;;   番地 = 0x8000 + row*128 + (x>>1)。ラッチは 14bit なので A14-A16 は R#14 へ。
        ;;   page1(0x8000) ゆえ R#14 = 2 + (row>>7)。ここを 0 にしていて粒が page0(非表示)に
        ;;   描かれていた(実際に踏んだ)。
    pq_addr:
        ld   b, a
        ld   a, (_vscroll_now)
        add  a, b                 ; リング行(0..255)
        ld   b, a
        rlca
        and  #1
        add  a, #2
        ld   d, a                 ; R#14
        ld   a, b
        and  #0x7F
        srl  a
        ld   h, a                 ; ラッチ上位
        ld   a, b
        and  #1
        rrca                      ; (row&1)<<7
        ld   l, a
        ld   a, c
        srl  a
        add  a, l
        ld   l, a                 ; ラッチ下位
        di
        ld   a, d
        out  (0x99), a
        ld   a, #0x80 + 14
        out  (0x99), a
        ld   a, l
        out  (0x99), a
        ld   a, h
        or   #0x40
        out  (0x99), a
        ei
        ret
    __endasm;
}
static void step_plot_end(void) __naked { __asm ret __endasm; }
static void (*stepr)(void);

/* 噴き口から粒を出す。★山なりに散らす(横に強く・上に強く)。 */
static void emit(u8 n) {
    u8 *p = PRT;
    u16 i;
    for (i = 0; i < PRT_N && n; i++, p += PRT_SZ) {
        if (p[3] != 0xFF) continue;
        {
            u16 r = rnd16();
            p[0] = (u8)(r & 0xFF);
            p[1] = (u8)(112 + ((r >> 8) & 31));        /* x(ドット): 艦の中心付近 */
            p[2] = (u8)(r >> 5);
            p[3] = (u8)(emit_y + YBIAS);
            r = rnd16();
            p[4] = (u8)((s8)((r & 0x7F) - 64));        /* 横 ±64(×16 で ±4 ドット/フレーム) */
            p[5] = (u8)((s8)(((r >> 9) & 0x3F) - 32)); /* 縦 ±32(放射状) */
            p[6] = 0xFF;                               /* まだ描いていない */
        }
        n--;
    }
}

/* 噴き口が通ったところの艦を海に戻す。★端をぎざぎざにする(一直線に消えると
   「艦が消えただけ」に見えるため、行ごとに左右の食い込みを乱数で変える)。 */
static void erase_row(u8 y) {
    u16 ring = (u16)(256 + (u8)(vscroll_now + y));
    u16 r = rnd16();
    u16 x0 = (u16)(r & 31);                 /* 左の食い込み 0..31 */
    u16 w  = (u16)(256 - x0 - ((r >> 5) & 31));
    vdp_copy(x0, (u16)(SC_SEATMPL_Y + (u8)(ring & 15)), x0, ring, w, 1);
}

static void white_pal(void) { u8 i; for (i = 0; i < 16; i++) vdp_set_pal(i, 7, 7, 7); }

/* 撃沈の直後に常駐から呼ばれる(OVL_SLOT_PART)。戻るまで数秒ここに居る。 */
void ovl_part(void) {
    u16 k, t;
    const u8 *sp = (const u8 *)step_plot;
    u16 len = (u16)((const u8 *)step_plot_end - sp);
    if (len > 640) return;
    seed = 0x2468;
    vscroll_now = g_vscroll;
    grab_tmpl();
    for (k = 0; k < PRT_N; k++) { PRT[k * PRT_SZ + 3] = 0xFF; PRT[k * PRT_SZ + 6] = 0xFF; }
    for (k = 0; k < len; k++) CODE[k] = sp[k];
    stepr = (void (*)(void))CODE;
    emit_y = 0;
    raster_off();
    /* ★白フラッシュ: 轟沈の瞬間を作る(パレットだけ=帯域ゼロ) */
    white_pal();
    vdp_wait_frame();
    vdp_wait_frame();
    vdp_palette_game();
    for (t = 0; t < SEQ_END; t++) {
        if (t < SEQ_FALL) {
            u8 ny = (u8)(((u16)t * 196) / SEQ_FALL);
            while (emit_y < ny) { erase_row(emit_y); emit_y++; }
            emit((u8)((t < 12) ? 28 : 16));           /* 最初にどっと噴き、以降も絶やさない */
        }
        stepr();
        vdp_wait_frame();
    }
}
