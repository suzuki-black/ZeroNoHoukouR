/* ovl_final.c — 最終面(巨大機 Douglas XB-19)。RAM オーバレイ OVL6_BANK で動く。設計は final.h。
   ★ボスは 24 枚のスプライト(表 B)。コマは tools/gen_boss.py が作り、面の準備で VRAM page2(y=528〜)へ
     焼いてある。コマを替えるときは HMMM 2 本(パターン 6 行→パターン表 / 色 3 行→表 B の色表)だけ。
     毎フレームの CPU 仕事は表 B の属性 24 枚ぶん(96B)を書くことだけ。
   ★弾は背景に描く(final_bgbul)。この面の背景は海しか無い＝**下に何があったかは計算で分かる**ので、
     消すときに VRAM を読み戻さず、RAM に写した海の模様を書き戻すだけで済む。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"
#include "sound.h"
#include "raster.h"
#include "scroll.h"
#include "player.h"
#include "sprites.h"
#include "aa_hot.h"     /* cam / aa_dead */
#include "final.h"
#define BOSS_FRAME_TABLES
#include "boss_frames.h"   /* 自動生成(tools/gen_boss.py) */

__sfr __at(0x98) FV_DAT;
__sfr __at(0x99) FV_CTRL;

static u8 prev_rect[30 * 4];   /* 背景弾の前回の矩形(pool の添字ごと)。オーバレイの static(0xEE00〜) */
#define PREV prev_rect
#define SEA_RAM   ((u8 *)0xB800)   /* 海テンプレ 256x16 の写し(128B×16行)。オーバレイ枠の末尾 2KB */
#define RG1SAV    ((volatile u8 *)0xF3E0)
#define SET_B_COL 0x7000           /* 表 B: 色表 / 属性表 */
#define SET_B_ATR 0x7200
#define BOSS_PAT0 160              /* ボス 24 枚のパターン番号の先頭(4 の倍数)。パターン表の 250〜255 行 */
#define WALL_PAT  72               /* 壁のパターン(空母の停泊機の枠。この面では使わない) */
#define HIDE_Y    213              /* 画面外(どの R#23 でも表示されない行) */

/* ---- VRAM 直アクセス(アドレス設定だけ di。データ 0x98 は割込みに壊されない) ---- */
static void wr_addr(u16 a) {
    __asm di __endasm;
    FV_CTRL = (u8)((a >> 14) & 3); FV_CTRL = 0x80 | 14;
    FV_CTRL = (u8)(a & 0xFF);      FV_CTRL = (u8)(((a >> 8) & 0x3F) | 0x40);
    __asm ei __endasm;
}

/* =====================================================================
   ボス
   ===================================================================== */
enum { ST_WAIT, ST_ENTRY, ST_FIGHT, ST_DEATH, ST_DONE };
static u8  st;
static u16 t0;          /* 曲の頭(snd_ticks)。登場はイントロ(576f)に合わせる */
static u8  fr, shown;   /* 出したいコマ / VRAM に載っているコマ */
static u8  glow, shown_glow, glow_t;   /* 1=被弾で全体を光らせる(色表を光る版に差し替え) */
static s16 cx, cy;      /* ボス中心(画面座標) */
static s16 tx;          /* 横移動の目標 */
static u8  tick, dtick;
static u8  r1n;         /* MAG を落とした R#1 */
static u8  wall_bits;   /* 組み上がった壁の枚(bit=左から) */
/* 壁を組む順(外側から内側へ)。1 枚ごとに「ガッ」と揺らす */
static const u8 wall_order[8] = { 0, 7, 1, 6, 2, 5, 3, 4 };
static Entity *wp[6];   /* 弱点(エンジン4＋銃座2)。当たり判定は既存の砲台と同じ仕組み */

/* 弱点の位置(通常コマ M72=表示 144x96 の中心から) */
static const s8 wp_dx[6] = { -22, -10, 11, 23, 0, 0 };
static const s8 wp_dy[6] = {  20,  20, 20, 20, -9, 24 };
static const u8 wp_hp[6] = {   8,   8,  8,  8, 12, 12 };   /* 実機で詰める(自機弾は1発=1) */
static const u8 fd_engine[] = { 64, 0, FIRE_AIMED, 2, 1, 3, FIRE_END };
static const u8 fd_turret[] = { 44, 0, FIRE_AIMFAN, 3, 1, 3, FIRE_END };

