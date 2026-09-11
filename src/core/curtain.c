/* curtain.c — CPU弾幕の常駐側(プール実体とリセットのみ)。
   ★本体(生成/更新/描画)は RAM オーバレイ side = banked/ovl_curtain.c にある。
     常駐コード窓(24KB)が満杯なので、毎フレーム回る演出コードはオーバレイへ出した(ROADMAP P0-2)。
     ラッパ(jp 0xA000+3*slot)は overlay.c にある。
   ★curtain_reset だけは常駐: シーン初期化(page2=cart)の文脈で呼ばれるため、
     オーバレイ(0xA000=page2 が RAM のときだけ有効)には置けない。 */
#include "curtain.h"

CBul __at(CBUL_ADDR) g_cbul[CBUL_MAX];
u8 g_cbul_live;

void curtain_reset(void) {
    u8 i;
    for (i = 0; i < CBUL_MAX; i++) g_cbul[i].alive = 0;
    g_cbul_live = 0;
}
