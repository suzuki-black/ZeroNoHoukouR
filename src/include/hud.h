/* hud.h — スプライトHUD(スコア/残機)。
   面表示は page1(スクロールする環状バッファ)なので vdp_text(page0直書き)は使えない。
   代わりに数字をスプライトで先頭スロット(0..HUD_SLOTS-1)に固定表示する。
   スプライトは縦スクロール補正(g_vscroll)済みなので Y を画面座標で置けば画面固定になる。 */
#ifndef HUD_H
#define HUD_H

#include "types.h"

/* ★HUD は**スロットの最後尾**に置く＝スプライトの最低優先度。V9938 は「低slot=高優先」かつ
     「1走査線に出せるのは低slot から 8 枚」なので、優先度と欠けやすさは同じつまみ。
     前は先頭(0..8)に置いて絶対に欠けないようにしていたが、そのぶん HUD が中ボスや撃破点数の
     ポップの**手前**に出て不自然だった(指摘)。最後尾に回すと HUD が先に欠けるので、
     **HUD が同じ走査線に並べる枚数を減らして**から回した:
       ・スコアの上位2桁を 1 枚に詰めた(16x16 の左8列/右8列に 8x8 のグリフを2つ)ので 5枚→4枚
       ・残機表示を上段から**下段(ボム棒と同じ行)**へ移し、上段はスコアの4枚だけにした
     実測(1面30秒, 毎フレーム SAT を採取): スコアの行に居るゲームのスプライトは 0枚=70% /
     3枚=14% / 5枚以上=3%。上段4枚なら欠けるのは 5枚以上のときだけ＝約3%、
     下段(3枚)は 6枚以上が必要で 0% だった。
   ★並びは「落ちても困らない順」。上位桁ほど手前(低slot)に置き、いちばん先に落ちるのは
     スコアの一の位。 */
#ifdef DEBUG_FPS
#define HUD_SLOTS 12  /* 通常8 ＋ FPS2桁 ＋ mask値2桁 */
extern u8  g_fps;     /* JIFFY増分を校正して算出した実FPS */
extern u16 g_frame;   /* ★JIFFY非依存: ループ毎+1のフレームカウンタ。ストップウォッチ検証用 */
extern u8  g_dbgmask; /* ★M(TRIGB)で0→7巡回。bit0=海停止/bit1=AI停止/bit2=描画停止。切り分け用 */
#else
#define HUD_SLOTS 8   /* スコア上位2桁(詰め)＋3桁＝4 / ボム棒＋残数 / 残機アイコン＋残機数 */
#endif

/* ゲームのスプライトが使ってよい上限(この後ろが HUD)。ent_draw_all / 弾幕 / 中ボスは
   ここを超えて書かない。★停止マーカ(Y=216)もここより手前に置いてはいけない
   (置くと以降=HUD が全部消える)。scene_stage が g_spr_hide_to でそれを止めている。 */
#define SPR_TOP  (32 - HUD_SLOTS)
#define HUD_SL0  SPR_TOP           /* HUD の先頭スロット */
/* HUD の中の並び。★落ちて困る順に前へ(先に欠けるのは後ろ=スコアの一の位)。 */
#define HUD_SL_BAR   (HUD_SL0 + 4)   /* ボムの棒 */
#define HUD_SL_BNUM  (HUD_SL0 + 5)   /* ボム残数(4本目以降の数字) */
#define HUD_SL_LICON (HUD_SL0 + 6)   /* 残機アイコン(零戦) */
#define HUD_SL_LNUM  (HUD_SL0 + 7)   /* 予備機数 */

void hud_init(void);                 /* 数字パターン投入＋HUD色＋g_spr_base 確保 */
void hud_colors(void);               /* HUDスロットの色だけ置き直す(色表を他用途に使った後の復旧) */
void hud_draw(u16 score, u8 lives);  /* スコア5桁＋残機を HUD スロットへ(毎フレーム) */

#endif /* HUD_H */
