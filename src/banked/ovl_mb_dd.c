/* ovl_mb_dd.c — 3面の中ボス: フッドの護衛の駆逐艦(英 トライバル級, ROADMAP B4)。中ボス用オーバレイ(OVL11_BANK)で動く。
   ★1・2・4面は飛行機なので、3面は海面の艦=背景に描く相手(潜水艦案は「ゲームに潜水艦が出てこない」と指摘され駆逐艦に)。
   ★艦は page1 のリング(中ボス戦の間はスクロールが止まっている)の DD_Y0 行から1回だけ描き、**その帯(32..63 行)ごと
     走査線の分割で左右へずらして**動かす(R#26/R#27。描き直しゼロ)。海は模様がランダムなので帯ごとずらしても分からない。
     ほかの帯は荒天のうねりで左右に揺らす(R#27 だけ 0..6)。左端 8 ドットは R#25 の MSK で隠す。分割表は ovl_dd_split(slot14)。
   ★絵(tools/gen_dd.py, 176x24, 0=透明)は ROM(bank60 の後半)にあり、中ボスの仕組み(g_mb_req→常駐が MB_BUF へ 1KB)で読む。
     透明の画素に今の海を残すため、行ごとに VRAM から海を読み、重ねて書き戻す(登場の演出=2行ずつ現れる)。
     その行は SEA13(海の波の塗り直し)から外す(g_sea_skip)。
   ★中ボス共通の決まり: 被弾=艦が1フレーム白く光る(その後3フレームは光らせない)(甲板の色 9 番を中ボスの間だけ艦専用にし、パレットで白へ=g_dd_flash) /
     予告=斉射の前に白く明滅 / 手負い=煙突から黒煙・蛇行が速く / 撃墜=爆発して沈む / 影は無い(艦)。
   ★砲塔4基の砲身はスプライト(SPR_BARREL0 の8方向)で、自機の方を向いて撃つ。枠は末尾の4枚。
   ★衝撃波はこの中ボスの間は出ない(うねりの帯とぶつかる)。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"       /* emit / aim_dir */
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "sound.h"
#include "sprites.h"
#include "raster.h"     /* g_ras */
#include "aa_hot.h"     /* cam */
#include "scroll.h"     /* g_sea_skip / SC_SEATMPL_Y */
#include "midboss.h"

__sfr __at(0x98) DD_DAT;

extern u8 rnd(void);
extern const u8 barrel_col[16], barrel_flash[16];

#define DD_HP       280     /* 半分単位(通常弾 140発)。大きく当てやすいので飛行機より硬く */
#define DD_TIMEOUT  1200
#define DD_Y0       36      /* 絵の上端の画面行(帯 32..63 の中) */
#define DD_H        24
#define DD_ROWB     88      /* 1行のバイト数(176 ドット) */
#define DD_XB       20      /* リングへ描く左端(バイト)=40 ドット */
#define DD_X0       40
#define DD_BOW      160     /* 絵の中の艦首/艦尾(ドット) */
#define DD_STERN    10
#define DD_REQ0     4       /* 絵は mb_bank(60) の 4096 から=添字 4,5,6 */
#define DD_SLOT0    28      /* 砲身 4枚 */
#define DD_WARN_T   12
#define DD_PIERCE   0x7ABF

enum { ST_DRAW, ST_FIGHT, ST_WARN, ST_SINK, ST_DONE };

static const u8 tur_x[4] = { 129, 115, 33, 48 };   /* 砲塔の x(絵の左端から) */
static const u8 fun_x[2] = { 82, 67 };             /* 煙突 */

static u8 dcool;                                    /* 白く光った後、次の白を出さない残り */
u8 g_dd_flash;                                      /* >0: パレット 9(艦)を白に(ovl_palette.c の OVL_DD) */
static u8  st, st_t, hurt, row, fire_k, zig;
static u16 t, hp;
static s16 sx, vx;                                  /* 艦の帯の横のずれ(ドット)と速さ(1/16) */
static s16 sxq;
static u8  wph;                                     /* うねりの位相 */

void ovl_mb_init(void) {
    st = ST_DRAW; st_t = 0; t = 0; hp = DD_HP; hurt = 0; row = 0; fire_k = 0; zig = 0;
    sx = 0; sxq = 0; vx = 8; wph = 0; g_dd_flash = 0; dcool = 0;
    g_mb_n = 4;
    g_mb_req = DD_REQ0; g_mb_new = 0;
    vdp_wreg(25, 0x02);                            /* MSK: 左端 8 ドットを隠す(横にずらした帯の切れ目) */
    {   u8 r0 = (u8)(cam + DD_Y0), i;               /* 艦の行を海の波の塗り直しから外す */
        for (i = 0; i < DD_H; i += 8) g_sea_skip |= (u16)(1u << (((u8)(r0 + i)) >> 4));
        g_sea_skip |= (u16)(1u << (((u8)(r0 + DD_H - 1)) >> 4)); }
}

