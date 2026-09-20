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
#include "gamestate.h"  /* g_alert(敵大将発見の警報中はスクロールと登場を止める) */
#include "final.h"
#define BOSS_FRAME_TABLES
#include "boss_frames.h"   /* 自動生成(tools/gen_boss.py) */

__sfr __at(0x98) FV_DAT;
__sfr __at(0x99) FV_CTRL;

/* ★大きい作業域は固定番地に置く。static にするとオーバレイの static 帯
     (0xEE00〜0xEEFF)を超えて、直後の分割表(0xEF00)を壊した(画面の色やモードが崩れた。一度そうなった)。 */
#define META_RAM  ((u8 *)0xEC00)   /* 今のコマの位置情報 97B。★CPU 弾幕の固定帯(CBUL_ADDR)＝最終面では使わない */
#define PREV      ((u8 *)0xEC80)   /* 背景弾の前回の矩形(pool の添字ごと×4B=120B) */
#define SEA_RAM   ((u8 *)0xB800)   /* 海テンプレ 256x16 の写し(128B×16行)。オーバレイ枠の末尾 2KB */
#define RG1SAV    ((volatile u8 *)0xF3E0)
/* ★ダブルバッファ: パターン表と表 B を 2 組持ち、新しいコマは**表示していない組**へ書いてから、
   画面の頭(分割表の 0 行目＝VBLANK)でレジスタだけ切り替える。表示中にコピーすると、
   コマを替えた瞬間に 1 フレームだけ絵が崩れた(新しいパターンが古い位置に出る)。
     組0: パターン表 0x7800(R#6=0x0F) / 表B 色 0x7000・属性 0x7200(R#5=0xE7)
     組1: パターン表 0x1F800(R#6=0x3F) / 表B 色 0x0000・属性 0x0200(R#5=0x07)  ※page0 の火球ベイク域(この面は使わない) */
static const u16 pg_col[2]  = { 0x7000, 0x0000 };
static const u16 pg_atr[2]  = { 0x7200, 0x0200 };
static const u16 pg_patr[2] = { 250, 1018 };       /* ボス 24 枚のパターンが載る行(パターン番号 160〜) */
static const u16 pg_colr[2] = { 224, 0 };          /* 表 B の色表の行 */
static const u8  pg_r6[2]   = { 0x0F, 0x3F };
static const u8  pg_r5[2]   = { SPR_R5_B, 0x07 };
static u8 ap;              /* 表示中の組 */
static u8 pg_src[2];       /* 各組に載っているコマ(光るコマを含む添字。0xFF=未) */
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

/* 17bit 番地(組1のパターン表 0x1F800〜)への書き込みアドレス */
static void wr_addr17(u8 hi, u16 lo) {
    __asm di __endasm;
    FV_CTRL = (u8)(((lo >> 14) & 3) | (hi << 2)); FV_CTRL = 0x80 | 14;
    FV_CTRL = (u8)(lo & 0xFF);                    FV_CTRL = (u8)(((lo >> 8) & 0x3F) | 0x40);
    __asm ei __endasm;
}
/* 16x16 のパターンを両方のパターン表へ(影/壁など、両方の組で同じに見えるべきもの) */
static void pat_both(u8 pat, const u8 *d) {
    u8 i;
    vdp_sprite_pattern(pat, d);
    wr_addr17(1, (u16)(0xF800 + (u16)pat * 8));
    for (i = 0; i < 32; i++) FV_DAT = d[i];
}

/* =====================================================================
   ボス
   ===================================================================== */
