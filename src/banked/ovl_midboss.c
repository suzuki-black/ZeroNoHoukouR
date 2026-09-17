/* ovl_midboss.c — 1面の中ボス Fw 200 コンドル(ROADMAP B4)。中ボス用オーバレイ(OVL8_BANK)で動く。
   ★見せ場は「64方向の回転」。コマは tools/gen_fw200.py が焼いた 64×128B を、面の開始で常駐(mb_bake)が
     page0 の MB_VRAM_Y 行から並べてある。向きが変わったフレームだけ、その1行を HMMM で
     スプライトパターン表の 247 行(=主砲の砲身 SPR_BARREL0..+12 の枠)へ写す。CPU の仕事はほぼ無い。
   ★砲身の枠は戦艦が出るまで使わないので借りる。終わったら常駐が退避しておいた行を書き戻す。
   ★スプライトは HUD の直後の4枠(宙返りの4枠のさらに後ろ)。その間エンティティは8つ後ろから詰める(常駐)。
   ★動き: 画面上から降りてきて、自機の上空を中心に反時計回りに旋回(半径は少しずつ縮む)。
     側面銃座から自機狙いの3方向弾。40秒で逃げる。撃墜で 200点＋残り時間ボーナス＋メガクラッシュ1回。
   ★終わったら g_mb=2 にするだけ。オーバレイの入れ替えと砲身の書き戻しは常駐がホット区間の外で行う。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"       /* emit / aim_dir */
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "hud.h"        /* HUD_SLOTS */
#include "sound.h"
#include "midboss.h"

extern u8 rnd(void);

#define MB_HP       320     /* 半分単位(通常弾 2)。通常弾で160発 */
#define MB_TIMEOUT  1200    /* 40秒で逃げる */
#define MB_SPIN     72      /* 旋回の角速度(64方向を 256 分割した単位/フレーム)=約220フレームで1周 */
#define MB_PIERCE   0x7ABC  /* 貫通弾に付ける印(同じ弾が毎フレーム当たらないように) */

enum { ST_ENTER, ST_ORBIT, ST_LEAVE, ST_DIE, ST_DONE };

static const s8 sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125, 122, 117, 112, 106, 98,
    90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122,
    -125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };
/* 行ごとの色: 画面に対して上から光が当たる(機体が回っても光の向きは変わらない)。上6行=淡灰/中=灰/下6行=暗灰 */
static const u8 col_top[16]   = { 14,14,14,14,14,14, 4,4,4,4,4,4,4,4,4,4 };
static const u8 col_bot[16]   = { 4,4,4,4,4,4,4,4,4,4, 5,5,5,5,5,5 };
static const u8 col_flash[16] = { 15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15 };

static u8  st, fcur, flast, flash, fire_t, r;
static u16 phi, t, hp;
static s16 bx, by, cx, cy;   /* bx,by=32x32 の左上(画面座標) / cx,cy=旋回の中心 */

void ovl_mb_init(void) {
    st = ST_ENTER; t = 0; hp = MB_HP; r = 72;
    cx = 128; cy = 88;
    bx = (s16)(cx - r - 16); by = -40;
    phi = (u16)48 << 8;      /* 旋回の入口=円の左端。そこでの進行方向は真下(=入場の向き) */
    fcur = 32; flast = 0xFF; flash = 0; fire_t = 60;
}

static void turn_to(u8 tgt) {
    u8 d = (u8)((tgt - fcur) & 63);
    if (d) fcur = (u8)((fcur + ((d < 32) ? 1 : 63)) & 63);
}

static void put_sprites(void) {
    u8 i;
    s16 x = bx;
    if (x < 0) x = 0; else if (x > 224) x = 224;   /* X は負にできない(EC 不使用) */
    for (i = 0; i < 4; i++) {
        u8 sl = (u8)(MB_SLOT + i);
        s16 y = (s16)(by + ((i & 2) ? 16 : 0));
        vdp_sprite_color_tab(sl, flash ? col_flash : ((i & 2) ? col_bot : col_top));
        vdp_sprite_pos(sl, (u8)(x + ((i & 1) ? 16 : 0)), (y < -16 || y > 212) ? 220 : (u8)y,
                       (u8)(MB_PAT + (i << 2)));
    }
    if (!g_loop_t) for (i = 0; i < 4; i++) vdp_sprite_pos((u8)(HUD_SLOTS + i), 0, 220, MB_PAT);   /* 宙返りの枠は空けておく */
}

