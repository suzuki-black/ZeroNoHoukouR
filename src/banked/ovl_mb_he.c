/* ovl_mb_he.c — 4面の中ボス He 111 ×2(双子, ROADMAP B4)。中ボス用オーバレイ(OVL10_BANK)で動く。
   ★4面は双子艦なので中ボスも2機。2機は画面の中心(128,106)について**点対称**に動く(2機目の位置=中心について反転、向き=+180°)。
   ★1機 64x64 で2機ぶんのスプライトを出すため、106 行でスプライト表(R#5)と絵の表(R#6)を上下で切り替える。
     上の帯=表A(属性 0x7600 / 色 0x7400 / 絵 0x7800)に1機目、下の帯=表B(属性 0x7200 / 色 0x7000 / 絵 0x2000)に2機目。
     2機はいつも 106 行の上下に分かれる(1機目の中心 y は 28..70 に留める)。分割表は ovl_twin_shock(入口 slot14)が組む
     (この間は衝撃波のゆがみを出さない)。
     自機・弾・HUD は元から両方の表へ同じものを書いている(g_spr_dual)。絵の表Bは出現時に表Aを写し、その後の HUD の絵の
     書き換えは vdp_sprite_pattern が g_spr_patb で両方へ書く。宙返りのコマは ovl_rot(OVL_TWIN 版)が両方へ写す。
   ★動き: 1機目が「自機の点対称の位置」を狙って 8 方向で突っ込む(1面と同じ 向き直り→白い明滅の予告→突進)。
     2機目はその鏡写しなので、自機の居る下の帯を突っ切ってくる。向き直りの途中で2機とも自機を狙って撃つ=弾が交差する。
     最初は霧の中から浮かび上がる(機体の色を 霧の色→灰→本来の色 と変える)。
   ★スプライトは各帯の末尾 18 枠(14..31)に固定=重ね(最大2)→本体→影4。エンティティは 9..13。色は絵を読み込んだときだけ書く。
     予告の明滅や霧のあとは絵を読み直して色を戻す。
   ★影: 海面に 30 ドット(2x2)。低空なので右下へ HE_SH_OFF だけ。上の機の影も 106 行より上に収まる(中心 y≦70)。
     ★最初は朝霧を理由に省いたが「影が無い」と実機で指摘されて足した。
   ★中ボス共通の決まり(ROADMAP「中ボス共通の決まり」。面ごとに違うのは駄目=実機で指摘):
     被弾=当たった機だけ1フレーム白く光る＋火花＋音(その後3フレームは光らせない) / 予告=白く明滅 / 手負い(耐久半分)=エンジン炎上＋速く / 影は変えない。
     白くしたあと元の色へすぐ戻せるよう、各機の色を colbuf(オーバレイの後ろの RAM)に持つ。
   ★片方を落とすと残った1機が怒る(向き直り倍速・待ち半分・突進 4px/f)。45秒で上下へ逃げる。
     1機 300点、2機とも落とすと残り時間ボーナス＋メガクラッシュ1回。メガクラッシュの間は2機を隠す(下の帯の絵の表が合わないため)。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"       /* emit / aim_dir */
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "sound.h"
#include "raster.h"     /* g_ras */
#include "midboss.h"

__sfr __at(0x98) HE_DAT;

extern u8 rnd(void);

#define HE_HP       120     /* 1機ぶん(半分単位=通常弾 60発) */
#define HE_TIMEOUT  1350    /* 45秒で逃げる */
#define HE_AIM_T    30
#define HE_DASH_T   45
#define HE_WARN_T   12
#define HE_FOG_T    60      /* 霧から浮かび上がる(30 フレームずつ 霧の色→灰) */
#define HE_SLOT0    14      /* 各帯の中ボスの枠 14..31 */
#define HE_PIERCE   0x7ABE
#define HE_SH_OFF   12      /* 影を右下へずらす量 */
#define HE_COLBUF   0xBD00  /* 2機の機体の色(最大14枚×16B×2)。オーバレイ(0xA000〜)の後ろ=ovl10 は 7424B 以下であること(Makefile が検証) */

enum { ST_FOG, ST_AIM, ST_DASH, ST_LEAVE, ST_DONE };

