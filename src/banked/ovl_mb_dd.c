/* ovl_mb_dd.c — 3面の中ボス: フッドの護衛の駆逐艦 2 隻(英 トライバル級, ROADMAP B4)。中ボス用オーバレイ(OVL11_BANK)で動く。
   ★2 隻: 1 隻(176x24)では「スプライトで出せそうなほど小さい」と実機で指摘 → ユーザーの選択で同じ艦を 2 隻。
     上の艦(0)は 32..63 行の帯・艦首が右、下の艦(1)は 64..95 行の帯・**左右反転で艦首が左**。帯ごとに別々に蛇行する
     (2 隻が縦に並ぶ隊形)。
     下の艦は甲板の色を 9 ではなく 6 番(中ボスの間は使われていない)で描き、被弾の白をそれぞれの艦だけに出す(g_dd_flash / g_dd_flash2)。
     1 隻沈めるごとに 300 点、2 隻とも沈めると残り時間ボーナス＋メガクラッシュ1回。
   ★1・2・4面は飛行機なので、3面は海面の艦=背景に描く相手(潜水艦案は「ゲームに潜水艦が出てこない」と指摘され駆逐艦に)。
   ★艦は page1 のリング(中ボス戦の間はスクロールが止まっている)の DD_Y0 行から1回だけ描き、**その帯(32..63 行)ごと
     走査線の分割で左右へずらして**動かす(R#26/R#27。描き直しゼロ)。海は模様がランダムなので帯ごとずらしても分からない。
     ほかの帯は荒天のうねりで左右に揺らす(R#27 だけ 0..6)。左端 8 ドットは R#25 の MSK で隠す。分割表は ovl_dd_split(slot14)。
   ★絵(tools/gen_dd.py, 176x24, 0=透明)は ROM(bank60 の後半)にあり、中ボスの仕組み(g_mb_req→常駐が MB_BUF へ 1KB)で読む。
     透明の画素に今の海を残すため、行ごとに VRAM から海を読み、重ねて書き戻す(登場の演出=2行ずつ現れる)。
     その行は SEA13(海の波の塗り直し)から外す(g_sea_skip)。
   ★中ボス共通の決まり: 被弾=艦が1フレーム白く光る(その後3フレームは光らせない)(甲板の色 9 番を中ボスの間だけ艦専用にし、パレットで白へ=g_dd_flash) /
     予告=斉射の前に白く明滅 / 手負い=煙突から黒煙・蛇行が速く / 撃墜=爆発して沈む / 影は無い(艦)。
   ★砲塔4基の砲身はスプライト(SPR_BARREL0 の8方向)で、自機の方を向いて撃つ。枠は末尾の 8 枚(1 隻 4 枚)。
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

#define DD_HP       180     /* 1 隻あたり(半分単位=通常弾 90 発)。2 隻で 360(1 隻のときは 280) */
#define DD_TIMEOUT  1200
#define DD_H        24
#define DD_W        176
#define DD_ROWB     88      /* 1行のバイト数(176 ドット) */
#define DD_XB       20      /* リングへ描く左端(バイト)=40 ドット */
#define DD_X0       40
#define DD_BOW      160     /* 絵の中の艦首/艦尾(ドット) */
#define DD_STERN    10
#define DD_REQ0     4       /* 絵は mb_bank(60) の 4096 から=添字 4,5,6 */
#define DD_SLOT0    24      /* 砲身 4枚×2 隻 */
#define DD_WARN_T   12
#define DD_PIERCE   0x7ABF

enum { ST_DRAW, ST_RUN, ST_DONE };               /* 全体: 描いている / 戦っている / 終わり */
enum { SK_FIGHT, SK_WARN, SK_SINK, SK_GONE };     /* 1 隻ごと */

static const u8 dd_y[2]  = { 36, 68 };            /* 絵の上端の画面行(帯 32..63 / 64..95 の中) */
static const u8 tur_x[4] = { 129, 115, 33, 48 };  /* 砲塔の x(上の艦の絵の左端から。下の艦は DD_W-1-x) */
static const u8 fun_x[2] = { 82, 67 };            /* 煙突 */

u8 g_dd_flash, g_dd_flash2;                       /* >0: パレット 9(上の艦)/6(下の艦)を白に(ovl_palette.c の OVL_DD) */
static u8  st, row, dk;                           /* 描いている艦 dk とその行 row */
static u8  sk[2], st_t[2], hurt[2], fire_k[2], zig[2], dcool[2], er[2];
static u16 t, hp[2];
static s16 sx[2], sxq[2], vx[2];                  /* 帯の横のずれ(ドット)とその 1/16・速さ */
static s16 sxs[2];                                /* 分割表に実際に入れたずれ(うねり込み)。砲身のスプライトはこれに合わせる */
static u8  wph;                                   /* うねりの位相 */