static void finish(void) {
    u8 i;
    for (i = 0; i < 8; i++) vdp_sprite_pos((u8)(HUD_SLOTS + i), 0, 220, MB_PAT);
    st = ST_DONE;
    g_mb = MB_RESTORE;
}

/* 自機弾との当たり。威力は弾が持つ(b->hp, 半分単位)。3段目の貫通弾は1発につき1回だけ当たる。 */
static void hit_test(void) {
    u8 i;
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        dx = (s16)(e->x - bx - 8); if (dx < 0) dx = -dx; if (dx >= 13) continue;
        dy = (s16)(e->y - by - 8); if (dy < 0) dy = -dy; if (dy >= 13) continue;
        if (e->ax == (s16)MB_PIERCE) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)MB_PIERCE;
        hp = (hp > e->hp) ? (u16)(hp - e->hp) : 0;
        flash = 2;
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
}

static void shoot(void) {
    s16 ox = (s16)(bx + 8), oy = (s16)(by + 8);
    u8 a;
    if (--fire_t) return;
    fire_t = 50;
    if (oy < 0 || oy > 176) return;
    a = aim_dir(ox, oy, g_player_x, g_player_y);
    emit(ox, oy, (u8)(a - 2), 2, 3);
    emit(ox, oy, a,           2, 3);
    emit(ox, oy, (u8)(a + 2), 2, 3);
    sfx(2, SFX_EFIRE);
}

void ovl_mb_frame(void) {
    u8 tgt = fcur;
    if (st == ST_DONE) return;
    t++;
    if (flash) flash--;
    if (st != ST_DIE) {   /* 旋回の中心は自機の横位置へゆっくり寄せる */
        s16 tx = (s16)(g_player_x + 8);
        if (tx < 80) tx = 80; else if (tx > 176) tx = 176;
        if (cx < tx) cx++; else if (cx > tx) cx--;
    }
    switch (st) {
    case ST_ENTER:
        by += 2; bx = (s16)(cx - r - 16); tgt = 32;
        if (by + 16 >= cy) st = ST_ORBIT;
        hit_test();
        break;
    case ST_ORBIT: {
        u8 a;
        phi -= MB_SPIN;
        if (r > 48 && (t & 7) == 0) r--;
        a = (u8)(((phi + 128) >> 8) & 63);
        bx = (s16)(cx + (((s16)r * sin64[a]) >> 7) - 16);
        by = (s16)(cy - (((s16)r * sin64[(a + 16) & 63]) >> 7) - 16);
        tgt = (u8)((a - 16) & 63);          /* 反時計回りの進行方向 */
        hit_test();
        shoot();
        if (t >= MB_TIMEOUT) st = ST_LEAVE;
        break; }
    case ST_LEAVE:                          /* 上へ向き直りながら、向いている方へ飛び去る */
        tgt = 0;
        bx += (s16)(((s16)sin64[fcur] * 3) >> 7);
        by -= (s16)(((s16)sin64[(fcur + 16) & 63] * 3) >> 7);
        if (by < -40 || bx < -40 || bx > 260 || by > 230) { finish(); return; }
        break;
    case ST_DIE:
        by++;
        if ((t & 3) == 0) {
            ent_spawn_explosion((s16)(bx + (rnd() & 15)), (s16)(by + (rnd() & 15)));
            if ((t & 7) == 0) sfx(2, SFX_BOOM);
        }
        flash = (u8)((t >> 1) & 1);
        if (t >= 48) { finish(); return; }
        break;
    }
    if ((st == ST_ENTER || st == ST_ORBIT) && hp == 0) {   /* 撃墜 */
        u16 pts = (u16)(200 + (MB_TIMEOUT - t) / 3);    /* 残り1秒=10点 */
        st = ST_DIE; t = 0;
        g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
        scorepop_add((s16)(bx + 8), (s16)(by + 8), pts);
        if (g_crush < CRUSH_MAX) g_crush++;
        g_hitstop = 4; g_shake = 8;
        shock_at((s16)(by + 16));
        sfx(2, SFX_BOOM);
    }
    turn_to(tgt);
    if (fcur != flast) { flast = fcur; vdp_copy(0, (u16)(MB_VRAM_Y + fcur), 0, MB_PAT_LINE, 256, 1); }
    put_sprites();
}