static const s8 dx8[8] = { 0, 2, 3, 2, 0, -2, -3, -2 };
static const s8 dy8[8] = { -3, -2, 0, 2, 3, 2, 0, -2 };
static const s8 hx8[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const s8 hy8[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static u8  st, st_t, d8, fcur, hurt, cur, gone, fogc;   /* fogc=霧の間の機体の色 */   /* gone: bit0=上の機 / bit1=下の機 が落ちた */
static u8  hcool[2];                               /* 白く光った後、次の白を出さない残り */
static u8  ldir[2], dt[2], hf[2], nc[2];           /* hf=白く光っている残り / nc=機体(重ね＋本体)の枚数。その後ろ4枚が影 */                         /* 各機の VRAM に載っている向き / 墜落の残りフレーム */
static u16 t, hp[2], bm[2], om[2];
static s16 cx, cy;                                 /* 上の機(=動きの主)の中心 */

void ovl_mb_init(void) {
    st = ST_FOG; st_t = 0; t = 0; hurt = 0; gone = 0;
    hp[0] = hp[1] = HE_HP; dt[0] = dt[1] = 0; hf[0] = hf[1] = 0; hcool[0] = hcool[1] = 0; nc[0] = nc[1] = 0; bm[0] = bm[1] = om[0] = om[1] = 0;
    cx = 56; cy = 44; fcur = 8;                    /* 上の機は左上で右向き / 下の機は右下で左向き */
    ldir[0] = ldir[1] = 0xFF;
    vdp_copy(0, 240, 0, 64, 256, 16);              /* 絵の表A(0x7800)→表B(0x2000)。自機や弾の絵を下の帯でも使う */
    g_spr_patb = 1;
    cur = 0; g_mb_req = fcur; g_mb_new = 0;        /* 上の機の絵を常駐がすぐ読む */
    g_mb_n = 18;
}

/* 下の機の中心 */
#define BX() ((s16)(256 - cx))
#define BY() ((s16)(212 - cy))

static void turn_to(u8 tgt) {
    u8 d = (u8)((tgt - fcur) & 31);
    if (d && ((t & 1) || hurt)) fcur = (u8)((fcur + ((d < 16) ? 1 : 31)) & 31);
}

static u8 move(s8 vx, s8 vy) {
    s16 x = (s16)(cx + vx), y = (s16)(cy + vy);
    u8 out = 0;
    if (x < 24) { x = 24; out = 1; } else if (x > 232) { x = 232; out = 1; }
    if (y < 28) { y = 28; out = 1; } else if (y > 70) { y = 70; out = 1; }
    cx = x; cy = y;
    return out;
}

/* 自機の点対称の位置を狙う向きを決めて向き直りへ(=下の機が自機へ向かう) */
static void aim(void) {
    s16 tx = (s16)(256 - 8 - g_player_x), ty = (s16)(212 - 8 - g_player_y);
    d8 = (u8)(((aim_dir(cx, cy, tx, ty) + 2) >> 2) & 7);
    st = ST_AIM; st_t = 0;
}

/* 機 k の中心 */
static s16 kx(u8 k) { return k ? BX() : cx; }
static s16 ky(u8 k) { return k ? BY() : cy; }

/* 機 k の枠(14..31)へ、絵のあるマスを 重ね→本体→影 の順に。show=0 で全部隠す。 */
static void put(u8 k, u8 show) {
    u8 c, j = 0, pass, n = 0, hy = (u8)(220 + g_vscroll - 1);
    s16 bx = (s16)(kx(k) - 32), by = (s16)(ky(k) - 32);
    if (hy == 216) hy = 215;
    vdp_write_addr((u16)((k ? 0x7200 : 0x7600) + HE_SLOT0 * 4));
    if (show) for (pass = 0; pass < 3; pass++) {
        u16 m = (pass == 0) ? om[k] : (pass == 1) ? bm[k] : 0x0660;   /* 影は 2x2(マス 5,6,9,10) */
        if (pass == 2) { bx += HE_SH_OFF; by += HE_SH_OFF; j = 2; }
        for (c = 0; c < 16; c++) {
            s16 x, y;
            u8 yy;
            if (!(m & (1u << c))) continue;
            x = (s16)(bx + ((c & 3) << 4));
            y = (s16)(by + ((c >> 2) << 4));
            yy = (u8)(y + g_vscroll - 1);
            if (yy == 216) yy = 215;
            if (x < 0 || x > 240 || y < -16 || y > 212) { yy = hy; x = 0; }
            HE_DAT = yy; HE_DAT = (u8)x; HE_DAT = (pass == 1) ? MB_CELL_PAT(c) : MB_OV_PAT(j); HE_DAT = 0;
            j++; n++;
        }
    }
    for (; n < 18; n++) { HE_DAT = hy; HE_DAT = 0; HE_DAT = MB_CELL_PAT(0); HE_DAT = 0; }
}

/* 機 k の機体(影は除く)の色を書く。c=0 なら colbuf の本来の色、それ以外はその1色。帯ごとの色表へ直接書く。 */
static void paint(u8 k, u8 c) {
    const u8 *p = (const u8 *)(HE_COLBUF + (k ? 224 : 0));
    u16 i;
    vdp_write_addr((u16)((k ? 0x7000 : 0x7400) + HE_SLOT0 * 16));
    for (i = (u16)nc[k] << 4; i; i--) HE_DAT = c ? c : *p++;
}
static void flash(u8 k) { if (!hf[k] && !hcool[k]) { paint(k, 15); hf[k] = 1; hcool[k] = 4; } }   /* 白は1フレームだけ(次のフレームの頭で戻す) */

static void hit_test(u8 k) {
    u8 i;
    s16 pcx = kx(k), pcy = ky(k);
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        dx = (s16)(e->x + 8 - pcx); if (dx < 0) dx = -dx; if (dx >= 22) continue;
        dy = (s16)(e->y + 8 - pcy); if (dy < 0) dy = -dy; if (dy >= 18) continue;
        if (e->ax == (s16)(HE_PIERCE + k)) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)(HE_PIERCE + k);
        hp[k] = (hp[k] > e->hp) ? (u16)(hp[k] - e->hp) : 0;
        flash(k);
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
    {   /* 胴体に触れたら被弾 */
        s16 dx = (s16)(pcx - 8 - g_player_x), dy = (s16)(pcy - 8 - g_player_y);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 12 && dy < 12) ent_player_hit(g_player_x, g_player_y);
    }
}

