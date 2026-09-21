/* hud.h — スプライトHUD(スコア/残機)。
   面表示は page1(スクロールする環状バッファ)なので vdp_text(page0直書き)は使えない。
   代わりに数字をスプライトで先頭スロット(0..HUD_SLOTS-1)に固定表示する。
   スプライトは縦スクロール補正(g_vscroll)済みなので Y を画面座標で置けば画面固定になる。 */
#ifndef HUD_H
#define HUD_H

#include "types.h"

#ifdef DEBUG_FPS
#define HUD_SLOTS 14  /* 通常10 ＋ FPS2桁(9,10) ＋ mask値2桁(11,12)。ent_draw_all は slot14 以降。
                         ★"MASK" の文字スプライトは廃止した(16x16 パターンの空き枠4つを津波
                           SPR_WAVE0 に譲ったため)。値だけ FPS の隣に出る。 */
extern u8  g_fps;     /* JIFFY増分を校正して算出した実FPS */
extern u16 g_frame;   /* ★JIFFY非依存: ループ毎+1のフレームカウンタ。ストップウォッチ検証用 */
extern u8  g_dbgmask; /* ★M(TRIGB)で0→7巡回。bit0=海停止/bit1=AI停止/bit2=描画停止。切り分け用 */
#else
#define HUD_SLOTS 10  /* スコア5桁(0-4)＋残機アイコン(5)＋残機数(6)＋メガクラッシュ(7=棒,8=残数)＋★パワーアップ段階(9)。
                         ent_draw_all は slot10 以降。★増やすほどゲームのスプライトが減る(最低優先の
                         落ち影/撃破点数から落ちる)ので切り詰めてある。 */
#endif

/* ★パワーアップ段階のアイコンは HUD の最後の1枠(段階が変わってもここ1枚。パターンを描き換える)。
   以前は段階ぶん(最大3枚)並べていたが、枠を3つも食うのと、増槽の絵が爆弾に見えると実機で指摘された。 */
#define SPR_SLOT_PWR (HUD_SLOTS - 1)

void hud_init(void);                 /* 数字パターン投入＋HUD色＋g_spr_base 確保 */
void hud_colors(void);               /* HUDスロットの色だけ置き直す(色表を他用途に使った後の復旧) */
void hud_draw(u16 score, u8 lives);  /* スコア5桁＋残機を HUD スロットへ(毎フレーム) */

#endif /* HUD_H */
