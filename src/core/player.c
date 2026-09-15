/* player.c — 自機の behavior(移動＋発砲)。入力(input)と効果音(sound)を使う。 */
#include "player.h"
#include "input.h"
#include "sound.h"
#include "sprites.h"
#include "gamestate.h"  /* g_loop_t: 宙返り中は撃てない＆回転コマを使う */

#define SCR_W 256
#define SCR_H 212
#define PSPEED   3   /* 移動px/frame       */
#define PCOOLDN  6   /* 連射クールダウン    */
#define PCOOLDN_PW 10 /* パワーアップ中の連射クールダウン(3発ずつなので間隔を延ばす) */
/* パワーアップの3発: 縦/左上/右上 */
static const s8 pw_dx[3]  = { 0, -6, 6 };
static const s8 pw_vx[3]  = { 0, -3, 3 };
static const s8 pw_vy[3]  = { -6, -5, -5 };
static const u8 pw_pat[3] = { SPR_PWV, SPR_PWL, SPR_PWR };
static const u8 pw_dmg[PWR_MAX + 1] = { 2, 4, 5, 6 };   /* 半分単位の威力(通常/2倍/2.5倍/3倍) */
/* 弾の行ごとの色(弾は 4〜11 行)。1段目=赤で先端だけ白 / 2段目=白で根元だけ赤 / 3段目=全部白(白熱) */
static const u8 pw_col1[16] = { 15,15,15,15, 15,15,11,11, 11,11,11,11, 11,11,11,11 };
static const u8 pw_col2[16] = { 15,15,15,15, 15,15,15,15, 15,15,11,11, 11,11,11,11 };
static const u8 pw_col3[16] = { 15,15,15,15, 15,15,15,15, 15,15,15,15, 15,15,15,15 };
static const u8 * const pw_col[PWR_MAX + 1] = { (const u8 *)0, pw_col1, pw_col2, pw_col3 };

u8 g_player_x, g_player_y;
u8 g_py_min;   /* ★自機が上がれる限界(画面Y)。最終面は壁の内側まで。通常面は0 */

void bh_player(Entity *e) {
    u8 in = g_input;

    if (in & INP_LEFT)  e->x -= PSPEED;
    if (in & INP_RIGHT) e->x += PSPEED;
    if (in & INP_UP)    e->y -= PSPEED;
    if (in & INP_DOWN)  e->y += PSPEED;

    /* 画面内へクランプ(スプライト16x16) */
    if (e->x < 0) e->x = 0; else if (e->x > (s16)(SCR_W - 16)) e->x = SCR_W - 16;
    if (e->y < (s16)g_py_min) e->y = g_py_min; else if (e->y > (s16)(SCR_H - 16)) e->y = SCR_H - 16;

    /* 発砲(トリガ押下＋クールダウン) — 自機弾は上方向、TEAM_PLAYER
       ★宙返り中は撃てない(「無敵で撃ち放題」にしないための代償。ROADMAP P2 項目9)。 */
    if (e->ftimer) e->ftimer--;
    if ((in & INP_TRIG) && e->ftimer == 0 && !g_loop_t) {
        /* ★パワーアップ中は太い弾を縦・左上・右上へ1発ずつ。発射間隔を 6→10 に延ばして画面上の自機弾を約9枚に抑える
           (1走査線に乗るのは同じ回の3発だけ)。威力は段階ごと(半分単位、gamestate.h)、色は行ごとに段階で変える。 */
        u8 k, n = g_pwr ? 3 : 1;
        g_pdmg = pw_dmg[g_pwr];
        for (k = 0; k < n; k++) {
            Entity *b = ent_spawn(ET_BULLET);
            if (!b) break;
            b->x = (s16)(e->x + pw_dx[k]); b->y = e->y - 10;
            b->vx = pw_vx[k]; b->vy = pw_vy[k];
            b->team = TEAM_PLAYER; b->hp = g_pdmg;
            b->pat = g_pwr ? pw_pat[k] : SPR_PBULLET; b->color = 11;   /* 通常=赤い縦ストリーク(旧pat44) */
            b->coltab = pw_col[g_pwr];                                  /* 通常は NULL(単色) */
        }
        e->ftimer = g_pwr ? PCOOLDN_PW : PCOOLDN;
        sfx(0, SFX_SHOT);
    }

    /* プロペラ回転: 先頭2行(細/太)を交互にしてブラー。被弾点滅中(hidden)は下で上書き。
       ★宙返り中の本体は ovl_rot_zoom の2×2合成が描く(entity.c が本体を描かない)。影はこの pat を使う。 */
    { static u8 prop; prop++; e->pat = (prop & 2) ? SPR_ZERO2 : SPR_ZERO; }

    /* 被弾直後の無敵: カウントを減らしつつ点滅(4フレーム周期で明滅) */
    if (g_pinv) { g_pinv--; e->hidden = (g_pinv & 4) ? 1 : 0; }
    else        e->hidden = 0;

    /* 現在位置を公開(AIMED/当たり判定用) */
    g_player_x = (u8)e->x;
    g_player_y = (u8)e->y;
}