enum { ST_WAIT, ST_ENTRY, ST_FIGHT, ST_DEATH, ST_DONE };
static u8  st;
static u16 t0;          /* 曲の頭(snd_ticks)。登場はイントロ(576f)に合わせる */
static u8  fr;          /* 出したいコマ(位置表の添字) */
static u8  glow, glow_t;   /* 1=被弾で全体を光らせる(明るい灰の専用コマへ差し替え) */
static s8  bl;          /* 傾きの段階 -3..3(左..右)。目標へ 1 段ずつ近づけて間を補間する */
static Entity *shadow;  /* 海面に落ちるボスの影(ET_SHOOTER=何もしない実体。描画だけ既存の仕組み) */
static u8  wp_php[6];   /* 弱点の前フレームの耐久(減ったら軽い被弾音) */
#define SPR_BOSS_SHADOW 56   /* 旧・艦載機の手描き枠(この面では使わない) */
#define SPR_BOSS_SHADOW_H 52 /* 主砲塔の枠(この面では使わない) */
#define SHADOW_SLOT 23       /* ボス帯の中の影は表 B の最後の 1 枚(ボスが 24 枚使うコマでは出さない) */
#define SPR_FAR_SHADOW  60   /* 最終面の自機の遠い影(entity.c の draw_shadow が使う) */
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
static const u8 wp_hp[6] = {  40,  40, 40, 40, 60, 60 };   /* 半分単位(最終面は通常の弾＝1発2)。エンジン20発・銃座30発。★「豆腐」と評価されたので 2.5 倍 */
static const u8 fd_engine[] = { 64, 0, FIRE_AIMED, 2, 1, 3, FIRE_END };
static const u8 fd_turret[] = { 44, 0, FIRE_AIMFAN, 3, 1, 3, FIRE_END };

/* ★16x16 のパターンは「左半分の 16 行 → 右半分の 16 行」の順。上 8 行だけに絵(拡大で 16 ライン) */
/* 上 6 行=壁 / 下 2 行=海に落ちる壁の影(網目で海を透かす) */
static const u8 wall_pat[32] = {
    0xFF, 0x00, 0xFF, 0xDB, 0xFF, 0xAA, 0x55, 0xAA,  0,0,0,0,0,0,0,0,   /* 左半分 */
    0xFF, 0x00, 0xFF, 0xDB, 0xFF, 0xAA, 0x55, 0xAA,  0,0,0,0,0,0,0,0,   /* 右半分 */
};
static const u8 wall_col[8] = { 15, 14, 4, 14, 5, 13, 13, 13 };

/* ★今のコマの位置情報(VRAM の各コマ 10 行目を、コマが替わったときだけ読む)。
   [0]=枚数 [1]=MAG [2]=原点x [3]=原点y [4]=上端 [5]=下端 [6]=左端 [7]=右端 [8..31]=格子 */
#define meta META_RAM
static u8 meta_fr;   /* ★初期値を書かない(初期値付き static はオーバレイの外=0xEE00 にデータを作る) */
static u16 frame_y(u8 f) {
    return (f < BOSS_VRAM_A_N) ? (u16)(BOSS_VRAM_Y + (u16)f * 10) : (u16)(BOSS_VRAM0_Y + (u16)(f - BOSS_VRAM_A_N) * 10);
}
static void read_meta(void) {
    u16 y = (u16)(frame_y(fr) + 9);
    u8 hi = (u8)((y >> 7) & 7), i;      /* VRAM 番地 = y*128 → R#14 = 番地>>14 = y>>7 */
    if (fr == meta_fr) return;
    __asm di __endasm;
    FV_CTRL = hi; FV_CTRL = 0x80 | 14;
    FV_CTRL = (u8)((y & 1) << 7); FV_CTRL = (u8)((y >> 1) & 0x3F);   /* 下位14bit = (y*128) & 0x3FFF */
    __asm ei __endasm;
    for (i = 0; i < 97; i++) meta[i] = FV_DAT;
    meta_fr = fr;
    pat_both(SPR_BOSS_SHADOW, meta + 32);      /* 影もコマの角度で回す(等倍で出す 16px) */
    pat_both(SPR_BOSS_SHADOW_H, meta + 64);    /* 同じ影の半分の解像度(拡大の帯で出すと 16px) */
}
static u8 boss_mag(void) { return meta[1]; }
static u8  shb_on;          /* 1=ボス帯の中に影を出す(表 B の SHADOW_SLOT) */
static s16 shb_x, shb_y;

/* ボスのスプライトが HUD 帯にも壁の行にも掛からず、左端が画面外へ出ない範囲へ収める */
static void clamp_cy(void) {
    u8 z = boss_mag() ? 2 : 1;
    s16 xl = (s16)(-(s16)(s8)meta[6] * z), xr = (s16)(256 - (s16)(s8)meta[7] * z);
    if (cx < xl) cx = xl;
    if (cx > xr) cx = xr;
    s16 lo = (s16)(FINAL_TOP_LINE + 4 - (s16)(s8)meta[4] * z);
    s16 hi = (s16)(FINAL_WALL_LINE - 16 - (s16)(s8)meta[5] * z);
    if (cy > hi) cy = hi;
    if (cy < lo) cy = lo;
}