/* ★16x16 のパターンは「左半分の 16 行 → 右半分の 16 行」の順。上 8 行だけに絵(拡大で 16 ライン) */
static const u8 wall_pat[32] = {
    0xFF, 0x00, 0xFF, 0xDB, 0xFF, 0xFF, 0x00, 0xAA,  0,0,0,0,0,0,0,0,   /* 左半分 */
    0xFF, 0x00, 0xFF, 0xDB, 0xFF, 0xFF, 0x00, 0xAA,  0,0,0,0,0,0,0,0,   /* 右半分 */
};
static const u8 wall_col[8] = { 15, 14, 4, 14, 4, 5, 13, 5 };

static u8 boss_mag(void) { return boss_fmag[fr]; }

/* ボスのスプライトが HUD 帯にも壁の行にも掛からず、左端が画面外へ出ない範囲へ収める */
static void clamp_cy(void) {
    u8 z = boss_mag() ? 2 : 1;
    s16 xl = (s16)(-(s16)boss_fleft[fr] * z), xr = (s16)(256 - (s16)boss_fright[fr] * z);
    if (cx < xl) cx = xl;
    if (cx > xr) cx = xr;
    s16 lo = (s16)(FINAL_TOP_LINE + 4 - (s16)boss_ftop[fr] * z);
    s16 hi = (s16)(FINAL_WALL_LINE - 16 - (s16)boss_fbot[fr] * z);
    if (cy > hi) cy = hi;
    if (cy < lo) cy = lo;
}

/* コマを VRAM からパターン表と表 B の色表へ(HMMM 2 本。VDP が CPU と並行に流す) */
static void load_frame(void) {
    u16 y = (u16)(BOSS_VRAM_Y + (u16)fr * 9), cy6 = (u16)(y + 6);
    u8 g = 0;
    if (glow && fr >= BOSS_F_FULL && fr <= BOSS_F_BANKR) { g = 1; cy6 = (u16)(BOSS_GLOW_Y + (u16)(fr - BOSS_F_FULL) * 3); }
    if (fr == shown && g == shown_glow) return;
    if (fr != shown) vdp_copy(0, y, 0, 250, 256, 6);   /* パターン 24 枚 → 0x7D00(250〜255行) */
    vdp_copy(0, cy6, 0, 224, 256, 3);                   /* 色表 24 枚 → 0x7000(224〜226行)。光る版もここで差し替え */
    shown_glow = g;
    vdp_cmd_wait();   /* ★コピーが終わる前に位置を書くと、古い絵を新しい位置に出して崩れる(一度そうなった) */
    shown = fr;
}

/* 表 B の属性: ボス 24 枚＋壁 8 枚 */
static void put_sprites(u8 blink) {
    u8 i, n = boss_fn[fr], z = boss_mag() ? 2 : 1, vs = g_vscroll;
    const u8 *tl = boss_ftile[fr];
    s16 ox = (s16)(cx + (s16)boss_fox[fr] * z), oy = (s16)(cy + (s16)boss_foy[fr] * z);
    s16 step = (s16)(16 * z);
    wr_addr(SET_B_ATR);
    for (i = 0; i < 24; i++, tl++) {
        s16 x = (s16)(ox + (s16)(*tl >> 4) * step), y = (s16)(oy + (s16)(*tl & 15) * step);
        u8 yy;
        if (i >= n || blink || x < 0 || x > 255) { yy = (u8)(HIDE_Y + vs - 1); x = 0; }
        else yy = (u8)(y + vs - 1);
        if (yy == 216) yy = 215;
        FV_DAT = yy; FV_DAT = (u8)x; FV_DAT = (u8)(BOSS_PAT0 + (i << 2)); FV_DAT = 0;
    }
    for (i = 0; i < 8; i++) {                  /* 壁: 組み上がった枚だけ出す(8 枚×32px=画面幅)。撃墜で崩れる */
        u8 on = (u8)(z == 2 && st < ST_DEATH && (wall_bits & (u8)(1 << i)));
        u8 yy = (u8)((on ? (FINAL_WALL_LINE - 16) : HIDE_Y) + vs - 1);
        if (yy == 216) yy = 215;
        FV_DAT = yy; FV_DAT = (u8)(i << 5); FV_DAT = WALL_PAT; FV_DAT = 0;
    }
}