void ovl_mb_init(void) {
    u8 k;
    st = ST_DRAW; t = 0; wph = 0; g_dd_flash = g_dd_flash2 = 0; dk = 0; row = 0;
    for (k = 0; k < 2; k++) {
        sk[k] = SK_FIGHT; hp[k] = DD_HP; hurt[k] = 0; fire_k[k] = 0; zig[k] = (u8)(k << 1); dcool[k] = 0; er[k] = 0;
        st_t[k] = (u8)(k * 20);                   /* 2 隻の砲撃をずらす(斉射も交互になる) */
        sx[k] = 0; sxq[k] = 0; vx[k] = k ? -11 : 8;
    }
    g_mb_n = 8;
    g_mb_req = DD_REQ0; g_mb_new = 0;
    vdp_wreg(25, 0x02);                           /* MSK: 左端 8 ドットを隠す(横にずらした帯の切れ目) */
    for (k = 0; k < 2; k++) {                     /* 艦の行を海の波の塗り直しから外す */
        u8 r0 = (u8)(cam + dd_y[k]), i;
        for (i = 0; i < DD_H; i += 8) g_sea_skip |= (u16)(1u << (((u8)(r0 + i)) >> 4));
        g_sea_skip |= (u16)(1u << (((u8)(r0 + DD_H - 1)) >> 4));
    }
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
    if (r5) { g_ras[i].reg = 5; g_ras[i].val = r5; }   /* 128 行はスプライト表Bへ(うねりは R#27 だけ。R#26 は 96 行の帯の 0 のまま) */
}

/* 分割表: 先頭(表A・うねり) / 32 行=上の艦の帯 / 64 行=下の艦の帯 / 96..192 行=うねり。どれも 32 行おき(実機の 29 行の壁より広い)。
   ★艦の帯は R#26(8 ドット単位)も使うので、次の 96 行の帯で R#26 を 0 に戻す(hs は R#26/R#27 を両方書く)。
   ★スプライト表B への切替(R#5)は 128 行へ移した(元は 96 行。1 本の分割で書けるレジスタは 2 つなので、R#26 を戻す 96 行とは
     同居できない)。中ボスの間は両方の表に同じ内容を写している(g_spr_dual)ので、切替の行が変わっても見た目は同じ。 */
u8 ovl_dd_split(u8 split_line) {
    u8 k;
    (void)split_line;
    g_ras[0].line = 0; g_ras[0].reg = 5; g_ras[0].val = SPR_R5_A; g_ras[0].reg2 = 26; g_ras[0].val2 = 0; g_ras[0].pidx = RAS_NOPAL;
    g_ras[1].line = 0; g_ras[1].reg = 27; g_ras[1].val = wave(0); g_ras[1].reg2 = RAS_NOREG; g_ras[1].pidx = RAS_NOPAL;
    for (k = 1; k < 7; k++) {
        s16 s = (s16)wave(k);
        if (k < 3) s = sxs[k - 1] = (s16)(s + sx[k - 1]);
        hs((u8)(k + 1), (u8)(k * 32), s, (u8)((k == 4) ? SPR_R5_B : 0));
    }
    return 8;
}

/* 絵を 2 行ずつリングへ(海と重ねる)。ROM の絵は 1 行 128B(88B 使用)=1KB に 8 行ちょうど。
   下の艦は左右反転(バイトを逆順に・上下の画素を入れ替え)し、甲板の 9 を 6 にする。沈んだ艦は描かない。全部描けたら 1 */
static u8 draw_rows(void) {
    u8 n;
    for (n = 0; n < 2; n++) {
        u8 *sea = (u8 *)MB_SBUF, i;
        const u8 *p;
        u16 a;
        while (dk < 2 && (row >= DD_H || sk[dk] >= SK_SINK)) { dk++; row = 0; if (dk < 2) { g_mb_new = 0; g_mb_req = DD_REQ0; return 0; } }
        if (dk >= 2) { g_mb_new = 0; return 1; }
        if (!g_mb_new) return 0;                       /* 絵がまだ来ていない */
        p = (const u8 *)(MB_BUF + ((u16)(row & 7) << 7));
        a = (u16)(((u16)(256 + (u8)(cam + dd_y[dk] + row)) << 7) + DD_XB);
        vdp_read_addr(a);
        for (i = 0; i < DD_ROWB; i++) sea[i] = DD_DAT;
        vdp_write_addr(a);
        for (i = 0; i < DD_ROWB; i++) {
            u8 v, s = sea[i], hi, lo;
            if (dk) { v = p[DD_ROWB - 1 - i]; hi = (u8)(v << 4); lo = (u8)(v >> 4);   /* 反転 */
                      if (hi == 0x90) hi = 0x60;
                      if (lo == 0x09) lo = 0x06; }
            else    { v = p[i]; hi = (u8)(v & 0xF0); lo = (u8)(v & 0x0F); }
            DD_DAT = (u8)((hi ? hi : (s & 0xF0)) | (lo ? lo : (s & 0x0F)));
        }
        if ((++row & 7) == 0 && row < DD_H) { g_mb_new = 0; g_mb_req = (u8)(DD_REQ0 + (row >> 3)); return 0; }
    }
    return 0;
}

