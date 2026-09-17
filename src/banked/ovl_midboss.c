/* ovl_midboss.c — 1面の中ボス Fw 200 コンドル ×2(ROADMAP B4)。中ボス用オーバレイ(OVL8_BANK)で動く。
   ★見せ場は「64方向の回転」。コマは tools/gen_fw200.py が焼いた 64×128B を、面の開始で常駐(mb_bake)が
     page0 の MB_VRAM_Y 行から並べてある。向きが変わったフレームだけ、その1行を HMMM で
     スプライトパターン表の 247/248 行(=主砲の砲身 SPR_BARREL0..+28 の枠。ちょうど2機ぶん)へ写す。
   ★砲身の枠は戦艦が出るまで使わないので借りる。終わったら常駐が退避しておいた2行を書き戻す。
   ★スプライトは HUD の直後の宙返り4枠のさらに後ろから 4枚×2機。その間エンティティは12後ろから詰める(常駐)。
   ★動き: 左右に並んだ別々の円を、左の機は反時計回り・右の機は時計回りに、左右対称に回る(同じ円だとぶつかる=実機で指摘)。
     円の中心は MB_SEP ずつ左右に離し、半径(40→30)の2倍＋機体幅より離れているので2機は重ならない。
     2つの円の真ん中は自機の横位置へ寄っていく。側面銃座から自機狙いの3方向弾。
     40秒で残っている機は逃げる。1機につき 200点、2機とも落とすと残り時間ボーナス＋メガクラッシュ1回。
   ★2機とも居なくなったら g_mb=MB_RESTORE にするだけ。オーバレイの入れ替えと砲身の書き戻しは常駐が行う。 */
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

#define MB_HP       200     /* 1機ぶん。半分単位(通常弾 2)=通常弾で100発 */
#define MB_TIMEOUT  1200    /* 40秒で逃げる */
#define MB_SPIN     80      /* 旋回の角速度(64方向を 256 分割した単位/フレーム)=約200フレームで1周 */
#define MB_SEP      64      /* 左右の円の中心を真ん中から離す量 */
#define MB_R0       40      /* 半径の初期値。内側で2機が最も近づいても 2*MB_SEP - 2*MB_R0 - 32 = 16 ドット空く */
#define MB_R1       30      /* 半径の最小値(そのとき 36 ドット空く) */
#define MB_PIERCE   0x7ABC  /* 貫通弾に付ける印(同じ弾が毎フレーム当たらないように) */

enum { ST_WAIT, ST_ENTER, ST_ORBIT, ST_LEAVE, ST_DIE, ST_DONE };

typedef struct {
    u8  st, fcur, flast, flash, fire_t, r, t;
    u16 phi, hp;
    s16 bx, by;            /* 32x32 の左上(画面座標) */
} Plane;

static const s8 sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125, 122, 117, 112, 106, 98,
    90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122,
    -125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };
/* 行ごとの色: 画面に対して上から光が当たる(機体が回っても光の向きは変わらない)。上6行=淡灰/中=灰/下6行=暗灰 */
static const u8 col_top[16]   = { 14,14,14,14,14,14, 4,4,4,4,4,4,4,4,4,4 };
static const u8 col_bot[16]   = { 4,4,4,4,4,4,4,4,4,4, 5,5,5,5,5,5 };
static const u8 col_flash[16] = { 15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15 };

static Plane pl[2];
static Plane c;            /* いま処理している機(pl[k] を写して処理し、書き戻す。ポインタ経由より大幅に小さい) */
static u8  k;              /* その添字(0=反時計回り / 1=時計回り) */
static u16 t;              /* 中ボス戦の経過フレーム */
static s16 cx, cy;         /* 2つの円の真ん中 */
static s16 ox;             /* いま処理している機の円の中心X */

void ovl_mb_init(void) {
    u8 i;
    t = 0; cx = 128; cy = 88;
    for (i = 0; i < 2; i++) {
        Plane *q = &pl[i];
        q->st = ST_WAIT; q->t = 0; q->hp = MB_HP; q->r = MB_R0;
        q->by = -40; q->flast = 0xFF; q->flash = 0; q->fire_t = (u8)(60 + i * 25);   /* 撃つ瞬間はずらす */
        q->fcur = 32;
        q->phi = (u16)(i ? 16 : 48) << 8;   /* 入口=左の円の左端(反時計回り)/右の円の右端(時計回り)=外側。そこでの進行方向は真下 */
    }
}

static void turn_to(u8 tgt) {
    u8 d = (u8)((tgt - c.fcur) & 63);
    if (d) c.fcur = (u8)((c.fcur + ((d < 32) ? 1 : 63)) & 63);
}

static void hide(u8 slot0, u8 n) {
    u8 i;
    for (i = 0; i < n; i++) vdp_sprite_pos((u8)(slot0 + i), 0, 220, MB_PAT);
}

static void put_sprites(void) {
    u8 i, sl0 = (u8)(MB_SLOT + (k << 2)), pat0 = (u8)(MB_PAT + (k << 4));
    s16 x = c.bx;
    if (c.st == ST_WAIT || c.st == ST_DONE) { hide(sl0, 4); return; }
    if (x < 0) x = 0; else if (x > 224) x = 224;   /* X は負にできない(EC 不使用) */
    for (i = 0; i < 4; i++) {
        s16 y = (s16)(c.by + ((i & 2) ? 16 : 0));
        vdp_sprite_color_tab((u8)(sl0 + i), c.flash ? col_flash : ((i & 2) ? col_bot : col_top));
        vdp_sprite_pos((u8)(sl0 + i), (u8)(x + ((i & 1) ? 16 : 0)), (y < -16 || y > 212) ? 220 : (u8)y,
                       (u8)(pat0 + (i << 2)));
    }
}