/* コマを VRAM からパターン表と表 B の色表へ(HMMM 2 本。VDP が CPU と並行に流す) */
/* 今フレームに書く組を決める: コマが替わるなら表示していない組へ載せて切り替える。戻り=書く組 */
static u8 load_frame(void) {
    u8 src = fr, q;
    u16 y;
    if (glow && fr >= BOSS_F_FULL && fr < BOSS_F_BANK0 + 6)   /* 光るコマ: 通常,左1..3,右1..3 の順 */
        src = (u8)(BOSS_F_GLOW0 + ((fr == BOSS_F_FULL) ? 0 : (fr - BOSS_F_BANK0 + 1)));
    if (pg_src[ap] == src) return ap;          /* 替わらない: 表示中の組の位置だけ書く */
    q = (u8)(1 - ap);
    if (pg_src[q] != src) {
        /* コマの置き場: 前半は page2/3(y=528〜)、入り切らない残りは page0(y=32〜) */
        y = frame_y(src);
        vdp_copy(0, y, 0, pg_patr[q], 256, 6);             /* パターン 24 枚 */
        vdp_copy(0, (u16)(y + 6), 0, pg_colr[q], 256, 3);  /* 色表 24 枚 */
        vdp_cmd_wait();
        pg_src[q] = src;
        if (meta[0] <= SHADOW_SLOT) {             /* 空いた最後の 1 枚を影の色(ほぼ黒)に */
            u8 k;
            wr_addr((u16)(pg_col[q] + SHADOW_SLOT * 16));
            for (k = 0; k < 16; k++) FV_DAT = 13;
        }
    }
    ap = q;            /* 分割表の 0 行目(次の VBLANK)で R#6/R#5 がこの組へ切り替わる */
    return q;
}

