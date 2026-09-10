/* gamestate.c — 共有ゲーム状態の実体(常駐)。既定は NORMAL / 残機3。 */
#include "gamestate.h"

u8  g_difficulty = 1;   /* NORMAL */
u8  g_lives_idx  = 1;   /* 3機     */
u8  g_durability = 1;   /* 耐久HP(既定=1=一撃死。今の難度だと3面まで行けて簡単すぎるため) */
u8  g_view       = 0;   /* 画面ビューア: 0=通常/1=カードのみ/2=結果のみ(config設定) */
u8  g_stage_sel  = 0;   /* 1面     */
u8  g_continue   = 1;   /* 継続ON  */
u8  g_invinc     = 0;   /* 無敵OFF */
u16 g_score;
u16 g_hiscore;
u8  g_lives;
u8  g_php;
u8  g_rage;

/* 難易度で間隔をスケール(EASY=1.25倍遅い/NORMAL=等倍/HARD=0.75倍速い)。レイジ中は更に×2/3。下限1。 */
u8 diff_interval(u8 base) {
    static const u8 m[3] = { 5, 4, 3 };
    u16 v = (u16)base * m[(g_difficulty < 3) ? g_difficulty : 1] / 4;
    if (g_rage) v = v * 2 / 3;    /* ボス最後の砲台=最終抵抗の速射 */
    if (v < 1) v = 1;
    return (v > 255) ? 255 : (u8)v;
}
