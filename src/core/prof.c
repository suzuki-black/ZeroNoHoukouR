/* prof.c — DEBUG_PROF 計測基盤(詳細は prof.h)。全体を #ifdef DEBUG_PROF で囲むので、
   通常ビルドには何も残らない。
   ★冷たい部分(自己診断画面・区間別µsの凍結表示)は banked/prof_bank.c(bank20)へ移した。
     演出を常時オンに畳んだ結果 DEBUG_PROF ビルドだけが常駐24KBを超えたため
     (性能と高速化 §3-C の常駐リクレイム)。ここに残すのは
       ・タイマ読み(区間計測の毎フレーム呼び)
       ・蓄積カウンタ
       ・キー待ち(_bcall は di で囲むのでバンク内で待ってはいけない)
     だけ。 */
#ifdef DEBUG_PROF
#include "prof.h"
#include "vdp.h"
#include "input.h"
#include "bank.h"

__sfr __at(0xE7) STMR_HI;   /* S1990 システムタイマ 上位(turboR) */
__sfr __at(0xE6) STMR_LO;   /* 同 下位 */

/* 上位を挟んで読み、桁上がりレースを排除(hi,lo,hi で hi 不変なら整合)。 */
u16 prof_tick(void) {
    u8 h1, lo, h2;
    do { h1 = STMR_HI; lo = STMR_LO; h2 = STMR_HI; } while (h1 != h2);
    return (u16)(((u16)h2 << 8) | lo);
}

u32 __at(PROF_RAM_ADDR) g_prof_acc[PF_N];
u16 g_prof_over;
u8  g_prof_mode;   /* bank20 への用件: 0=自己診断 / 1=区間表示 */

/* 起動時1回: 実機µs自己診断を bank20 に描かせ、トリガ押下→離しで抜ける。 */
void prof_selftest(void) {
    g_prof_mode = 0;
    bcall_to(PROF_BANK);
    for (;;) { input_poll(); if (g_input & INP_TRIG) break; }
    for (;;) { input_poll(); if (!(g_input & INP_TRIG)) break; }
}

/* 各フレーム末に呼ぶ。60フレーム(=約1秒)蓄積したら画面を凍結して区間別µsを表示、Mキーで再開。
   ★表示はpage0へ切替→Mで復帰時にpage1へ戻す(ゲームは次フレームのスクロールでpage確定)。 */
void prof_frame_end(u16 compute_ticks) {
    static u8 fc;
    if (compute_ticks > 4262) g_prof_over++;   /* 1VBLANK(16.7ms)超の計算フレーム数 */
    if (++fc < 60) return;
    fc = 0;
    g_prof_mode = 1;
    bcall_to(PROF_BANK);                       /* 値の整形と描画(蓄積のクリアも向こう側) */
    for (;;) { input_poll(); if (g_input & INP_TRIGB) break; }     /* Mキーで進む(発砲トリガと別) */
    for (;;) { input_poll(); if (!(g_input & INP_TRIGB)) break; }
    vdp_set_display_page(1);
}
#endif /* DEBUG_PROF */
