/* resram.c — 常駐 RAM の「予約」だけを置くモジュール。
   ★RESIDENT_RELS の**最後**に置くこと。理由は番地の積み上がり方にある。

   _DATA はリンク順に積まれる。hot_ram(5696B) より**前**のモジュールで配列を増やすと
   hot_ram の開始番地がその分だけ下がる。ところが ovl_spin はきりもみの演出で
   hot_ram の 0xC600-0xD9FF を借りており、hot_ram が 0xC600 を越えて下がると収まらない
   (Makefile がリンク後に検査して止める。実際 g_ras をここへ移す最初の試みは
    hot_ram=0xC608 となって弾かれた。余白はわずか 100B しか無い)。

   末尾に置けば hot_ram の番地は動かず、_DATA の末尾に残っている空き
   (0xDEAE 以降。天井は 0xE000)をそのまま使える。
   ★ここに足すときは必ずビルドして「0xE000 まで残り」を確認すること。 */
#include "types.h"
#include "raster.h"
#include "pcm.h"

/* ラスタ分割表。以前は 0xEF00 へ固定していたが、そこはスタックの領域だった(raster.h 参照)。 */
RasSplit g_ras[RAS_MAX];

/* スネアの素材を置く枠 256B。★__at の配列は番地を決めるだけで _DATA を食わない。
   中身は起動時に coldsetup.c(冷たいバンク)が写す。 */
u8 __at(PCM_SNARE_ADDR) pcm_snare_buf[PCM_SNARE_SLOT];
