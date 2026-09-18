/* ovl_mb_pby.c — 2面の中ボス PBY カタリナ(飛行艇, ROADMAP B4)。中ボス用オーバレイ(OVL9_BANK)で動く。
   ★1面(Fw 200)との違いは「高さ」。ふだんは低空(自機と同じ高さ)で撃ってきて、弾が当たる。胴体に触れると被弾。
     しばらく経つか、撃たれてある程度傷むと高度を上げて逃げる。高い間は大きく見え、自機の弾は下を抜けて当たらない
     (当たらないと一目で分かるよう、機体を1色 PB_HIGH_COL で塗る)。高い所で位置を変え、白く明滅(予告)して自機へ降りてくる。
     ★最初は「高い所が常態で、降りてきた時だけ当たる」にしたが、1〜5面は雲の無い低空で戦う設定と逆なので反転した(ユーザー指摘)。
     低空では側面銃座から左右へ、機首から自機へ撃つ。
   ★大きさは高さで6段階(43〜60 ドット)×16方向。1件1024B(tools/gen_pby.py)。読み込みの仕組みは1面と同じ(midboss.h)。
     影は海面なので大きさ一定(32 ドット=2x2)。夕日で右下へ、高いほど離れる。向きごとの影は常駐が MB_SBUF へ一緒に読む。
   ★スプライトは末尾の最低優先枠に 重ね→本体→影 の順(合計18枚以下)。パターンは本体16=砲身/小さい艦載機、重ね2と影4=中くらいの艦載機。
   ★手負い(耐久半分): 右エンジンが燃え、速く・旋回が速く・昇降が速くなる。40秒で逃げる。撃墜で 500点＋残り時間＋メガクラッシュ1回。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"       /* emit / aim_dir */
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "sound.h"
#include "midboss.h"

extern u8 rnd(void);

#define PB_HP       160     /* 半分単位(通常弾 2)=通常弾で80発。当たる時間が短いので1面(200)より低く */
#define PB_TIMEOUT  1200    /* 40秒で逃げる */
#define PB_ALT_MAX  95      /* 高さ(0=海面すれすれ)。大きさの段階 = 高さ/16 */
#define PB_ALT_HIT  32      /* これより低いと弾が当たる */
#define PB_ALT_BODY 16      /* これより低いと胴体に触れて被弾 */
#define PB_WARN_T   12      /* 降下の予告(白く明滅) */
#define PB_LOW_T    150     /* 低空で戦う時間。これを過ぎるか PB_FLEE だけ傷むと逃げる */
#define PB_FLEE     40      /* 低空に居る間にこれだけ削られたら高度を上げて逃げる */
#define PB_HIGH_T   75      /* 高い所で位置を変える時間 */
#define PB_HIGH_COL 10      /* 高い間(当たらない)の1色。2面の夕焼けでは淡い黄(5,5,2)=夕日を浴びて光る */
#define PB_PIERCE   0x7ABD  /* 貫通弾に付ける印 */

enum { ST_ENTER, ST_HIGH, ST_DIVE, ST_LOW, ST_CLIMB, ST_LEAVE, ST_DIE, ST_DONE };

/* 16方向の速度(1/4 ドット単位で 2 ドット/フレーム。0=上, 時計回り) */
static const s8 vx16[16] = { 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3 };
static const s8 vy16[16] = { -8, -7, -6, -3, 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7 };

static u8  st, st_t, dir, alt, flast, flash, coldirty, hurt, hi;
static u16 t, hp, hp_low;   /* hp_low=低空に降りた時の耐久(逃げる判定) */
static s16 qx, qy;          /* 機体の中心(1/4 ドット単位) */

void ovl_mb_init(void) {
    st = ST_ENTER; t = 0; hp = PB_HP; hp_low = PB_HP; alt = 0; hi = 0;
    qx = 128 << 2; qy = -48 << 2; dir = 8;
    flash = 0;  coldirty = 1; hurt = 0;
    flast = 8;
    g_mb_n = 0; g_mb_req = flast; g_mb_new = 0;   /* 最初の絵(低空・下向き)は常駐がすぐ読む */
}

static u8 aim16(s16 tx, s16 ty) {
    return (u8)(((aim_dir((s16)(qx >> 2), (s16)(qy >> 2), tx, ty) + 1) >> 1) & 15);
}

