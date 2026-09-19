/* ovl_palette.c — パレットエンジン(設計メモ §2-A「パレットで殴る全画面演出」)。
   ★狙い: 実測で確定した天井は VDP 帯域(204 B/ms)。パレットは **16 エントリ = 全画面の数千分の1** で
     画面全体の色を変えられる＝「重い計算・軽い出力」の理想形。毎フレーム 16 色を計算して書いても
     VDP コストはほぼゼロ(1 エントリ 4 ポートアクセス ≒ 20µs、全書換でも 0.32ms)。
   ★常駐ではなく RAM オーバレイ(overlay.h)で動く＝常駐窓を消費しない。

   実装している演出:
     ・被弾の赤染め      … g_playerhit の増加を検出して数フレーム全画面を赤へ寄せる
     ・撃破の白フラッシュ… g_gun_kills の増加を検出して数フレーム全画面を白へ寄せる
     ・海のシマー        … 海の斑点色(2/7)だけを微妙に上下させ、水面が生きて見えるようにする
       (地色 1 は動かさない。面積が大きいので動かすと画面全体が明滅して品が無い)

   ★トリガは既存のカウンタの増加を見るだけ＝ゲーム側に一切手を入れない。

   ★★色番号の所有者を調べてから動かすこと(一度これで実機のベース表示を壊した)。
     当初シマーで色7も動かしていたが、**色7は「暗い海の斑点」と「艦のドロップシャドウ(SHADOWC)」の
     共用**で、戦艦の影の色が勝手に変わって見えた。vdp_palette_game のコメントにも
     「暗海 / 艦のドロップシャドウ」と書いてあったのに読み落とした。
     調査結果(常時動かしてよいのは色2だけ):
       色1  = 海の地色。面積が大きく、動かすと画面全体が明滅する          → 触らない
       色2  = 海の明斑点。**海専用**(scroll.c の1箇所のみ。敵機カラー表/zcol/barrel_col にも無し) → 可
       色7  = 暗い海の斑点 **かつ** 艦のドロップシャドウ(ship_render の SHADOWC) → 触らない
       色13 = 落ち影(entity.c draw_shadow)かつ砲身の縁/Fw190 の陰               → 触らない
     フラッシュ(被弾/撃破)は全色を一瞬だけ寄せる演出なので、共用でも問題ない(すぐ戻る)。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"   /* g_playerhit / g_gun_kills: 演出のトリガに使う既存カウンタ */
#include "gamestate.h" /* g_crush_t / CRUSH_FRAMES: メガクラッシュの雷光 */

/* ★面ごとの時間帯・天候の基準パレット(1面=昼 / 2面=夕焼け / 3面=荒天 / 4面=朝霧 / 5面=夜戦)。
   tools/gen_grade.py が生成する。1面(昼)の値は vdp_palette_game() と同じ(あちらは初期化、こちらは毎フレームの計算元)。
   ★色調はパレットだけで変える＝VDP 帯域ほぼゼロで面の印象が変わる。弾/爆発/数字(11,12,15)は全面で同じ明るさ。 */
#include "aa_hot.h"   /* curstage / rnd */
#include "sound.h"    /* sfx(雷鳴) */
#ifdef OVL_FINAL
/* ★最終面のオーバレイは枠が狭い(残り数十B)ので、昼の 1 面分だけ持つ(最終面は昼) */
static const u8 pal_stage[1][16][3] = {
    { {0,0,0},{1,4,5},{2,5,6},{1,3,1},{3,3,3},{2,2,2},{6,5,3},{0,1,3},
      {2,5,2},{3,3,1},{4,6,4},{7,1,1},{7,4,0},{1,1,1},{4,4,5},{7,7,7} } };
