/* hwdiag.h — ★実機の計測用 ROM(make clean && make HWDIAG=1)。2026-10-12。
   実機でだけ出る不具合(3 面の中ボスの艦が止まる / 敵弾の速さが上り下りで違う / 5 面で撃沈にならない)を測る。
   ★常駐のコードには一切手を入れない(コードが数バイト動くだけで 3 面の中ボスの結果が変わったため)。
     計測はオーバレイ(ovl_mb_dd.c / ovl_curtain.c。どちらもそのオーバレイの最後にリンクされる)の中だけで行い、
     結果は HUD の点数の欄に「1 桁目 = 値の番号、残り 4 桁 = 値」で出す(点数は上書きされる)。
   ★掛け算・割り算は常駐の計算ルーチンを呼ばないよう、シフトと引き算で書く。 */
#ifndef HWDIAG_H
#define HWDIAG_H
#ifdef HWDIAG
#include "types.h"
#include "gamestate.h"   /* g_score */

extern u16 g_ras_ttime;  /* raster.c: 前の合図の時刻(システムタイマ) */
extern u8  g_ras_tick;   /* raster.c: いま R#19 に張っているのは 0 = 分割 / 1 = 合図 / 2 = PCM */
#define DG_JIFFY (*(volatile u16 *)0xFC9E)

/* システムタイマ(E6h/E7h、3.911µs)を読む。常駐の _pcm_now(HL で返す)を呼ぶ */
static u16 dg_now(void) __naked {
    __asm
        call _pcm_now
        ex   de, hl
        ret
    __endasm;
}

/* いま割込みが許可されているか(IFF2。ld a,i で P/V に出る)。1 = 許可 */
static u8 dg_iff(void) __naked {
    __asm
        ld   a, i
        ld   a, #0
        jp   po, 00001$
        inc  a
    00001$:
        ret
    __endasm;
}

/* x * 100(x <= 655) */
static u16 dg_x100(u16 x) { return (u16)((x << 6) + (x << 5) + (x << 2)); }

/* a / b(b >= 1。引き算で) */
static u16 dg_div(u16 a, u16 b) { u16 q = 0; while (a >= b) { a = (u16)(a - b); q++; } return q; }

/* 点数の欄へ: id(1..5) を 1 桁目、v(0..9999) を残り 4 桁 */
static const u16 dg_base[6] = { 0, 10000, 20000, 30000, 40000, 50000 };
static void dg_show(u8 id, u16 v) { if (v > 9999) v = 9999; g_score = (u16)(dg_base[id] + v); }

#endif /* HWDIAG */
#endif /* HWDIAG_H */