/* うねり: 帯 k(0..6)の右へのずれ 0..6 */
static const s8 sin8[8] = { 0, 2, 3, 2, 0, -2, -3, -2 };
static u8 wave(u8 k) { return (u8)(3 + sin8[(u8)((wph >> 3) + k * 3) & 7]); }

/* 右へ s ドットずらす R#26(8 ドット単位の左)/R#27(1 ドット単位の右) */
static void hs(u8 i, u8 line, s16 s, u8 r5) {
    u8 l = (u8)(-s), c = (u8)(((u16)l + 7) >> 3) & 31;
    g_ras[i].line = line;
    g_ras[i].reg = 26; g_ras[i].val = c; g_ras[i].reg2 = 27; g_ras[i].val2 = (u8)((c << 3) - l) & 7;
    g_ras[i].pidx = RAS_NOPAL;
    if (r5) { g_ras[i].reg = 5; g_ras[i].val = r5; }   /* 96 行はスプライト表Bへ(うねりは R#27 だけ) */
}

/* 分割表: 先頭(表A・うねり) / 32 行=艦の帯 / 64..192 行=うねり(96 行で表Bへ)。どれも 32 行おき(実機の 29 行の壁より広い) */
u8 ovl_dd_split(u8 split_line) {
    u8 k;
    (void)split_line;
    g_ras[0].line = 0; g_ras[0].reg = 5; g_ras[0].val = SPR_R5_A; g_ras[0].reg2 = 26; g_ras[0].val2 = 0; g_ras[0].pidx = RAS_NOPAL;
    g_ras[1].line = 0; g_ras[1].reg = 27; g_ras[1].val = wave(0); g_ras[1].reg2 = RAS_NOREG; g_ras[1].pidx = RAS_NOPAL;
    hs(2, 32, (s16)(sx + wave(1)), 0);
    for (k = 2; k < 7; k++) hs((u8)(k + 1), (u8)(k * 32), (s16)wave(k), (u8)((k == 3) ? SPR_R5_B : 0));
    return 8;
}

/* 絵を 2 行ずつリングへ(海と重ねる)。ROM の絵は 1 行 128B(88B 使用)=1KB に 8 行ちょうど。全部描けたら 1 */
static u8 draw_rows(void) {
    u8 n;
    for (n = 0; n < 2 && row < DD_H; n++) {
        u8 *sea = (u8 *)MB_SBUF, i;
        const u8 *p = (const u8 *)(MB_BUF + ((u16)(row & 7) << 7));
        u16 a = (u16)(((u16)(256 + (u8)(cam + DD_Y0 + row)) << 7) + DD_XB);
        if (!g_mb_new) return 0;                       /* 絵がまだ来ていない */
        vdp_read_addr(a);
        for (i = 0; i < DD_ROWB; i++) sea[i] = DD_DAT;
        vdp_write_addr(a);
        for (i = 0; i < DD_ROWB; i++) {
            u8 v = *p++, s = sea[i];
            DD_DAT = (u8)(((v & 0xF0) ? (v & 0xF0) : (s & 0xF0)) | ((v & 0x0F) ? (v & 0x0F) : (s & 0x0F)));
        }
        if ((++row & 7) == 0 && row < DD_H) { g_mb_new = 0; g_mb_req = (u8)(DD_REQ0 + (row >> 3)); }
    }
    if (row < DD_H) return 0;
    g_mb_new = 0;
    return 1;
}

static void flash(void) { if (!dcool) { g_dd_flash = 1; dcool = 4; } }   /* 白は1フレームだけ(当て続けてもチカチカ) */

static void shoot(u8 k, u8 spread) {
    s16 ox = (s16)(DD_X0 + tur_x[k] + sx - 8), oy = (s16)(DD_Y0 + DD_H / 2 - 8);
    u8 a = aim_dir(ox, oy, g_player_x, g_player_y);
    emit(ox, oy, a, 2, 3);
    if (spread) { emit(ox, oy, (u8)(a - 2), 2, 3); emit(ox, oy, (u8)(a + 2), 2, 3); }
}

static void hit_test(void) {
    u8 i;
    s16 x0 = (s16)(DD_X0 + DD_STERN + sx), x1 = (s16)(DD_X0 + DD_BOW + sx);
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 bx, by;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        bx = (s16)(e->x + 8); by = (s16)(e->y + 8);
        if (bx < x0 || bx > x1 || by < DD_Y0 + 3 || by > DD_Y0 + DD_H - 3) continue;
        if (e->ax == (s16)DD_PIERCE) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)DD_PIERCE;
        hp = (hp > e->hp) ? (u16)(hp - e->hp) : 0;
        flash();
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
}