/* 分割表: HUD 帯(表A/等倍) → ボス帯(表B/コマに応じて拡大) → 自機帯(表A/等倍) */
static void arm_splits(void) {
    g_ras[0].line = 0;
    g_ras[0].reg = 5; g_ras[0].val = SPR_R5_A; g_ras[0].reg2 = 1; g_ras[0].val2 = r1n; g_ras[0].pidx = RAS_NOPAL;
    g_ras[1].line = FINAL_TOP_LINE;
    g_ras[1].reg = 5; g_ras[1].val = SPR_R5_B; g_ras[1].reg2 = 1; g_ras[1].val2 = (u8)(r1n | boss_mag());
    g_ras[1].pidx = RAS_NOPAL;
    g_ras[2].line = FINAL_WALL_LINE;
    g_ras[2].reg = 5; g_ras[2].val = SPR_R5_A; g_ras[2].reg2 = 1; g_ras[2].val2 = r1n; g_ras[2].pidx = RAS_NOPAL;
    raster_arm(3);
}

static void spawn_weakpoints(void) {
    u8 i;
    for (i = 0; i < 6; i++) {
        Entity *e = ent_spawn(ET_TURRET);
        wp[i] = e;
        if (!e) continue;
        g_lturret++;
        e->hp = wp_hp[i];
        e->hidden = 1;            /* 当たり判定だけの実体(絵はボスのスプライト) */
        e->color = 0xFE;          /* ★炎上 BG の焼き込み(fire_draw)の対象外にする印 */
        e->fire = (i < 4) ? fd_engine : fd_turret;
        e->ftimer = (u8)(20 + i * 9);
        e->vx = 4;
        e->h = 0;                 /* ★砲台は h を「命中フラッシュの残り」に使う(既定値は当たり判定の高さ16) */
    }
}

static void place_weakpoints(void) {
    u8 i;
    for (i = 0; i < 6; i++) {
        Entity *e = wp[i];
        if (!e || !e->active) continue;
        e->ax = (s16)(cx + wp_dx[i] - 8);
        e->ay = (s16)(cy + wp_dy[i] - 8 + (s16)cam);
    }
}

/* 全方位の下半分へ撃つ輪(ボスの見せ場) */
static void ring_volley(void) {
    u8 d;
    for (d = 6; d <= 26; d = (u8)(d + 2)) emit((s16)(cx - 8), (s16)(cy + 12), d, 0, 2);
    sfx(1, SFX_EFIRE);
}

void ovl_final_init(void) {
    u8 i;
    /* 海テンプレ(page2 y=512 = 0x10000)を RAM へ。R#14=4 */
    __asm di __endasm;
    FV_CTRL = 4; FV_CTRL = 0x80 | 14;
    FV_CTRL = 0; FV_CTRL = 0;
    __asm ei __endasm;
    { u8 *p = SEA_RAM; u16 k; for (k = 0; k < 2048; k++) *p++ = FV_DAT; }

    /* 壁のパターンと色(表 B の slot 24〜31) */
    vdp_sprite_pattern(WALL_PAT, wall_pat);
    wr_addr((u16)(SET_B_COL + 24 * 16));
    for (i = 0; i < 8; i++) { u8 r; for (r = 0; r < 16; r++) FV_DAT = (r < 8) ? wall_col[r] : 0; }

    r1n = (u8)(*RG1SAV & 0xFE);
    for (i = 0; i < SHIP_NAAG; i++) aa_dead[i] = 1;   /* 対空砲は無い(クリア判定を砲台だけにする) */
    st = ST_WAIT; t0 = 0; fr = 0; shown = 0xFF; glow = 0; shown_glow = 0; glow_t = 0; wall_bits = 0;
    cx = 208; cy = 44; tx = 128; tick = 0; dtick = 0;
    for (i = 0; i < 6; i++) wp[i] = (Entity *)0;
    g_py_min = FINAL_WALL_LINE;
    { u8 *q = PREV; for (i = 0; i < ENT_MAX * 4; i++) *q++ = 0; }   /* 背景弾の前回位置 */
}

