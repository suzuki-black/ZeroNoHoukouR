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

   ★トリガは既存のカウンタの増加を見るだけ＝ゲーム側に一切手を入れない。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"   /* g_playerhit / g_gun_kills: 演出のトリガに使う既存カウンタ */

/* 基準パレット。vdp_palette_game() と同じ値を持つ(あちらは初期化、こちらは毎フレームの計算元)。
   ★二重持ちだが、オーバレイから常駐の static を覗くわけにいかないので許容する。
     vdp_palette_game を変えたらここも合わせること。 */
static const u8 pal_base[16][3] = {
    { 0, 0, 0 }, { 1, 4, 5 }, { 2, 5, 6 }, { 1, 3, 1 },
    { 3, 3, 3 }, { 2, 2, 2 }, { 6, 5, 3 }, { 0, 1, 3 },
    { 2, 5, 2 }, { 3, 3, 1 }, { 4, 6, 4 }, { 7, 1, 1 },
    { 7, 4, 0 }, { 1, 1, 1 }, { 4, 4, 5 }, { 7, 7, 7 },
};

/* 直前に書いた値。差分のあるエントリだけ書く(全書換でも安いが、無駄は無いほうがよい)。 */
static u8 pal_cur[16][3];
static u8 pal_valid;        /* 0=pal_cur 未初期化(初回は全書き) */

/* 演出状態 */
static u8 fx_kind;          /* 0=なし / 1=被弾(赤) / 2=撃破(白) */
static u8 fx_t;             /* 残りフレーム(大きいほど濃い) */
static u8 last_hit, last_gun;
static u8 shimmer;          /* 海シマーの位相 */

#define FX_NONE 0
#define FX_HIT  1
#define FX_KILL 2
#define FX_HIT_FRAMES  10
#define FX_KILL_FRAMES 5

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

    if (fx_t) {
        fx_t--;
        if (fx_kind == FX_HIT) { tr = 7; tg = 0; tb = 0;   /* 赤へ */
            w = (u8)((fx_t * 4 + FX_HIT_FRAMES / 2) / FX_HIT_FRAMES); }
        else                   { tr = 7; tg = 7; tb = 7;   /* 白へ */
            w = (u8)((fx_t * 4 + FX_KILL_FRAMES / 2) / FX_KILL_FRAMES); }
        if (w > 4) w = 4;
        if (fx_t == 0) fx_kind = FX_NONE;
    }

    shimmer++;

    for (i = 0; i < 16; i++) {
        u8 r = pal_base[i][0], g = pal_base[i][1], b = pal_base[i][2];
        /* 海の斑点(2=明/7=暗)だけを微かに上下させる。地色(1)は面積が大きいので動かさない。 */
        if (i == 2 || i == 7) {
            u8 ph = (u8)((shimmer + (i == 7 ? 8 : 0)) & 31);
            if (ph < 8)       { if (b < 7) b++; }
            else if (ph < 16) { }
            else if (ph < 24) { if (b) b--; }
        }
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
    pal_valid = 0; fx_kind = FX_NONE; fx_t = 0; shimmer = 0;
    last_hit = g_playerhit; last_gun = g_gun_kills;
}
