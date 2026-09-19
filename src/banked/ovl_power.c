/* ovl_power.c — パワーアップの増槽(ROADMAP P2-B / A1)。RAM オーバレイ(通常面)で動く。
   ★海モードに1機だけ出る銀の敵機(scene_stage.c)を落とすと、当たり判定(entity.c)が g_drop を立てる。
     ここでその位置に増槽(ET_ITEM)を出し、自機が触れたらパワーアップ段階を上げる。
   ★増槽は海の世界座標に置く(挙動は停泊機と同じ bh_parked)＝海と一緒に下へ流れる。画面の下へ抜けたら消える。
   ★常駐の当たり判定に書くと SDCC の割付が崩れて 800B も膨らんだので、ここ(オーバレイ)に置いた。 */
#include "types.h"
#include "entity.h"
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "sound.h"
#include "sprites.h"
#include "scroll.h"     /* g_cam */

/* 増槽の行ごとの色: 上端=明るい銀 / 胴=銀 / 赤帯 / 尾翼=暗い灰 */
/* 最高段階で取ったときの点数(主砲1基=60、対空砲=20〜30、戦闘機=10 と比べて大きく) */
#define PWR_BONUS 1000u

static const u8 tank_col[16] = { 14,15,15,14, 14,11,11,14, 14,14,14,14, 14,4,4,4 };

/* 増槽を取った: 段階を上げる。最高段階なら高得点 */
static void take(Entity *e) {
    e->active = 0;
    if (g_pwr < PWR_MAX) g_pwr++;
    else {                                                      /* ★最高段階でさらに取ったら高得点 */
        g_score = (g_score > 65535u - PWR_BONUS) ? 65535u : (u16)(g_score + PWR_BONUS);
        scorepop_add(e->x, e->y, PWR_BONUS);
        sfx(2, SFX_BOOM);
    }
    sfx(1, SFX_HIT);
}

/* ★中ボスの前と戦艦の前: 敵機・敵弾・爆発を色を落としながら消す(いきなり消すと唐突=ユーザー指摘)。
   8フレームごとに 淡灰→中灰→海の明→海の地 と沈め、g_fade が 0 になった瞬間に消す。撃つのも影もやめる。
   主砲・停泊機(艦に載っているもの)は触らない。画面に残っている増槽は取ったことにする(取り逃しを無駄にしない)。 */
static const u8 fade_col[4] = { 1, 2, 4, 14 };   /* 段(残り 0〜7 / 8〜15 / 16〜23 / 24〜31)ごとの色 */
static void fade_frame(void) {
    u8 i, col;
    Entity *e = ent_pool();
    col = fade_col[(u8)(g_fade - 1) >> 3];
    g_fade--;
    for (i = 0; i < ENT_MAX; i++, e++) {
        u8 ty = e->type;
        if (!e->active || ty == ET_PLAYER || ty == ET_TURRET || ty == ET_PARKED) continue;
        if (ty == ET_BULLET && e->team == TEAM_PLAYER) continue;
        if (ty == ET_ITEM) { take(e); continue; }
        if (!g_fade) { e->active = 0; continue; }
        e->fire = (const u8 *)0; e->shadow = 0; e->coltab = (const u8 *)0; e->color = col;
    }
}

void ovl_power_frame(void) {
    u8 i;
    Entity *e;
    if (g_fade) fade_frame();
    if (g_drop) {
        g_drop = 0;
        e = ent_spawn(ET_ITEM);
        if (e) {
            e->x = g_drop_x; e->y = g_drop_y;
            e->ax = (s16)(g_drop_x - g_meander); e->ay = (s16)(g_drop_y + (s16)g_cam);
            e->pat = SPR_TANK; e->coltab = tank_col;
        }
    }
    e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_ITEM) continue;
        if (e->y > 212) { e->active = 0; continue; }
        dx = e->x - (s16)g_player_x; dy = e->y - (s16)g_player_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 12 && dy < 12) take(e);
    }
}
