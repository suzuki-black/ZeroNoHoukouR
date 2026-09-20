/* coldsetup.c — 面の準備でしか使わない配置処理(bank30)。常駐リクレイムのため scene_stage.c から移設。
   ★中身は RAM(エンティティ・対空砲の表)を書くだけで、VRAM もバンク窓も触らない=バンクで動かせる。
     呼ぶのは stage_build から1回だけ(bcall_to(COLDSETUP_BANK))。
   ★面別の表(主砲の発砲スクリプト・耐久・対空砲の耐久)も一緒に持ってきた(常駐の節約)。 */
#include "types.h"
#include "entity.h"
#include "fire.h"        /* FIRE_* */
#include "gamestate.h"
#include "scroll.h"      /* SC_SHIP_R0 */
#include "ship.h"        /* SHIP_NAAG / g_shipargs */
#include "sprites.h"     /* SPR_BARREL0 / SPR_HELLCAT / barrel_col */
#include "aa_hot.h"      /* aa_fire / aa_hp / aa_dead */
#include "assets_data.h" /* STAGE_COUNT */
#include "final.h"       /* STAGE_FINAL */

/* ★常駐(scene_stage.c)が持っているもの。面の準備で先に埋まっている */
extern u8  cur_gun_x[4];   /* 主砲4基の艦内x */
extern u16 cur_gun_y[4];   /* 同 y */
extern u8  cur_ctab[16];   /* この面の機体の行別色(停泊機の色) */

/* ★主砲の発砲スクリプトは**常駐**(scene_stage.c)に置く。ここ(バンク)に置くと砲台が持つポインタが
   バンクの窓(0xA000〜)を指し、窓が既定へ戻った瞬間に中身が変わって撃たなくなる(実機で「2面で全く撃ってこない」と指摘)。 */
extern const u8 *const fd_gun_stage[STAGE_COUNT];

/* 破壊物の耐久(半分単位。通常の自機弾は1発=2)。面が進むほど硬い。
   ★2面(空母)の主砲は小さな5インチ砲で、左前の砲は甲板の停泊機の列の奥にある(弾は停泊機に当たると消える)ので
     他より軟らかくした(48 では「やたら硬い」と実機で指摘)。 */
static const u8 gun_hp[STAGE_COUNT]    = { 40, 36, 56, 64, 80 };   /* 主砲4基(通常弾で20/18/28/32/40発) */
static const u8 aa_big_hp[STAGE_COUNT] = { 14, 16, 18, 22, 26 };   /* 大型対空砲14基 */
static const u8 aa_sml_hp[STAGE_COUNT] = {  8,  9, 10, 12, 14 };   /* 小型対空砲9基 */

static void spawn_turret(u8 shipX, u16 shipY, u8 delay) {
    Entity *e = ent_spawn(ET_TURRET);
    if (e) {
        g_lturret++;   /* ★生存砲台O(1)カウンタ(stage_buildで0初期化済み。当たり判定の撃破で--) */
        e->ax = (s16)shipX - 8;                /* 砲塔中心x→スプライト左上(中心x=shipX) */
        e->ay = (s16)(SC_SHIP_R0 * 16 + shipY) - 8;   /* 砲身スプライト(旋回中心=8,8)をドーム中心に合わせる */
        e->hp = gun_hp[curstage]; e->fire = fd_gun_stage[curstage]; e->ftimer = delay;
        e->vx = 4; e->vy = 0; e->h = 0;        /* 砲身の向き=下 / 旋回冷却 / 命中フラッシュ残 */
        e->pat = (u8)(SPR_BARREL0 + 4 * 4);    /* 可動砲身(下向き, BGドームに重なる) */
        e->coltab = barrel_col;                /* 金属シェード(行別カラー) */
    }
}

/* 空母(2面)の停泊F6F(甲板8機)。エンティティ(ET_PARKED)で艦上に静止=破壊/発艦できる。 */
static void spawn_parked(u8 shipX, u16 shipY) {
    Entity *e = ent_spawn(ET_PARKED);
    if (e) {
        e->ax = (s16)shipX - 8;                /* 艦上世界アンカー(中心=shipX,ship座標y) */
        e->ay = (s16)(SC_SHIP_R0 * 16 + shipY) - 8;
        e->pat = SPR_HELLCAT; e->coltab = cur_ctab; e->shadow = 1;
    }
}

void banked_entry(void) {
    u8 i;
    for (i = 0; i < SHIP_NAAG; i++) {          /* 対空砲の発射タイマと耐久 */
        u16 t = (u16)60 + (u16)i * 11;         /* ★u16で計算し255クランプ(u8のままだと高iで桁溢れ) */
        aa_fire[i] = (t > 255) ? 255 : (u8)t;
        aa_hp[i]   = (i < 14) ? aa_big_hp[curstage] : aa_sml_hp[curstage];
        aa_dead[i] = 0;
    }
    if (curstage == STAGE_FINAL) return;       /* 最終面に艦は無い(ボス/弱点はオーバレイ側) */
    spawn_turret(cur_gun_x[0], cur_gun_y[0], 30);
    spawn_turret(cur_gun_x[1], cur_gun_y[1], 45);
    spawn_turret(cur_gun_x[2], cur_gun_y[2], 60);
    spawn_turret(cur_gun_x[3], cur_gun_y[3], 75);
    if (curstage == 1) {                       /* 空母: 停泊F6F 8機を甲板へ(中央列x120×4＋左列x98×4) */
        for (i = 0; i < 4; i++) spawn_parked(120, (u16)(130 + i * 40));
        for (i = 0; i < 4; i++) spawn_parked(98,  (u16)(150 + i * 40));
    }
}