/* 砲身(スプライト): 自機の方へ向く8方向 */
static void put_barrels(u8 show) {
    u8 k;
    for (k = 0; k < 4; k++) {
        s16 x = (s16)(DD_X0 + tur_x[k] + sx - 8);
        u8 d = (u8)(((aim_dir(x, DD_Y0 + 4, g_player_x, g_player_y) + 2) >> 2) & 7);
        if (!show || x < 0 || x > 240) { vdp_sprite_pos((u8)(DD_SLOT0 + k), 0, 220, SPR_BARREL0); continue; }
        vdp_sprite_color_tab((u8)(DD_SLOT0 + k), g_dd_flash ? barrel_flash : barrel_col);
        vdp_sprite_pos((u8)(DD_SLOT0 + k), (u8)x, DD_Y0 + 4, (u8)(SPR_BARREL0 + (d << 2)));
    }
}

/* 艦の行を海へ戻す(沈む/去る)。1 フレーム 2 行、下(艦尾側でなく喫水の下)から */
static u8 erase_rows(void) {
    u8 n;
    for (n = 0; n < 2 && row; n++) {
        u8 ry = (u8)(cam + DD_Y0 + --row);
        vdp_copy(0, (u16)(SC_SEATMPL_Y + (ry & 15)), 0, (u16)(256 + ry), 256, 1);
    }
    return (u8)(row == 0);
}

static void finish(void) {
    g_sea_skip = 0;
    vdp_wreg(25, 0x00);
    vdp_set_hscroll(0, 0);
    g_dd_flash = 0;
    mb_finish();
}

void ovl_mb_frame(void) {
    if (st == ST_DONE) return;
    t++; wph++;
    if (g_dd_flash) g_dd_flash--;
    if (dcool) dcool--;
    switch (st) {
    case ST_DRAW:                                    /* 嵐の中から現れる(2行ずつ描く) */
        if (draw_rows()) { st = ST_FIGHT; st_t = 0; }
        break;
    case ST_FIGHT:                                   /* 左右へ蛇行しながら砲塔が1基ずつ撃つ。4回に1回は斉射(予告あり) */
        if (++st_t >= (hurt ? 24 : 40)) {
            st_t = 0;
            if (++zig & 3) { shoot(fire_k, 0); fire_k = (u8)((fire_k + 1) & 3); sfx(2, SFX_EFIRE); }
            else st = ST_WARN;
        }
        if (t >= DD_TIMEOUT) { st = ST_SINK; row = DD_H; }
        hit_test();
        break;
    case ST_WARN:                                    /* 斉射の予告: 白く明滅 → 4基から3方向ずつ */
        if ((++st_t & 3) == 1) { dcool = 0; flash(); }
        if (st_t >= DD_WARN_T) { u8 k; for (k = 0; k < 4; k++) shoot(k, 1); sfx(2, SFX_BOOM); st = ST_FIGHT; st_t = 0; }
        hit_test();
        break;
    case ST_SINK:                                    /* 沈む(撃沈)/嵐に消える(時間切れ) */
        if (hp == 0 && (t & 3) == 0) {
            ent_spawn_explosion((s16)(DD_X0 + DD_STERN + sx + (rnd() & 127)), (s16)(DD_Y0 - 4 + (rnd() & 15)));
            if ((t & 7) == 0) sfx(2, SFX_BOOM);
        }
        if ((t & 1) && erase_rows()) { st = ST_DONE; put_barrels(0); finish(); return; }
        break;
    }
    if (st != ST_DRAW) {                             /* 蛇行: 端で折り返す(手負いは倍速)。帯のずれ sx は -36..+36 */
        sxq += hurt ? (vx << 1) : vx;
        sx = (s16)(sxq >> 4);
        if (sx > 36) { sx = 36; sxq = 36 << 4; vx = -vx; }
        else if (sx < -36) { sx = -36; sxq = -36 << 4; vx = -vx; }
    }
    if (st == ST_FIGHT || st == ST_WARN) {
        if (hp && hp < DD_HP / 2) {                  /* 手負い: 煙突から黒煙(爆発を繰り返す) */
            if (!hurt) { hurt = 1; g_shake = 8; sfx(2, SFX_BOOM); }
            if ((t & 7) == 0) ent_spawn_explosion((s16)(DD_X0 + fun_x[(t >> 3) & 1] + sx - 8), (s16)(DD_Y0 - 6));
        }
        if (!hp) {                                   /* 撃沈 */
            u16 pts = (u16)(500 + (DD_TIMEOUT - t) / 3);
            st = ST_SINK; row = DD_H;
            g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
            scorepop_add((s16)(DD_X0 + 80 + sx), DD_Y0, pts);
            if (g_crush < CRUSH_MAX) g_crush++;
            g_hitstop = 4; g_shake = 8;
            sfx(2, SFX_BOOM);
        }
    }
    put_barrels((u8)(st == ST_FIGHT || st == ST_WARN));
}