static void flash(u8 k) {                          /* 白は1フレームだけ(当て続けてもチカチカ) */
    if (dcool[k]) return;
    if (k) g_dd_flash2 = 1; else g_dd_flash = 1;
    dcool[k] = 4;
}

static s16 tx(u8 k, u8 x) { return (s16)(DD_X0 + (k ? (u8)(DD_W - 1 - x) : x) + sx[k]); }   /* 絵の x → 画面の x */

static void shoot(u8 k, u8 j, u8 spread) {
    s16 ox = (s16)(tx(k, tur_x[j]) - 8), oy = (s16)(dd_y[k] + DD_H / 2 - 8);
    u8 a = aim_dir(ox, oy, g_player_x, g_player_y);
    emit(ox, oy, a, 2, 3);
    if (spread) { emit(ox, oy, (u8)(a - 2), 2, 3); emit(ox, oy, (u8)(a + 2), 2, 3); }
}

static void hit_test(void) {
    u8 i, k;
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 bx, by;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        bx = (s16)(e->x + 8); by = (s16)(e->y + 8);
        for (k = 0; k < 2; k++) {
            s16 x0, x1;
            if (sk[k] >= SK_SINK) continue;
            if (by < dd_y[k] + 3 || by > dd_y[k] + DD_H - 3) continue;
            x0 = tx(k, k ? DD_BOW : DD_STERN); x1 = tx(k, k ? DD_STERN : DD_BOW);
            if (bx < x0 || bx > x1) continue;
            if (e->ax == (s16)DD_PIERCE) break;
            if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)DD_PIERCE;
            hp[k] = (hp[k] > e->hp) ? (u16)(hp[k] - e->hp) : 0;
            flash(k);
            if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
            break;
        }
    }
}

/* 砲身(スプライト): 自機の方へ向く8方向 */
/* ★位置は分割表に入れたずれ(sxs=うねり込み・このフレームの画面に出る値)で置く。sx で置くと、うねり(0..6)と
     1 フレームの遅れのぶん砲塔の丸から横へずれた(下の艦で約 8 ドット。openMSX の画面で確認) */
static void put_barrels(void) {
    u8 k, j;
    for (k = 0; k < 2; k++) {
        u8 show = (u8)(st == ST_RUN && sk[k] < SK_SINK), y = (u8)(dd_y[k] + 4);
        u8 fl = k ? g_dd_flash2 : g_dd_flash;
        for (j = 0; j < 4; j++) {
            u8 sl = (u8)(DD_SLOT0 + (k << 2) + j);
            s16 x = (s16)(tx(k, tur_x[j]) - sx[k] + sxs[k] - 8);
            u8 d = (u8)(((aim_dir(x, y, g_player_x, g_player_y) + 2) >> 2) & 7);
            if (!show || x < 0 || x > 240) { vdp_sprite_pos(sl, 0, 220, SPR_BARREL0); continue; }
            vdp_sprite_color_tab(sl, fl ? barrel_flash : barrel_col);
            vdp_sprite_pos(sl, (u8)x, y, (u8)(SPR_BARREL0 + (d << 2)));
        }
    }
}

/* 艦 k の行を海へ戻す(沈む/去る)。1 フレーム 2 行、下(喫水の下)から。戻し終えたら 1 */
static u8 erase_rows(u8 k) {
    u8 n;
    for (n = 0; n < 2 && er[k]; n++) {
        u8 ry = (u8)(cam + dd_y[k] + --er[k]);
        vdp_copy(0, (u16)(SC_SEATMPL_Y + (ry & 15)), 0, (u16)(256 + ry), 256, 1);
    }
    return (u8)(er[k] == 0);
}

static void finish(void) {
    g_sea_skip = 0;
    vdp_wreg(25, 0x00);
    vdp_set_hscroll(0, 0);
    g_dd_flash = g_dd_flash2 = 0;
    mb_finish();
}

static void sink(u8 k) { sk[k] = SK_SINK; er[k] = DD_H; }