u8 ovl_final_frame(void) {
    u16 el;
    u8 blink = 0;
    if (tick == 0 && st == ST_WAIT) t0 = g_bgm_t0;   /* ★曲を鳴らし始めた瞬間(登場はイントロ 576f に合わせる) */
    el = (u16)(snd_ticks - t0);

    /* 前進: 1px/f。カメラが小さくなったら 512 巻き戻す(リング上の位置は同じ＝描き直しゼロ) */
    cam--;
    if (cam < 64) { cam = (u16)(cam + 512); scroll_rebase(512); }
    scroll_to(cam);
    g_scroll_dy = 0;          /* ★敵弾を海と一緒に流さない(撃っているのは空のボス) */

    tick++;
    if (st == ST_WAIT) {
        if (el >= 96) st = ST_ENTRY;
    }
    if (st == ST_ENTRY) {
        u16 p = (u16)(el - 96);                       /* 0..480 */
        if (el < 384)      fr = (u8)(p * 7 / 288);   /* 等倍 7 コマ */
        else if (el < 552) fr = (u8)(BOSS_F_FIRSTMAG + (el - 384) * 5 / 168);
        else               fr = BOSS_F_FULL;
        if (fr > BOSS_F_FULL) fr = BOSS_F_FULL;
        if (p > 480) p = 480;
        cx = (s16)(208 - (s16)(p / 6));               /* 208 → 128 */
        cy = (s16)(44 + (s16)(p / 15));               /* 44 → 76 */
        /* ★壁の構築: 拡大に切り替わる瞬間(小節頭 384f)から 6f ごとに 1 枚、外側から内側へ組む。
           音は最も深いノイズを 1 フレームおきに強弱させて「ががががが」(イントロに重ねてよい) */
        if (el >= 384) {
            u8 k = (u8)((el - 384) / 6);
            if (k < 8) {
                u8 b = (u8)(1 << wall_order[k]);
                if (!(wall_bits & b)) { wall_bits |= b; g_shake = 3; }
                if (!(tick & 15)) sfx(2, SFX_RUMBLE);
                g_rumble_lv = (tick & 1) ? 15 : 5;
            } else if (wall_bits != 0xFF) { wall_bits = 0xFF; g_rumble_lv = 0; }
            else g_rumble_lv = 0;
        }
        if (el >= 576) { st = ST_FIGHT; spawn_weakpoints(); tx = 128; }
    } else if (st == ST_FIGHT) {
        u8 i, hit = 0;
        /* 横移動。傾きは「目標まで 12px 以上ある間」だけ(着く直前に水平へ戻す＝コマの替え過ぎを防ぐ) */
        if (cx == tx) { if ((tick & 63) == 0) {
            s16 xl = (s16)(-(s16)boss_fleft[BOSS_F_BANKL] * 2), xr = (s16)(256 - (s16)boss_fright[BOSS_F_BANKR] * 2);
            tx = (s16)(xl + (s16)(rnd() % (u8)(xr - xl + 1))); } }
        else if (cx < tx) cx++;
        else cx--;
        { s16 d = (s16)(tx - cx);
          fr = (d > 12) ? BOSS_F_BANKR : (d < -12) ? BOSS_F_BANKL : BOSS_F_FULL; }
        { u8 b = (u8)((tick >> 3) & 7); cy = (s16)(74 + ((b < 4) ? b : (7 - b)) + 1); }
        place_weakpoints();
        for (i = 0; i < 6; i++) if (wp[i] && wp[i]->active && wp[i]->h) hit = 1;
        if (hit) glow_t = 8;                           /* ★被弾中は全体を光らせる(点滅はちらつきに見える)。 */
        else if (glow_t) glow_t--;                     /*   連射の間に消えないよう少し持たせる=光りっぱなしに見える */
        glow = (u8)(glow_t != 0);
        if ((tick % 150) == 75) ring_volley();
        if (g_lturret == 0) { st = ST_DEATH; dtick = 0; fr = BOSS_F_DEATH0; glow = 0; sfx(2, SFX_BOOM); }
    } else if (st == ST_DEATH) {
        dtick++;
        fr = (u8)(BOSS_F_DEATH0 + dtick / 6);
        cx++; cy = (s16)(cy + ((dtick & 1) ? 1 : 0));
        if ((dtick & 7) == 0) { sfx(2, SFX_BOOM); g_shake = 4; }
        if (fr >= BOSS_NF) { fr = BOSS_NF - 1; st = ST_DONE; }
    }

    if (st == ST_ENTRY || st == ST_FIGHT) clamp_cy();
    if (st == ST_WAIT) {
        put_sprites(1);
    } else {
        load_frame();
        put_sprites(blink);
    }
    arm_splits();
    return (u8)(st == ST_DONE);
}