/* 表 B の属性: ボス 24 枚＋壁 8 枚 */
static void put_sprites(u8 blink, u8 pg) {
    u8 i, n = meta[0], z = boss_mag() ? 2 : 1, vs = g_vscroll;
    const u8 *tl = meta + 8;
    s16 ox = (s16)(cx + (s16)(s8)meta[2] * z), oy = (s16)(cy + (s16)(s8)meta[3] * z);
    s16 step = (s16)(16 * z);
    wr_addr(pg_atr[pg]);
    for (i = 0; i < 24; i++, tl++) {
        s16 x = (s16)(ox + (s16)(*tl >> 4) * step), y = (s16)(oy + (s16)(*tl & 15) * step);
        u8 yy;
        u8 pat = (u8)(BOSS_PAT0 + (i << 2));
        if (i == SHADOW_SLOT && n <= SHADOW_SLOT && shb_on) {   /* 空いている最後の 1 枚＝ボス帯の中の影 */
            x = shb_x; y = shb_y; pat = z == 2 ? SPR_BOSS_SHADOW_H : SPR_BOSS_SHADOW;
            yy = (u8)(y + vs - 1);
        } else if (i >= n || blink || x < 0 || x > 255) { yy = (u8)(HIDE_Y + vs - 1); x = 0; }
        else yy = (u8)(y + vs - 1);
        if (yy == 216) yy = 215;
        FV_DAT = yy; FV_DAT = (u8)x; FV_DAT = pat; FV_DAT = 0;
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
    g_ras[0].line = 0;   /* 画面の頭: パターン表の組を切り替え(R#6)。表Aへ戻すのは前フレームの壁の行で済んでいる */
    g_ras[0].reg = 6; g_ras[0].val = pg_r6[ap]; g_ras[0].reg2 = 5; g_ras[0].val2 = SPR_R5_A; g_ras[0].pidx = RAS_NOPAL;
    g_ras[1].line = FINAL_TOP_LINE;
    g_ras[1].reg = 5; g_ras[1].val = pg_r5[ap]; g_ras[1].reg2 = 1; g_ras[1].val2 = (u8)(r1n | boss_mag());
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

    r1n = (u8)(*RG1SAV & 0xFE);
    for (i = 0; i < SHIP_NAAG; i++) aa_dead[i] = 1;   /* 対空砲は無い(クリア判定を砲台だけにする) */
    st = ST_WAIT; t0 = 0; fr = 0; meta_fr = 0xFF; ap = 0; pg_src[0] = pg_src[1] = 0xFF; glow = 0; glow_t = 0; wall_bits = 0; bl = 0;

    shadow = ent_spawn(ET_SHOOTER);
    if (shadow) { shadow->pat = SPR_BOSS_SHADOW; shadow->color = 13; shadow->hidden = 1; }
    cx = 208; cy = 44; tx = 128; tick = 0; dtick = 0;
    for (i = 0; i < 6; i++) wp[i] = (Entity *)0;
    g_py_min = FINAL_WALL_LINE;
    { u8 *q = PREV; for (i = 0; i < ENT_MAX * 4; i++) *q++ = 0; }   /* 背景弾の前回位置 */
}

u8 ovl_final_frame(void) {
    u16 el;
    u8 blink = 0;
    if (tick == 0 && st == ST_WAIT) {
        u8 i;
        t0 = g_bgm_t0;   /* ★曲を鳴らし始めた瞬間(登場はイントロ 576f に合わせる) */
        /* ★組1の表を用意する。page0 の上端(組1の色/属性)はカードが出ている間は見えるので、
           表示が page1 に切り替わった後(＝最初のフレーム)でないと書けない。 */
        vdp_copy(0, 240, 0, 1008, 256, 16);          /* パターン表を丸ごと組1へ(自機/弾/HUD の絵も両方に要る) */
        vdp_cmd_wait();
        pat_both(WALL_PAT, wall_pat);
        pat_both(SPR_FAR_SHADOW, player_far_shadow_pat);
        for (i = 0; i < 2; i++) {
            u16 k;
            wr_addr(pg_col[i]);
            for (k = 0; k < 24 * 16; k++) FV_DAT = 0;
            for (k = 0; k < 8 * 16; k++) FV_DAT = ((k & 15) < 8) ? wall_col[k & 15] : 0;   /* 壁 slot24〜31 */
        }
    }
    /* ★登場は曲の頭に合わせる。曲は「敵大将発見」の警報が終わってから鳴らす(scene_stage の段取り)ので、
       待っている間は曲の頭を取り直し続ける */
    if (st == ST_WAIT) t0 = g_bgm_t0;
    el = (u16)(snd_ticks - t0);

    /* 前進: 1px/f。カメラが小さくなったら 512 巻き戻す(リング上の位置は同じ＝描き直しゼロ)。
       ★警報の間は止める(警報の文字は表示リングに描いてある) */
    if (!g_alert) cam--;
    if (cam < 64) { cam = (u16)(cam + 512); scroll_rebase(512); }
    scroll_to(cam);
    g_scroll_dy = 0;          /* ★敵弾を海と一緒に流さない(撃っているのは空のボス) */

    tick++;
    if (st == ST_WAIT) {
        if (el >= 96 && !g_alert) st = ST_ENTRY;
    }
    if (st == ST_ENTRY) {
        u16 p = (u16)(el - 96);                       /* 0..480 */
        if (el < 384)      fr = (u8)(p * BOSS_NN / 288);                          /* 等倍コマ */
        else if (el < 552) fr = (u8)(BOSS_F_FIRSTMAG + (el - 384) * BOSS_NM / 168);  /* 拡大コマ */
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
                if (!(tick & 3)) sfx(2, SFX_RUMBLE);   /* ★16フレームごとだと壁が組み上がる0.8秒の間に1〜2回しか鳴らず
                                                          「がっ」1回に聞こえた(実機で指摘)。4フレームごとに詰めて「ががががが」に */
                g_rumble_lv = (tick & 1) ? 15 : 5;
            } else if (wall_bits != 0xFF) { wall_bits = 0xFF; g_rumble_lv = 0; }
            else g_rumble_lv = 0;
        }
        if (el >= 576) { u8 i; st = ST_FIGHT; spawn_weakpoints(); tx = 128; for (i = 0; i < 6; i++) wp_php[i] = wp_hp[i]; }
    } else if (st == ST_FIGHT) {
        u8 i, hit = 0;
        /* 横移動。傾きは「目標まで 12px 以上ある間」だけ(着く直前に水平へ戻す＝コマの替え過ぎを防ぐ) */
        if (cx == tx) { if ((tick & 63) == 0) {
            s16 xl = BOSS_XL3, xr = BOSS_XR3;
            tx = (s16)(xl + (s16)(rnd() % (u8)(xr - xl + 1))); } }
        else if (cx < tx) cx++;
        else cx--;
        /* 傾き: 目標までの距離で段階を決め、3 フレームに 1 段ずつ近づける(0→1→2→3 と間を補間) */
        { s16 d = (s16)(tx - cx);
          s8 want = (d > 24) ? 3 : (d > 14) ? 2 : (d > 4) ? 1 : (d < -24) ? -3 : (d < -14) ? -2 : (d < -4) ? -1 : 0;
          if ((tick % 3) == 0) { if (bl < want) bl++; else if (bl > want) bl--; }
          fr = (bl == 0) ? BOSS_F_FULL : (bl < 0) ? (u8)(BOSS_F_BANK0 - 1 - bl) : (u8)(BOSS_F_BANK0 + 2 + bl); }
        { u8 b = (u8)((tick >> 3) & 7); cy = (s16)(74 + ((b < 4) ? b : (7 - b)) + 1); }
        place_weakpoints();
        for (i = 0; i < 6; i++) {
            Entity *w = wp[i];
            if (!w || !w->active) continue;
            if (w->hp < wp_php[i]) { hit = 1; if (w->hp) sfx(2, SFX_HIT); }   /* ★毎発の軽い被弾音(壊した瞬間は既存の爆発音) */
            wp_php[i] = w->hp;
        }
        if (hit && !glow_t) glow_t = 4;                /* ★被弾した瞬間の1フレームだけ光る。その後3フレームは光らせない */
        else if (glow_t) glow_t--;                     /*   (以前は光りっぱなしにしていたが、もっと速く戻すよう指摘=中ボスと同じ) */
        glow = (u8)(glow_t == 4);
        if ((tick % 150) == 75) ring_volley();
        if (g_lturret == 0) { st = ST_DEATH; dtick = 0; fr = BOSS_F_DEATH0; glow = 0; glow_t = 0; sfx(2, SFX_BOOM); }
    } else if (st == ST_DEATH) {
        dtick++;
        fr = (u8)(BOSS_F_DEATH0 + dtick / 3);
        cx++; cy = (s16)(cy + ((dtick & 1) ? 1 : 0));
        if ((dtick & 7) == 0) { sfx(2, SFX_BOOM); g_shake = 4; }
        if (fr >= BOSS_F_DEATH0 + BOSS_ND) { fr = (u8)(BOSS_F_DEATH0 + BOSS_ND - 1); st = ST_DONE; }
    }
    /* 海面の影: 高空なので小さく(自機の 5 面までの影と同じ 16x16)、右下へ大きく離す。
       撃墜で海へ落ちていくほど本体へ近づく。 */
    read_meta();
    if (st == ST_ENTRY || st == ST_FIGHT) clamp_cy();
    {   /* ★影の位置は高度から: コマの表示幅が 16px(海面すれすれ)なら本体と重なり、大きく見えるほど右下へ離れる。
           離す量はコマごとに生成器が計算して meta[96] に入れてある。登場は離れていき、墜落では着水で重なる。 */
        s16 off, sx, sy;
        u8 vis = (u8)(st == ST_ENTRY || st == ST_FIGHT || st == ST_DEATH);
        off = meta[96];
        sx = (s16)(cx + (off >> 1) - 8); sy = (s16)(cy + off - 8);
        if (shadow) { shadow->hidden = (u8)!vis; shadow->x = sx; shadow->y = sy; }   /* 自機帯(表A)に来た分 */
        /* ボス帯の中(表B)に来た分: 壁の行に掛かると壁が 9 枚目で欠けるので、壁より上に収まるときだけ */
        shb_on = (u8)(vis && sy >= FINAL_TOP_LINE + 2 && sy + 16 <= FINAL_WALL_LINE - 16);
        shb_x = boss_mag() ? (s16)(sx - 8) : sx;
        shb_y = boss_mag() ? (s16)(sy - 8) : sy;
        if (shb_x < 0) shb_on = 0;
    }

    if (st == ST_WAIT) {
        put_sprites(1, ap);
    } else {
        put_sprites(blink, load_frame());
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
