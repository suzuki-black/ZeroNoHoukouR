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
u8  g_loop_t;     /* 宙返り中の残りフレーム(0=通常) */
u8  g_loop_cd;    /* 宙返りのクールダウン */
u8  g_loop_alt;   /* 宙返り中の高度(影を離す量) */
u8  g_shock_t;    /* 衝撃波ディストーションの残りフレーム(0=なし) */
u8  g_shock_y;    /* 衝撃波の震源の画面Y */
u8  g_crush;      /* メガクラッシュ残数(stage_build で補充) */
u8  g_pdmg;
u8  g_mb;         /* 中ボスの段階(midboss.h の MB_*)。面の準備で MB_NONE */
u8  g_mb_n;       /* 中ボスがいま使っているスプライト枚数(末尾から)。エンティティは 32-g_mb_n まで */
u8  g_mb_req;     /* 中ボス: オーバレイが読んでほしい向き(0xFF=なし)。常駐がホット区間の外で読む */
u8  g_mb_new;     /* 中ボス: 読み終えた(オーバレイが次のフレームで書く) */
u8  g_pwr;        /* 自機弾のパワーアップ段階 0..3(増槽を取るたびに+1。ミス/新規ゲーム/最終面で0) */
u8  g_crush_t;    /* メガクラッシュ発動中の残りフレーム(0=非発動) */

/* ★衝撃波を起こす(震源の画面Y)。画面外なら起こさない。
   エンティティの y は画面座標(砲塔は hot.c の bh_turret が ay-g_cam で毎フレーム入れている)。 */
void shock_at(s16 sy) {
    if (sy < 0 || sy > 211) return;
    g_shock_y = (u8)sy;
    g_shock_t = SHOCK_FRAMES;
}

/* 難易度で間隔をスケール(EASY=1.25倍遅い/NORMAL=等倍/HARD=0.75倍速い)。レイジ中は更に×2/3。下限1。 */
u8 diff_interval(u8 base) {
    static const u8 m[3] = { 5, 4, 3 };
    u16 v = (u16)base * m[(g_difficulty < 3) ? g_difficulty : 1] / 4;
    if (g_rage) v = v * 2 / 3;    /* ボス最後の砲台=最終抵抗の速射 */
    if (v < 1) v = 1;
    return (v > 255) ? 255 : (u8)v;
}