#define PAL_STAGE_IDX 0
#elif defined(OVL_DD)
/* ★3面の中ボス(駆逐艦)のオーバレイ: 3面(荒天)の1面分だけ。9 番は艦の甲板の灰(被弾で白)に差し替える */
#define PAL_ONLY_STAGE 2
#include "stage_grade.h"
#define PAL_STAGE_IDX 0
extern u8 g_dd_flash;
#elif defined(OVL_P61)
/* ★5面の中ボス(P-61)のオーバレイ: 5面(夜戦)の1面分だけ */
#define PAL_ONLY_STAGE 4
#include "stage_grade.h"
#define PAL_STAGE_IDX 0
#elif defined(OVL_TWIN)
/* ★4面の中ボスのオーバレイも枠が狭いので、4面(朝霧)の1面分だけ持つ */
#define PAL_ONLY_STAGE 3
#include "stage_grade.h"
#define PAL_STAGE_IDX 0
#else
#include "stage_grade.h"
#define PAL_STAGE_IDX ((curstage < 5) ? curstage : 0)
#endif

/* 直前に書いた値。差分のあるエントリだけ書く(全書換でも安いが、無駄は無いほうがよい)。 */
static u8 pal_cur[16][3];
static u8 pal_valid;        /* 0=pal_cur 未初期化(初回は全書き) */

/* 演出状態 */
static u8 fx_kind;          /* 0=なし / 1=被弾(赤) / 2=撃破(白) */
static u8 fx_t;             /* 残りフレーム(大きいほど濃い) */
static u8 last_hit, last_gun;
static u8 shimmer;          /* 海シマーの位相 */
static u8 storm_t, storm_f; /* 3面の稲光: 次までのフレーム / 光っている残り */
#ifdef OVL_SINK
u8 sink_flash;              /* 撃沈シーン(ovl_sink.c)の閃光: >0 の間、全画面を白へ寄せる */
#endif

#define FX_NONE 0
#define FX_HIT  1
#define FX_KILL 2
#define FX_HIT_FRAMES  10
#define FX_KILL_FRAMES 5

/* ★雷光の強さ(0..4)。index = g_crush_t(残りフレーム)。本物の雷のように不規則に瞬かせる
   (一定に光らせると「白い板」になって雷に見えない)。
   台本は gamestate.h の CRUSH_* と対応させること:
     t=72/66/60 が稲妻を描くフレーム(必ず 0) / t=46..1 が津波 / t=54 は稲妻の消去。
   ★クラッシュ中は 60fps なので、閃光1段=1/60秒。30fps のときより1段ずつ細かく置く。 */
static const u8 crush_lv[CRUSH_FRAMES + 1] = {
    /* t=  0.. */ 0, 1, 0, 0, 1, 0, 0, 0, 0, 0,
    /* t= 10.. */ 1, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    /* t= 20.. */ 0, 1, 0, 0, 0, 0, 1, 0, 1, 0,
    /* t= 30.. */ 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    /* t= 40.. */ 0, 0, 0, 0, 0, 1, 2, 0, 0, 0,
    /* t= 50.. */ 0, 0, 0, 0, 0, 1, 2, 2, 4, 4,
    /* t= 60.. */ 0, 1, 2, 2, 4, 4, 0, 1, 2, 2,
    /* t= 70.. */ 4, 4, 0, 0, 1, 0, 1, 0, 1,
};
/* ★稲妻を描くフレーム(t=72/66/60)は必ず **0(素の画面)**。ここを明るくすると白い筋が背景に溶けて
   何も見えない(実際に一度そうなった)。閃光はその**直後**のフレームに置き、
   「暗い海に白い筋が走る → 次の瞬間に画面が白く飛ぶ」を繰り返して雷に見せる。
   ★津波の間(t=46..1)も同じ理由で 0/1 に抑える(明るくすると白い波頭が背景に溶ける)。 */

/* base から target へ w/4 だけ寄せる(w=0..4)。0-7 の範囲に収まる。 */
static u8 mix(u8 base, u8 target, u8 w) {
    s8 d = (s8)((s8)target - (s8)base);
    return (u8)((s8)base + (s8)(((s16)d * (s16)w) >> 2));
}