/* 自機弾との当たり。威力は弾が持つ(b->hp, 半分単位)。3段目の貫通弾は1発につき1回だけ当たる。 */
static void hit_test(void) {
    u8 i;
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        dx = (s16)(e->x - c.bx - 8); if (dx < 0) dx = -dx; if (dx >= 13) continue;
        dy = (s16)(e->y - c.by - 8); if (dy < 0) dy = -dy; if (dy >= 13) continue;
        if (e->ax == (s16)(MB_PIERCE + k)) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)(MB_PIERCE + k);
        c.hp = (c.hp > e->hp) ? (u16)(c.hp - e->hp) : 0;
        c.flash = 2;
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
}

static void shoot(void) {
    s16 ox = (s16)(c.bx + 8), oy = (s16)(c.by + 8);
    u8 a;
    if (--c.fire_t) return;
    c.fire_t = 50;
    if (oy < 0 || oy > 176) return;
    a = aim_dir(ox, oy, g_player_x, g_player_y);
    emit(ox, oy, (u8)(a - 2), 2, 3);
    emit(ox, oy, a,           2, 3);
    emit(ox, oy, (u8)(a + 2), 2, 3);
    sfx(2, SFX_EFIRE);
}

static void step(void) {
    u8 tgt = c.fcur;
    if (c.flash) c.flash--;
    switch (c.st) {
    case ST_WAIT:
        c.st = ST_ENTER;
        return;
    case ST_ENTER:
        c.by += 2; c.bx = (s16)(k ? ox + c.r - 16 : ox - c.r - 16); tgt = 32;
        if (c.by + 16 >= cy) c.st = ST_ORBIT;
        hit_test();
        break;
    case ST_ORBIT: {
        u8 a;
        if (k) c.phi += MB_SPIN; else c.phi -= MB_SPIN;
        if (c.r > MB_R1 && (t & 15) == 0) c.r--;
        a = (u8)(((c.phi + 128) >> 8) & 63);
        c.bx = (s16)(ox + (((s16)c.r * sin64[a]) >> 7) - 16);
        c.by = (s16)(cy - (((s16)c.r * sin64[(a + 16) & 63]) >> 7) - 16);
        tgt = (u8)((k ? a + 16 : a - 16) & 63);   /* 進行方向(時計回り=+90° / 反時計回り=-90°) */
        hit_test();
        shoot();
        if (t >= MB_TIMEOUT) c.st = ST_LEAVE;
        break; }
    case ST_LEAVE:                          /* 上へ向き直りながら、向いている方へ飛び去る */
        tgt = 0;
        c.bx += (s16)(((s16)sin64[c.fcur] * 3) >> 7);
        c.by -= (s16)(((s16)sin64[(c.fcur + 16) & 63] * 3) >> 7);
        if (c.by < -40 || c.bx < -40 || c.bx > 260 || c.by > 230) c.st = ST_DONE;
        break;
    case ST_DIE:
        c.by++; c.t++;
        if ((c.t & 3) == 0) {
            ent_spawn_explosion((s16)(c.bx + (rnd() & 15)), (s16)(c.by + (rnd() & 15)));
            if ((c.t & 7) == 0) sfx(2, SFX_BOOM);
        }
        c.flash = (u8)((c.t >> 1) & 1);
        if (c.t >= 48) c.st = ST_DONE;
        break;
    default:
        return;
    }
    if ((c.st == ST_ENTER || c.st == ST_ORBIT) && c.hp == 0) {   /* 撃墜 */
        u16 pts = 200;
        c.st = ST_DIE; c.t = 0;
        if (pl[k ^ 1].st >= ST_DIE) {       /* 2機目も落とした: 残り時間ボーナス(1秒=10点)＋メガクラッシュ1回 */
            pts = (u16)(pts + (MB_TIMEOUT - t) / 3);
            if (g_crush < CRUSH_MAX) g_crush++;
        }
        g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
        scorepop_add((s16)(c.bx + 8), (s16)(c.by + 8), pts);
        g_hitstop = 4; g_shake = 8;
        shock_at((s16)(c.by + 16));
        sfx(2, SFX_BOOM);
    }
    turn_to(tgt);
    if (c.fcur != c.flast) {
        c.flast = c.fcur;
        vdp_copy(0, (u16)(MB_VRAM_Y + c.fcur), 0, (u16)(MB_PAT_LINE + k), 256, 1);
    }
}

void ovl_mb_frame(void) {
    s16 tx = (s16)(g_player_x + 8);
    t++;
    if (tx < 120) tx = 120; else if (tx > 136) tx = 136;   /* 2つの円の真ん中は自機の横位置へゆっくり寄せる(外側の機が画面外へ出ない範囲) */
    if (cx < tx) cx++; else if (cx > tx) cx--;
    for (k = 0; k < 2; k++) { ox = (s16)(k ? cx + MB_SEP : cx - MB_SEP); c = pl[k]; step(); put_sprites(); pl[k] = c; }
    if (!g_loop_t) hide(HUD_SLOTS, 4);     /* 宙返りの枠は空けておく */
    if (pl[0].st == ST_DONE && pl[1].st == ST_DONE) {
        hide(HUD_SLOTS, 12);
        g_mb = MB_RESTORE;
    }
}
