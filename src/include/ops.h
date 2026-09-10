/* ops.h — データ駆動の描画インタプリタ run_ops(骨格)。
   艦/背景などの大きな造形を op 配列で表現し、run_ops が解釈して VDP コマンドで描く。
   HANDOFF §3/§4-2: 艦は op配列で描画、データは将来バンクへ。

   ShipOps(バイト列): [op, args...] ... OPS_END
     OPS_RECT: x, y, w, h, color   (原点 (ox,oy) からの相対、px)
     OPS_END : 終端
   ※本番では op を増やす(ライン/三角/パターン転送/艦橋段積み等)。座標系は現状 8bit(0-255)。 */
#ifndef OPS_H
#define OPS_H

#include "types.h"

#define OPS_END  0
#define OPS_RECT 1

/* 原点 (ox,oy) に op 配列を描画。 */
void run_ops(u16 ox, u16 oy, const u8 *ops);

#endif /* OPS_H */
