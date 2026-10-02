/* opll.c — MSX-MUSIC(OPLL)の書き込み(常駐)。仕様と注意点は opll.h を読むこと。
   ★**検出は冷たいバンク(coldsetup.c, bank30)**にある。起動時に1回しか使わないものを
     常駐に置くと、曲へ繋ぐぶんの枠が無くなるため(常駐は 24KB しかない)。 */
#include "opll.h"

__sfr __at(0x7C) OPLL_ADR;     /* アドレスレジスタ */
__sfr __at(0x7D) OPLL_DAT;     /* データレジスタ   */

u8 g_opll;                     /* 0=無し / 1=内蔵 / 2=FM-PAC。検出が立てる */

/* ★ウェイトのループ回数。1 周 = nop,nop,djnz。
   Z80(3.58MHz): 4+4+13 = 21 ステート = 5.87us/周 → 長い待ち(23.52us)は 4 周で足りる。
   R800(7.16MHz): 1 周がおよそ 0.56us → 同じ 23.52us に 42 周要る。
   ★**速い方(R800)に合わせて**決める。遅い CPU では必要以上に待つが、待ち過ぎは安全側。 */
#define W_LONG   48            /* データ書込み後(23.52us 以上) */
#define W_SHORT   8            /* アドレス書込み後(3.36us 以上) */
static u8 wcnt;

static void opll_delay(void) __naked {
    __asm
        ld   a, (_wcnt)
        ld   b, a
    opw_l:
        nop
        nop
        djnz opw_l
        ret
    __endasm;
}

void opll_w(u8 reg, u8 val) {
    if (!g_opll) return;
    OPLL_ADR = reg;  wcnt = W_SHORT; opll_delay();
    OPLL_DAT = val;  wcnt = W_LONG;  opll_delay();
}
