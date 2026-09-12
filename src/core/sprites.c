/* sprites.c — スプライトの「常駐」部分: 毎フレーム参照する色表(hot)と、パターン投入のラッパ。
   ★パターンのビットマップ(pat_*, 約768B)は面開始で1回VRAMへ流すだけの cold data なので、
     常駐節約のため banked/ship_render.c(bank16, mode4) へ移設し、sprites_load は薄い bcall ラッパにした。
     hot な色表(zcol/barrel_col/barrel_flash=毎フレーム coltab 参照)だけ常駐に残す。
   mode2 16x16: 前半16B=左列(cols0-7, MSB=col0) rows0-15 / 後半16B=右列(cols8-15) rows0-15。 */
#include "sprites.h"
#include "ship.h"    /* g_shipargs / SHIP_RENDER_BANK(パターン投入を bank16 へ bcall) */
#include "bank.h"    /* bcall_to */
#include "vdp.h"     /* vdp_sprite_pattern(戦闘機8方向の手続き生成でVRAM投入) */

/* 零戦の16行カラーテーブル(旧 zcol): 3/8/10=緑系, 14=ハイライト, 5=暗(プロペラ/尾)。
   ★自機の e->coltab=zcol として毎フレーム参照される=常駐に残す。 */
const u8 zcol[16] = { 5, 5, 3, 8, 14, 8, 10, 8, 8, 3, 8, 8, 3, 8, 3, 3 };

/* 砲身の行別シェード(金属感): 黒縁→暗灰→中灰 の対称バンド。★白(15)/淡灰(14)は使わない=
   ドームの白っぽいテカリと同化して砲身がちょん切れて見えるのを防ぐ(ハイライトは中灰4止まり)。
   ★bh_turret が e->coltab=barrel_col/flash として毎フレーム参照する=常駐に残す。 */
const u8 barrel_col[16]   = { 13,1,1,5,5,4,4,4,4,4,4,5,5,1,1,13 };  /* 暗いガンメタル砲身(ドームの白と明確に対比) */
const u8 barrel_flash[16] = { 15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15 };

/* スプライトパターン投入(各シーンが init で1回呼ぶ)。重いビットマップ(pat_*)は常駐節約のため
   bank16 に置いたので、引数なしの cold 経路として mode4 で bcall(カード拡大等と同じ機構)。 */
void sprites_load(u8 stage) {
    g_shipargs.mode = 4;
    bcall_to(SHIP_RENDER_BANK);   /* 静的パターン(bank16) */
    /* ★戦闘機8方向×3サイズの手続き生成は bank19(gen_planes.c)へ移設。面開始で1回しか走らない
       cold code が約900B 常駐を食っていた。演出を常時オンにしたら DEBUG_PROF ビルドが常駐24KB を
       超過したのが契機(性能と高速化 §3-C の常駐リクレイム)。引数は g_shipargs.hull で渡す。 */
    g_shipargs.hull = stage;
    bcall_to(GEN_PLANES_BANK);
}