/* 向いている方へ進む(手負いは 1.5倍)。中心は画面の内側に留める */
static void fly(void) {
    qx += vx16[dir]; qy += vy16[dir];
    if (hurt) { qx += vx16[dir] >> 1; qy += vy16[dir] >> 1; }
    if (st == ST_LEAVE || st == ST_ENTER) return;
    if (qx < (16 << 2)) qx = 16 << 2; else if (qx > (240 << 2)) qx = 240 << 2;
    if (qy < (16 << 2)) qy = 16 << 2; else if (qy > (196 << 2)) qy = 196 << 2;
}

/* ent_draw_all の後に呼ぶ: 重ね→本体→影 の順に末尾の枠へ。エンティティとの間の枠は隠す。 */
static void put_sprites(void) {
    u8 c, j = 0, pass, sl, s0 = (u8)(32 - g_mb_n);
    s16 bx = (s16)((qx >> 2) - 32), by = (s16)((qy >> 2) - 32), off = (s16)((alt >> 2) + (alt >> 3));
    const u8 *col = (const u8 *)(MB_BUF + 708);
    for (sl = g_spr_used; sl < s0; sl++) vdp_sprite_pos(sl, 0, 220, MB_CELL_PAT(0));
    sl = s0;
    for (pass = 0; pass < 3; pass++) {
        u16 m = (pass == 0) ? g_mb_om : (pass == 1) ? g_mb_bm : 0x0660;   /* 影は 2x2(マス 5,6,9,10) */
        for (c = 0; c < 16; c++) {
            s16 x, y;
            u8 o;
            if (!(m & (1u << c))) continue;
            x = (s16)(bx + ((c & 3) << 4));
            y = (s16)(by + ((c >> 2) << 4));
            if (pass == 2) { x += off; y += off; }
            if (coldirty) {
                if (pass == 2) vdp_sprite_color(sl, 13);
                else if (flash) vdp_sprite_color(sl, 15);
                else if (hi) vdp_sprite_color(sl, PB_HIGH_COL);
                else vdp_sprite_color_tab(sl, col);
            }
            if (pass < 2) col += 16;
            o = (u8)(x < 0 || x > 240 || y < -16 || y > 212);
            vdp_sprite_pos(sl, o ? 0 : (u8)x, o ? 220 : (u8)y,
                           (pass == 0) ? MB_OV_PAT(j) : (pass == 1) ? MB_CELL_PAT(c) : MB_OV_PAT(j + 2));
            j++; sl++;
        }
        if (pass == 1) j = 0;
    }
    coldirty = 0;
}

/* 自機弾との当たり(低いときだけ)。高いときは弾が下を抜ける。 */
static void hit_test(void) {
    u8 i;
    Entity *e = ent_pool();
    if (alt >= PB_ALT_HIT) return;
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        dx = (s16)(e->x + 8 - (qx >> 2)); if (dx < 0) dx = -dx; if (dx >= 18) continue;
        dy = (s16)(e->y + 8 - (qy >> 2)); if (dy < 0) dy = -dy; if (dy >= 14) continue;
        if (e->ax == (s16)PB_PIERCE) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)PB_PIERCE;
        hp = (hp > e->hp) ? (u16)(hp - e->hp) : 0;
        if (!flash) coldirty = 1;
        flash = 2;
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
}

/* 低空: 側面銃座から左右へ1発ずつ、機首から自機へ1発 */
static void shoot(void) {
    s16 ox = (s16)((qx >> 2) - 8), oy = (s16)((qy >> 2) - 8);
    u8 d = (u8)(dir << 1);
    if (oy < 0 || oy > 176) return;
    emit(ox, oy, (u8)(d + 8), 2, 3);
    emit(ox, oy, (u8)(d + 24), 2, 3);
    emit(ox, oy, aim_dir(ox, oy, g_player_x, g_player_y), 2, 3);
    sfx(2, SFX_EFIRE);
}