/* =====================================================================
   弾を背景に描く
   ===================================================================== */
/* 前回描いた矩形(pool の添字ごと): [0]=リング行 [1]=先頭バイト [2]=バイト数 [3]=行数(0=無し) */


/* 形: 5面までのスプライトと同じドット(sprites のパターンをそのまま写した)。行ごとのマスクは左端が bit5、色は e->color の単色 */
typedef struct { u8 ox, oy, w, h; u8 m[12]; } Shape;
static const Shape sh_bullet = { 5, 5, 6, 6,  { 0x3F,0x3F,0x3F,0x3F,0x3F,0x3F } };                                /* SPR_BULLET 6x6 */
static const Shape sh_pbul   = { 7, 4, 2, 8,  { 0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30 } };                      /* SPR_PBULLET 2x8 */
static const Shape sh_shell  = { 6, 2, 4, 12, { 0x0C,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x0C } };   /* SPR_EBSHELL 4x12 */

static void erase_one(u8 *pv) {
    u8 r, rr = pv[0], b0 = pv[1], nb = pv[2], h = pv[3];
    for (r = 0; r < h; r++, rr++) {
        const u8 *src = &SEA_RAM[((u16)(rr & 15) << 7) + b0];
        u8 k;
        wr_addr((u16)(0x8000 + ((u16)rr << 7) + b0));
        for (k = 0; k < nb; k++) FV_DAT = src[k];
    }
    pv[3] = 0;
}

static void draw_one(Entity *e, u8 *pv) {
    const Shape *s = (e->pat == SPR_PBULLET) ? &sh_pbul : (e->pat == SPR_EBSHELL) ? &sh_shell : &sh_bullet;
    s16 x0 = (s16)(e->x + s->ox), y0 = (s16)(e->y + s->oy);
    u8 r, b0, nb, rr0 = 0, first = 1, rows = 0, col = (u8)(e->color & 15);
    u8 buf[4];
    if (x0 < 0 || x0 > (s16)(256 - s->w)) return;
    b0 = (u8)(x0 >> 1);
    nb = (u8)(((x0 + s->w - 1) >> 1) - b0 + 1);
    for (r = 0; r < s->h; r++) {
        s16 y = (s16)(y0 + r);
        u8 rr, k, c, m = s->m[r];
        if (y < 0 || y >= 212) { if (!first) break; continue; }
        rr = (u8)(y + g_vscroll);
        if (first) { rr0 = rr; first = 0; }
        { const u8 *src = &SEA_RAM[((u16)(rr & 15) << 7) + b0]; for (k = 0; k < nb; k++) buf[k] = src[k]; }
        for (c = 0; c < s->w; c++) {
            if (m & (u8)(0x20 >> c)) {
                u16 px = (u16)(x0 + c);
                u8 bi = (u8)((px >> 1) - b0);
                if (px & 1) buf[bi] = (u8)((buf[bi] & 0xF0) | col);
                else        buf[bi] = (u8)((buf[bi] & 0x0F) | (u8)(col << 4));
            }
        }
        wr_addr((u16)(0x8000 + ((u16)rr << 7) + b0));
        for (k = 0; k < nb; k++) FV_DAT = buf[k];
        rows++;
    }
    pv[0] = rr0; pv[1] = b0; pv[2] = nb; pv[3] = rows;
}

void ovl_final_bgbul(void) {
    u8 i;
    Entity *e;
    u8 *pv = PREV;
    vdp_cmd_wait();                         /* ★海の行流し(HMMM)が終わってから直に書く */
    for (i = 0; i < ENT_MAX; i++, pv += 4) if (pv[3]) erase_one(pv);
    e = ent_pool(); pv = PREV;
    for (i = 0; i < ENT_MAX; i++, e++, pv += 4) {
        u8 ty = e->type;
        if (!e->active || (ty != ET_BULLET && ty != ET_AABURST)) continue;
        e->hidden = 1;                      /* スプライトでは描かせない */
        draw_one(e, pv);
    }
}
