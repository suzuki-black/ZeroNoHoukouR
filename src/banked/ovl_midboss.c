/* ovl_midboss.c — 1面の中ボス Fw 200 コンドル(ROADMAP B4)。中ボス用オーバレイ(OVL8_BANK)で動く。
   ★大きさは 64x64(16x16 を 4x4)。32x32 では「中ボスの迫力が無い」、2機にしても同じと実機で指摘された。
   ★回転は 32 方向。コマは tools/gen_fw200.py が焼いた 32×512B を、面の開始で常駐(mb_bake)が page0 の
     MB_VRAM_Y 行から並べてある。向きが変わったフレームだけ、4行を HMMM 2回でスプライトパターン表へ写す。
   ★スプライトは最低優先の末尾(32-n..31)。絵のあるマスだけに割り当てる(多くの向きで12枚)。
     エンティティは g_spr_limit=32-n の手前まで。混んだ走査線では中ボスの方が先に欠ける(弾が見えなくなるよりよい)。
   ★呼ぶのは ent_draw_all の**後**: その末尾に書かれる停止マーカ(Y=216)を、中ボスの手前まで隠しスプライトで埋め直すため。
   ★動き: 画面上から降りてきて、自機の上空を中心に反時計回りに旋回。側面銃座から自機狙いの3方向弾。
     40秒で逃げる。撃墜で 500点＋残り時間ボーナス＋メガクラッシュ1回。
   ★終わったら g_mb=MB_RESTORE にするだけ。オーバレイの入れ替えとパターンの書き戻しは常駐が行う。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"       /* emit / aim_dir */
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "sound.h"
#include "midboss.h"
#include "fw200_mask.h" /* fw_mask[32]: 各コマで絵のあるマス */

extern u8 rnd(void);

#define MB_HP       400     /* 半分単位(通常弾 2)=通常弾で200発 */
#define MB_TIMEOUT  1200    /* 40秒で逃げる */
#define MB_SPIN     56      /* 旋回の角速度(64分割を 256 分割した単位/フレーム)=約290フレームで1周 */
#define MB_R0       60      /* 半径の初期値 */
#define MB_R1       44      /* 半径の最小値 */
#define MB_PIERCE   0x7ABC  /* 貫通弾に付ける印(同じ弾が毎フレーム当たらないように) */

enum { ST_ENTER, ST_ORBIT, ST_LEAVE, ST_DIE, ST_DONE };

static const s8 sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125, 122, 117, 112, 106, 98,
    90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122,
    -125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };
/* 行ごとの色: 画面に対して上から光が当たる(機体が回っても光の向きは変わらない)。上12行=淡灰/中=灰/下12行=暗灰 */
static const u8 col_row0[16]  = { 14,14,14,14,14,14,14,14,14,14,14,14, 4,4,4,4 };
static const u8 col_mid[16]   = { 4,4,4,4,4,4,4,4, 4,4,4,4,4,4,4,4 };
static const u8 col_row3[16]  = { 4,4,4,4, 5,5,5,5,5,5,5,5,5,5,5,5 };
static const u8 col_flash[16] = { 15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15 };

static u8  st, fcur, flast, flash, fire_t, r, coldirty;
static u16 phi, t, hp, mask;
static s16 bx, by, cx, cy;   /* bx,by=64x64 の左上(画面座標) / cx,cy=旋回の中心 */

void ovl_mb_init(void) {
    st = ST_ENTER; t = 0; hp = MB_HP; r = MB_R0;
    cx = 128; cy = 84;
    bx = (s16)(cx - r - 32); by = -64;
    phi = (u16)48 << 8;      /* 旋回の入口=円の左端。そこでの進行方向は真下(=入場の向き) */
    fcur = 16; flast = 0xFF; flash = 0; fire_t = 60; mask = 0; coldirty = 1;
    g_mb_n = 0;
}

static void turn_to(u8 tgt) {
    u8 d = (u8)((tgt - fcur) & 31);
    if (d && (t & 1)) fcur = (u8)((fcur + ((d < 16) ? 1 : 31)) & 31);
}

/* ent_draw_all の後に呼ぶ: 絵のあるマスを末尾の枠へ置き、エンティティとの間の枠は隠して停止マーカを消す。 */
static void put_sprites(void) {
    u8 c, sl, s0 = (u8)(32 - g_mb_n);
    for (sl = g_spr_used; sl < s0; sl++) vdp_sprite_pos(sl, 0, 220, MB_CELL_PAT(0));
    sl = s0;
    for (c = 0; c < 16; c++) {
        s16 x, y;
        if (!(mask & (1u << c))) continue;
        x = (s16)(bx + ((c & 3) << 4));
        y = (s16)(by + ((c >> 2) << 4));
        if (coldirty)
            vdp_sprite_color_tab(sl, flash ? col_flash : ((c < 4) ? col_row0 : (c >= 12) ? col_row3 : col_mid));
        vdp_sprite_pos(sl, (x < 0 || x > 240) ? 0 : (u8)x, (x < 0 || x > 240 || y < -16 || y > 212) ? 220 : (u8)y,
                       MB_CELL_PAT(c));
        sl++;
    }
    coldirty = 0;
}