static void shoot(u8 k) {
    s16 ox = (s16)(kx(k) - 8), oy = (s16)(ky(k) - 8);
    u8 a;
    if (oy < 0 || oy > 176) return;
    a = aim_dir(ox, oy, g_player_x, g_player_y);
    emit(ox, oy, (u8)(a - 1), 2, 3);
    emit(ox, oy, (u8)(a + 1), 2, 3);
}

void ovl_mb_frame(void) {
    u8 tgt = fcur, k, crush = g_crush_t;
    if (st == ST_DONE) return;
    t++;
    for (k = 0; k < 2; k++) {                     /* 前のフレームで白くした機を本来の色へ(白は1フレームだけ) */
        if (hf[k]) { hf[k] = 0; paint(k, 0); }
        if (hcool[k]) hcool[k]--;
    }
    switch (st) {
    case ST_FOG:                                   /* 霧から浮かび上がる: 霧の色 → 灰 → 本来の色(読み直し) */
        move(1, 0);
        if (st_t == 0) fogc = 2;                   /* 色は絵を読み込むたびに fogc で塗る(登場の瞬間はまだ絵が無い) */
        else if (st_t == HE_FOG_T / 2) { fogc = 4; paint(0, 4); paint(1, 4); }
        if (++st_t >= HE_FOG_T) { paint(0, 0); paint(1, 0); aim(); }   /* 本来の色へ */
        break;
    case ST_AIM: {
        u8 f8 = (u8)(((fcur + 2) >> 2) & 7);
        move(hx8[f8], hy8[f8]);
        tgt = (u8)(d8 << 2);
        if (++st_t == 12) { if (!(gone & 1)) shoot(0); if (!(gone & 2)) shoot(1); sfx(2, SFX_EFIRE); }
        if (st_t >= (hurt ? HE_AIM_T / 2 : HE_AIM_T) && fcur == tgt) { st = ST_DASH; st_t = 0; }
        if (t >= HE_TIMEOUT) st = ST_LEAVE;
        break; }
    case ST_DASH:                                  /* 予告(白く明滅)のあと、一直線に突っ込む */
        if (++st_t <= HE_WARN_T) {
            if ((st_t & 3) == 1 && !crush) { hcool[0] = hcool[1] = 0; flash(0); flash(1); }   /* 1フレーム白→戻る、を繰り返す=明滅 */
        } else if (move(dx8[d8], dy8[d8]) || (hurt && move(hx8[d8], hy8[d8])) || st_t >= HE_WARN_T + HE_DASH_T) aim();
        break;
    case ST_LEAVE:                                 /* 上の機は上へ、下の機は(鏡写しで)下へ抜ける */
        tgt = 0;
        cy -= 3;
        if (cy < -40) st = ST_DONE;
        break;
    }
    for (k = 0; k < 2; k++) {
        u8 b = (u8)(1 << k);
        if (gone & b) continue;
        if (dt[k]) {                               /* 墜落中: 爆発を出しながら消える */
            if ((t & 3) == 0) ent_spawn_explosion((s16)(kx(k) - 24 + (rnd() & 31)), (s16)(ky(k) - 24 + (rnd() & 31)));
            if ((t & 7) == 0) sfx(2, SFX_BOOM);
            if (!--dt[k]) { gone |= b; hurt = 1; }    /* 片方を落とされると残りが怒る */
            continue;
        }
        if (st != ST_FOG && st != ST_LEAVE && !crush) hit_test(k);
        if (hp[k] && hp[k] < HE_HP / 2) {          /* 手負い: 内側エンジンが燃え、動きが速くなる(共通の決まり) */
            u8 f8 = (u8)((((k ? fcur + 16 : fcur) + 2) >> 2) & 7), l8 = (u8)((f8 + 6) & 7);
            if (!hurt) { hurt = 1; g_shake = 8; sfx(2, SFX_BOOM); }
            if ((t & 7) == (k << 2))
                ent_spawn_explosion((s16)(kx(k) - 8 + dx8[f8] * 3 + dx8[l8] * 6), (s16)(ky(k) - 8 + dy8[f8] * 3 + dy8[l8] * 6));
        }
        if (!hp[k]) {                              /* 撃墜 */
            u16 pts = 300;
            dt[k] = 40;
            if (gone | dt[k ^ 1]) {                /* 2機目: 残り時間ボーナス(1秒=10点)＋メガクラッシュ1回 */
                pts = (u16)(pts + (HE_TIMEOUT - t) / 3);
                if (g_crush < CRUSH_MAX) g_crush++;
            }
            g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
            scorepop_add((s16)(kx(k) - 8), (s16)(ky(k) - 8), pts);
            g_hitstop = 4; g_shake = 8;
            shock_at(ky(k));
            sfx(2, SFX_BOOM);
        }
    }
    if (gone == 3 || st == ST_DONE) { g_spr_patb = 0; mb_finish(); return; }
    turn_to(tgt);
    if (g_mb_new) {                                /* 常駐が読んだ絵を、その機の絵の表へ。色も書く */
        const u8 *p = (const u8 *)(MB_BUF + 708);
        u16 i, n = 0;
        g_mb_pat_off = cur ? (u16)(MB_PATB - 0x7800) : 0;
        mb_upload(320, 1);                         /* 本体16＋重ね2＋影4 */
        g_mb_n = 18;
        bm[cur] = g_mb_bm; om[cur] = g_mb_om;
        for (i = bm[cur]; i; i >>= 1) n += (u16)(i & 1);
        for (i = om[cur]; i; i >>= 1) n += (u16)(i & 1);
        nc[cur] = (u8)n;
        {   u8 *q = (u8 *)(HE_COLBUF + (cur ? 224 : 0));   /* 本来の色を控える */
            for (i = n << 4; i; i--) *q++ = *p++; }
        paint(cur, (st == ST_FOG) ? fogc : hf[cur] ? 15 : 0);
        for (n = 64; n; n--) HE_DAT = 13;              /* 影4枚(いつも同じ色) */
    }
    if (g_mb_req == 0xFF && !g_mb_new) {   /* 向きが変わった機の絵を読みに行く(上の機が先) */
        u8 want0 = fcur, want1 = (u8)((fcur + 16) & 31);
        if (ldir[0] != want0) { cur = 0; ldir[0] = want0; g_mb_req = want0; }
        else if (ldir[1] != want1) { cur = 1; ldir[1] = want1; g_mb_req = want1; }
    }
    for (k = 0; k < 2; k++) put(k, (u8)(!(gone & (1 << k)) && !crush));
}

/* 分割表: 先頭=スプライト表A＋絵の表A / 106 行=スプライト表B＋絵の表B の2本だけ。
   ★4面の中ボスの間は衝撃波のゆがみ(R#23 の帯)を出さない。衝撃波の組み立て(ovl_shock)をこのオーバレイから外して
     枠を空けるため(8KB に収まらなかった)。画面の揺れとヒットストップは残る。R#23 は分割で動かさないので先頭で戻す必要も無い。 */
u8 ovl_twin_shock(u8 split_line) {
    (void)split_line;
    g_ras[0].line = 0;             g_ras[0].reg = 5; g_ras[0].val = SPR_R5_A; g_ras[0].reg2 = 6; g_ras[0].val2 = 0x0F; g_ras[0].pidx = RAS_NOPAL;
    g_ras[1].line = MB_TWIN_SPLIT; g_ras[1].reg = 5; g_ras[1].val = SPR_R5_B; g_ras[1].reg2 = 6; g_ras[1].val2 = MB_R6_B; g_ras[1].pidx = RAS_NOPAL;
    return 2;
}