void ovl_mb_frame(void) {
    u8 tgt = dir, dv = (u8)(hurt ? 3 : 2);
    s16 cx = (s16)(qx >> 2), cy = (s16)(qy >> 2), off = (s16)((alt >> 2) + (alt >> 3));
    s16 px = (s16)(g_player_x + 8), py = (s16)(g_player_y + 8);
    if (st == ST_DONE) return;
    t++;
    if (flash && !--flash) coldirty = 1;
    switch (st) {
    case ST_ENTER:                          /* 低空で画面の上端から入ってくる */
        fly();
        if (cy >= 40) { st = ST_LOW; st_t = 0; }
        hit_test();
        break;
    case ST_LOW:                            /* 低空で戦う。ゆっくり自機の方へ曲がりながら撃つ */
        if ((t & 7) == 0) tgt = aim16(px, py);
        fly();
        if ((++st_t & 15) == 8) shoot();   /* 16 フレームごと */
        if (st_t >= PB_LOW_T || (u16)(hp_low - hp) >= PB_FLEE) st = ST_CLIMB;
        hit_test();
        break;
    case ST_CLIMB:                          /* 高度を上げて逃げる(自機から横へ離れつつ上へ) */
        tgt = aim16((s16)(cx + cx - px), 24);
        fly();
        alt += dv;
        if (alt >= PB_ALT_MAX) { alt = PB_ALT_MAX; st = ST_HIGH; st_t = 0; }
        hit_test();
        break;
    case ST_HIGH:                           /* 高い所で、自機と反対側の上空へ回り込む */
        tgt = aim16((s16)(256 - px), 56);
        fly();
        if (++st_t >= PB_HIGH_T) { st = ST_DIVE; st_t = 0; }
        if (t >= PB_TIMEOUT) st = ST_LEAVE;
        break;
    case ST_DIVE:                           /* 予告(白く明滅)のあと自機へ降りてくる */
        tgt = aim16(px, py);
        fly();
        if (++st_t <= PB_WARN_T) { if ((st_t & 3) == 1) { flash = 2; coldirty = 1; } }
        else if (alt > dv) alt -= dv;
        else { alt = 0; st = ST_LOW; st_t = 0; hp_low = hp; }
        hit_test();
        break;
    case ST_LEAVE:                          /* 上へ向き直り、上がりながら抜ける */
        tgt = 0;
        fly();
        if (cy < -48) st = ST_DONE;
        break;
    case ST_DIE:                            /* 燃えながら海へ落ちる */
        if (alt) alt--;
        if ((t & 3) == 0) {
            ent_spawn_explosion((s16)(cx - 16 + (rnd() & 31)), (s16)(cy - 16 + (rnd() & 31)));
            if ((t & 7) == 0) sfx(2, SFX_BOOM);
        }
        if (((t >> 1) & 1) != flash) { flash = (u8)((t >> 1) & 1); coldirty = 1; }
        if (++st_t >= 60) st = ST_DONE;
        break;
    }
    if (st == ST_DONE) { mb_finish(); return; }
    if ((u8)(alt >= PB_ALT_HIT) != hi) { hi = (u8)(alt >= PB_ALT_HIT); coldirty = 1; }   /* 当たる/当たらないで色を切替 */
    if (st <= ST_CLIMB && st != ST_HIGH) {
        if (alt < PB_ALT_BODY) {            /* 胴体に触れたら被弾 */
            s16 dx = (s16)(cx - px), dy = (s16)(cy - py);
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx < 12 && dy < 12) ent_player_hit(g_player_x, g_player_y);
        }
        if (hp && hp < PB_HP / 2) {         /* 手負い: 右エンジンが燃える */
            if (!hurt) { hurt = 1; g_shake = 8; sfx(2, SFX_BOOM); }
            if ((t & 7) == 0) ent_spawn_explosion((s16)(cx - 8 + vx16[(dir + 4) & 15]), (s16)(cy - 8 + vy16[(dir + 4) & 15]));
        }
        if (!hp) {                          /* 撃墜 */
            u16 pts = (u16)(500 + (PB_TIMEOUT - t) / 3);
            st = ST_DIE; st_t = 0;
            g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
            scorepop_add((s16)(cx - 8), (s16)(cy - 8), pts);
            if (g_crush < CRUSH_MAX) g_crush++;
            g_hitstop = 4; g_shake = 8;
            shock_at(cy);
            sfx(2, SFX_BOOM);
        }
    }
    if (dir != tgt && (t & (hurt ? 1 : 3)) == 0) dir = (u8)((dir + ((((tgt - dir) & 15) < 8) ? 1 : 15)) & 15);
    {   /* 絵(大きさ×向き)が変わったら常駐へ読み込みを頼む。読み込み中は待つ */
        u8 idx = (u8)(((alt >> 4) << 4) + dir);
        if (idx != flast && g_mb_req == 0xFF && !g_mb_new) { flast = idx; g_mb_req = idx; }
    }
    if (g_mb_new) { mb_upload(320, 1); coldirty = 1; }   /* 常駐が読んだ絵をパターン表へ(midboss.h) */
    put_sprites();
}