void ovl_mb_frame(void) {
    u8 k;
    if (st == ST_DONE) return;
    /* ★メガクラッシュ(津波)は稲妻を消すために背景を世界の正本から引き直す(scroll_repaint_all)。
       艦はリングへ直接描いてあるので**一緒に消える**(実機で「ボムで本体が消える」と指摘)。
       登場のときと同じ手順で描き直す(嵐の中からもう一度現れる絵になる)。沈みかけの艦は消えたままにする。 */
    if (g_mb_recol) {
        g_mb_recol = 0;
        if (st == ST_RUN) {
            for (k = 0; k < 2; k++) if (sk[k] == SK_SINK) { er[k] = 0; sk[k] = SK_GONE; }
            if (sk[0] != SK_GONE || sk[1] != SK_GONE) { st = ST_DRAW; dk = 0; row = 0; g_mb_req = DD_REQ0; g_mb_new = 0; }
        }
    }
    t++; wph++;
    if (g_dd_flash) g_dd_flash--;
    if (g_dd_flash2) g_dd_flash2--;
    if (st == ST_DRAW) {                             /* 嵐の中から現れる(2行ずつ描く) */
        if (draw_rows()) st = ST_RUN;
        put_barrels();
        return;
    }
    hit_test();
    for (k = 0; k < 2; k++) {
        if (dcool[k]) dcool[k]--;
        switch (sk[k]) {
        case SK_FIGHT:                               /* 左右へ蛇行しながら砲塔が1基ずつ撃つ。4回に1回は斉射(予告あり) */
            if (++st_t[k] >= (hurt[k] ? 24 : 40)) {
                st_t[k] = 0;
                if (++zig[k] & 3) { shoot(k, fire_k[k], 0); fire_k[k] = (u8)((fire_k[k] + 1) & 3); sfx(2, SFX_EFIRE); }
                else sk[k] = SK_WARN;
            }
            break;
        case SK_WARN:                                /* 斉射の予告: 白く明滅 → 4基から3方向ずつ */
            if ((++st_t[k] & 3) == 1) { dcool[k] = 0; flash(k); }
            if (st_t[k] >= DD_WARN_T) { u8 j; for (j = 0; j < 4; j++) shoot(k, j, 1); sfx(2, SFX_BOOM); sk[k] = SK_FIGHT; st_t[k] = 0; }
            break;
        case SK_SINK:                                /* 沈む(撃沈)/嵐に消える(時間切れ) */
            if (hp[k] == 0 && (t & 3) == 0) {
                ent_spawn_explosion((s16)(tx(k, k ? DD_BOW : DD_STERN) + (rnd() & 127)), (s16)(dd_y[k] - 4 + (rnd() & 15)));
                if ((t & 7) == 0) sfx(2, SFX_BOOM);
            }
            if ((t & 1) && erase_rows(k)) sk[k] = SK_GONE;
            break;
        }
        if (sk[k] == SK_GONE) continue;
        {   /* 蛇行: 端で折り返す(手負いは倍速)。帯のずれ sx は -36..+36 */
            sxq[k] += hurt[k] ? (vx[k] << 1) : vx[k];
            sx[k] = (s16)(sxq[k] >> 4);
            if (sx[k] > 36) { sx[k] = 36; sxq[k] = 36 << 4; vx[k] = -vx[k]; }
            else if (sx[k] < -36) { sx[k] = -36; sxq[k] = -36 << 4; vx[k] = -vx[k]; }
        }
        if (sk[k] >= SK_SINK) continue;
        if (hp[k] && hp[k] < DD_HP / 2) {            /* 手負い: 煙突から黒煙(爆発を繰り返す) */
            if (!hurt[k]) { hurt[k] = 1; g_shake = 8; sfx(2, SFX_BOOM); }
            if ((t & 7) == 0) ent_spawn_explosion((s16)(tx(k, fun_x[(t >> 3) & 1]) - 8), (s16)(dd_y[k] - 6));
        }
        if (!hp[k]) {                                /* 撃沈: 1 隻 300 点。2 隻目で残り時間ボーナス＋メガクラッシュ */
            u16 pts = 300;
            sink(k);
            if (sk[k ^ 1] >= SK_SINK && hp[k ^ 1] == 0) {
                pts = (u16)(pts + (DD_TIMEOUT - t) / 3);
                if (g_crush < CRUSH_MAX) g_crush++;
            }
            g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
            scorepop_add((s16)(tx(k, 80)), dd_y[k], pts);
            g_hitstop = 4; g_shake = 8;
            sfx(2, SFX_BOOM);
        }
    }
    if (t >= DD_TIMEOUT) for (k = 0; k < 2; k++) if (sk[k] < SK_SINK) sink(k);
    if (sk[0] == SK_GONE && sk[1] == SK_GONE) { st = ST_DONE; put_barrels(); finish(); return; }
    put_barrels();
}
