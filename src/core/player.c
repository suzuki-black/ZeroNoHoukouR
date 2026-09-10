/* player.c — 自機の behavior(移動＋発砲)。入力(input)と効果音(sound)を使う。 */
#include "player.h"
#include "input.h"
#include "sound.h"
#include "sprites.h"

#define SCR_W 256
#define SCR_H 212
#define PSPEED   3   /* 移動px/frame       */
#define PCOOLDN  6   /* 連射クールダウン    */

u8 g_player_x, g_player_y;

void bh_player(Entity *e) {
    u8 in = g_input;

    if (in & INP_LEFT)  e->x -= PSPEED;
    if (in & INP_RIGHT) e->x += PSPEED;
    if (in & INP_UP)    e->y -= PSPEED;
    if (in & INP_DOWN)  e->y += PSPEED;

    /* 画面内へクランプ(スプライト16x16) */
    if (e->x < 0) e->x = 0; else if (e->x > (s16)(SCR_W - 16)) e->x = SCR_W - 16;
    if (e->y < 0) e->y = 0; else if (e->y > (s16)(SCR_H - 16)) e->y = SCR_H - 16;

    /* 発砲(トリガ押下＋クールダウン) — 自機弾は上方向、TEAM_PLAYER */
    if (e->ftimer) e->ftimer--;
    if ((in & INP_TRIG) && e->ftimer == 0) {
        Entity *b = ent_spawn(ET_BULLET);
        if (b) {
            b->x = e->x; b->y = e->y - 10;
            b->vx = 0; b->vy = -6;
            b->team = TEAM_PLAYER;
            b->pat = SPR_PBULLET; b->color = 11;   /* 赤い縦ストリーク(旧pat44) */
        }
        e->ftimer = PCOOLDN;
        sfx(0, SFX_SHOT);
    }

    /* プロペラ回転: 先頭2行(細/太)を交互にしてブラー。被弾点滅中(hidden)は下で上書き。 */
    { static u8 prop; prop++; e->pat = (prop & 2) ? SPR_ZERO2 : SPR_ZERO; }

    /* 被弾直後の無敵: カウントを減らしつつ点滅(4フレーム周期で明滅) */
    if (g_pinv) { g_pinv--; e->hidden = (g_pinv & 4) ? 1 : 0; }
    else        e->hidden = 0;

    /* 現在位置を公開(AIMED/当たり判定用) */
    g_player_x = (u8)e->x;
    g_player_y = (u8)e->y;
}