/* 自機弾との当たり。威力は弾が持つ(b->hp, 半分単位)。3段目の貫通弾は1発につき1回だけ当たる。 */
static void hit_test(void) {
    u8 i;
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        dx = (s16)(e->x - bx - 24); if (dx < 0) dx = -dx; if (dx >= 24) continue;
        dy = (s16)(e->y - by - 24); if (dy < 0) dy = -dy; if (dy >= 22) continue;
        if (e->ax == (s16)MB_PIERCE) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)MB_PIERCE;
        hp = (hp > e->hp) ? (u16)(hp - e->hp) : 0;
        if (!flash) coldirty = 1;
        flash = 2;
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
}

static void shoot(void) {
    s16 ox = (s16)(bx + 24), oy = (s16)(by + 24);
    u8 a;
    if (--fire_t) return;
    fire_t = 40;
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
    if (flash && !--flash) coldirty = 1;
    if (st != ST_DIE) {   /* 旋回の中心は自機の横位置へゆっくり寄せる(64 ドット幅が画面の左右に収まる範囲) */
        s16 tx = (s16)(g_player_x + 8);
        if (tx < 112) tx = 112; else if (tx > 144) tx = 144;
        if (cx < tx) cx++; else if (cx > tx) cx--;
    }
    switch (st) {
    case ST_ENTER:
        by += 2; bx = (s16)(cx - r - 32); tgt = 16;
        if (by + 32 >= cy) st = ST_ORBIT;
        hit_test();
        break;
    case ST_ORBIT: {
        u8 a;
        phi -= MB_SPIN;
        if (r > MB_R1 && (t & 15) == 0) r--;
        a = (u8)(((phi + 128) >> 8) & 63);
        bx = (s16)(cx + (((s16)r * sin64[a]) >> 7) - 32);
        by = (s16)(cy - (((s16)r * sin64[(a + 16) & 63]) >> 7) - 32);
        tgt = (u8)(((((a - 16) & 63) + 1) >> 1) & 31);   /* 反時計回りの進行方向(64分割→32方向) */
        hit_test();
        shoot();
        if (t >= MB_TIMEOUT) st = ST_LEAVE;
        break; }
    case ST_LEAVE:                          /* 上へ向き直りながら上へ抜ける */
        tgt = 0;
        by -= 3;
        if (by < -72) st = ST_DONE;
        break;
    case ST_DIE:
        by++;
        if ((t & 3) == 0) {
            ent_spawn_explosion((s16)(bx + 8 + (rnd() & 31)), (s16)(by + 8 + (rnd() & 31)));
            if ((t & 7) == 0) sfx(2, SFX_BOOM);
        }
        if (((t >> 1) & 1) != flash) { flash = (u8)((t >> 1) & 1); coldirty = 1; }
        if (t >= 60) st = ST_DONE;
        break;
    }
    if (st == ST_DONE) {
        u8 sl;
        for (sl = g_spr_used; sl < 32; sl++) vdp_sprite_pos(sl, 0, 220, MB_CELL_PAT(0));
        ent_spr_cache_inval((u8)(32 - g_mb_n));
        g_mb_n = 0;
        g_mb = MB_RESTORE;
        return;
    }
    if ((st == ST_ENTER || st == ST_ORBIT) && hp == 0) {   /* 撃墜 */
        u16 pts = (u16)(500 + (MB_TIMEOUT - t) / 3);    /* 残り1秒=10点 */
        st = ST_DIE; t = 0;
        g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
        scorepop_add((s16)(bx + 24), (s16)(by + 24), pts);
        if (g_crush < CRUSH_MAX) g_crush++;
        g_hitstop = 4; g_shake = 8;
        shock_at((s16)(by + 32));
        sfx(2, SFX_BOOM);
    }
    turn_to(tgt);
    if (fcur != flast) {
        u8 n = 0; u16 m;
        u16 row = (u16)(MB_VRAM_Y + ((u16)fcur << 2));
        flast = fcur;
        vdp_copy(0, row, 0, MB_PAT_ROW_A, 256, 2);
        vdp_copy(0, (u16)(row + 2), 0, MB_PAT_ROW_B, 256, 2);
        mask = fw_mask[fcur];
        for (m = mask; m; m >>= 1) n += (u8)(m & 1);
        if (n != g_mb_n) {                  /* 枚数が変われば枠の割り当てがずれる=色を書き直し、エンティティ側のキャッシュも捨てる */
            ent_spr_cache_inval((u8)(32 - ((n > g_mb_n) ? n : g_mb_n)));
            g_mb_n = n;
        }
        coldirty = 1;
    }
    put_sprites();
}
