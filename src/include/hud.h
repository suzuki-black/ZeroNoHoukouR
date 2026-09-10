/* hud.h — スプライトHUD(スコア/残機)。
   面表示は page1(スクロールする環状バッファ)なので vdp_text(page0直書き)は使えない。
   代わりに数字をスプライトで先頭スロット(0..HUD_SLOTS-1)に固定表示する。
   スプライトは縦スクロール補正(g_vscroll)済みなので Y を画面座標で置けば画面固定になる。 */
#ifndef HUD_H
#define HUD_H

#include "types.h"

#ifdef DEBUG_FPS
#define HUD_SLOTS 15  /* 通常7 ＋ FPS2桁(7,8) ＋ "MASK"4字(9-12) ＋ mask値2桁(13,14)。ent_draw_all は slot15 以降 */
extern u8  g_fps;     /* JIFFY増分を校正して算出した実FPS */
extern u16 g_frame;   /* ★JIFFY非依存: ループ毎+1のフレームカウンタ。ストップウォッチ検証用 */
extern u8  g_dbgmask; /* ★M(TRIGB)で0→7巡回。bit0=海停止/bit1=AI停止/bit2=描画停止。切り分け用 */
#else
#define HUD_SLOTS 7   /* スコア5桁(0-4)＋残機アイコン(5=零戦シルエット)＋残機数(6)。ent_draw_all は slot7 以降 */
#endif

void hud_init(void);                 /* 数字パターン投入＋HUD色＋g_spr_base 確保 */
void hud_draw(u16 score, u8 lives);  /* スコア5桁＋残機を HUD スロットへ(毎フレーム) */

#endif /* HUD_H */
