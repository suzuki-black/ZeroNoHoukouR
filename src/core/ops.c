/* ops.c — データ駆動描画インタプリタ run_ops(骨格)。
   現状は矩形(LMMV)のみ。艦体を op 配列から描く土台。将来 op を拡張。 */
#include "ops.h"
#include "vdp.h"

void run_ops(u16 ox, u16 oy, const u8 *p) {
    while (*p != OPS_END) {
        u8 op = *p++;
        if (op == OPS_RECT) {
            u8 x = *p++, y = *p++, w = *p++, h = *p++, c = *p++;
            vdp_fill(ox + x, oy + y, w, h, c);
        } else {
            break;   /* 未知opは安全側で終端 */
        }
    }
}