void ovl_pal_update(void) {
    u8 i, w = 0, tr = 0, tg = 0, tb = 0;

    /* --- トリガ検出(ゲーム側のカウンタの増加を見るだけ) --- */
    if (g_playerhit != last_hit) { last_hit = g_playerhit; fx_kind = FX_HIT;  fx_t = FX_HIT_FRAMES; }
    else if (g_gun_kills != last_gun) { last_gun = g_gun_kills; fx_kind = FX_KILL; fx_t = FX_KILL_FRAMES; }

    /* ★メガクラッシュの雷光は最優先(被弾/撃破フラッシュより上)。 */
    if (g_crush_t) {
        u8 lv = crush_lv[(g_crush_t <= CRUSH_FRAMES) ? g_crush_t : CRUSH_FRAMES];
        fx_t = 0; fx_kind = FX_NONE;
        w = lv; tr = 7; tg = 7; tb = 7;
    } else if (fx_t) {
        fx_t--;
        if (fx_kind == FX_HIT) { tr = 7; tg = 0; tb = 0;   /* 赤へ */
            w = (u8)((fx_t * 4 + FX_HIT_FRAMES / 2) / FX_HIT_FRAMES); }
        else                   { tr = 7; tg = 7; tb = 7;   /* 白へ */
            w = (u8)((fx_t * 4 + FX_KILL_FRAMES / 2) / FX_KILL_FRAMES); }
        if (w > 4) w = 4;
        if (fx_t == 0) fx_kind = FX_NONE;
    }

#ifdef OVL_SINK
    if (sink_flash) { w = (sink_flash > 4) ? 4 : sink_flash; tr = tg = tb = 7; sink_flash--; }
#endif
    shimmer++;
    /* ★3面(荒天): ときどき稲光。数秒おきに「白→少し戻る→また光る→消える」を 4 フレームで。 */
#if !defined(OVL_FINAL) && !defined(OVL_TWIN) && !defined(OVL_P61)   /* 3面の稲光(最終面/4面・5面の中ボスのオーバレイには要らない) */
    if (curstage == 2 && !w) {
        if (storm_t) storm_t--;
        else { storm_t = (u8)(90 + (rnd() & 127)); storm_f = 4; sfx(2, SFX_THUNDER); }
        if (storm_f) { static const u8 flash[5] = { 0, 2, 1, 3, 4 }; w = flash[storm_f]; tr = tg = tb = 7; storm_f--; }
    }
#endif

    for (i = 0; i < 16; i++) {
        const u8 *pb = pal_stage[PAL_STAGE_IDX][i];
        u8 r = pb[0], g = pb[1], b = pb[2];
        /* ★海の明斑点(色2)だけ。色7は艦のドロップシャドウと共用なので絶対に触らない(上の調査)。 */
        if (i == 2) {
            u8 ph = (u8)(shimmer & 31);
            if (ph < 8)       { if (b < 7) b++; }
            else if (ph < 16) { }
            else if (ph < 24) { if (b) b--; }
        }
#ifdef OVL_DD
        if (i == 9) { if (g_dd_flash) r = g = b = 7; else { r = 3; g = 3; b = 3; } }   /* 駆逐艦の甲板(中ボス専用の色) */
#endif
        if (w) { r = mix(r, tr, w); g = mix(g, tg, w); b = mix(b, tb, w); }
        if (!pal_valid || pal_cur[i][0] != r || pal_cur[i][1] != g || pal_cur[i][2] != b) {
            vdp_set_pal(i, r, g, b);
            pal_cur[i][0] = r; pal_cur[i][1] = g; pal_cur[i][2] = b;
        }
    }
    pal_valid = 1;
}

/* 面開始/再開で呼ぶ: 状態を捨てて次フレームに全エントリを書き直させる。 */
void ovl_pal_reset(void) {
    pal_valid = 0; fx_kind = FX_NONE; fx_t = 0; shimmer = 0; storm_t = 120; storm_f = 0;
    last_hit = g_playerhit; last_gun = g_gun_kills;
}
