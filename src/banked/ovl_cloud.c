/* ovl_cloud.c — 1〜5面の雲(ROADMAP P2-B / B3)。RAM オーバレイ(通常面)で動く。
   ★高さの関係: 戦闘機は低空を飛んでいるので、**雲は機体より上**。上から見下ろす画面では雲が機体の手前に来る。
     MSX は背景をスプライトより手前に出せないので、雲は**スプライト**で、**HUD の次・自機より前の番号**に置く
     (低い番号ほど手前)。雲はカメラに近いので海より速く流す(海 1px/フレーム、雲 2px/フレーム)。
   ★雲の絵は市松の網目(tools/gen_cloud.py)。雲の下に入っても自機や敵弾が隙間から見える＝理不尽に隠さない。
   ★1走査線 8 枚の制約: 雲は 32x32(スプライト 2x2)で 1 走査線に 2 枚。同時に出す雲は 1 つ。
     雲は手前(低い番号)なので、混んだ走査線では弾のほうが消える。なので海の区間(弾が少ない)だけで出す。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"     /* ent_spr_cache_inval */
#include "gamestate.h"  /* g_crush(ボム残数。発動で減る＝津波が色表を上書きした印) */
#include "sprites.h"
#include "aa_hot.h"     /* cam / curstage / rnd */

#define CL_SPEED  2       /* 下へ流れる速さ px/フレーム(海は 1) */
#define SAT_A     0x7600  /* スプライト属性表 セットA / B(vdp.c の SPR_ATTR / SPR_ATTR_B と同じ番地) */
#define SAT_B     0x7200

/* 面ごとの色(上半分=明るい / 下半分=影)。暗い面で真っ白だと浮くので灰へ落とす */
static const u8 cl_col[5][2] = { {15, 14}, {15, 14}, {14, 4}, {15, 14}, {14, 4} };

/* ★オーバレイの枠が残り少ないので、雲は同時に 1 つ(32x32＝スプライト 4 枚)。 */
static s16 cy;            /* 雲の上端(画面 Y)。212 以上=出ていない */
static u8  cx, wait;
static u8  last_key, last_crush;   /* 前回の置き方(開始 slot・先頭の部品・枚数)。変わったら色表を書き直す */
static u16 last_cam;
static u8  buf[16];
static u8  col[16];

/* slot から雲のスプライトを置き、使った枚数を返す。spawn=0 で新しい雲を出さない(残っている雲は流れ去る) */
u8 ovl_cloud_frame(u8 slot, u8 spawn) {
    u8 k, k0, n, *p, key;

    /* 面の開始/やり直し(カメラが戻った)で雲を捨てる。★static は前のオーバレイの残り物なので、範囲外の値も捨てる
       (x が 207 を超えた残り物で右半分が左端へ回り込み、半分だけの雲が出た) */
    if (cam > last_cam || cy < -32 || cx > 207) { cy = 212; cx = 0; wait = 20; }
    last_cam = cam;

    if (cy < 212) cy += CL_SPEED;
    else if (wait) wait--;
    else if (spawn) { cy = -32; cx = (u8)(16 + (rnd() % 192)); wait = (u8)(30 + (rnd() & 63)); }
    if (cy >= 212) return 0;

    /* 置く: 左上/右上/左下/右下 のうち画面に掛かる行だけ(上の行が上端より上なら下の行だけ) */
    k0 = (cy <= -16) ? 2 : 0;
    n = (cy >= 196) ? 2 : (u8)(4 - k0);
    p = buf;
    for (k = k0; k < k0 + n; k++) {
        u8 ay = (u8)((u8)cy + ((k & 2) ? 16 : 0) + g_vscroll - 1);   /* 表示 Y = 属性 Y + 1、縦スクロール補正 */
        if (ay == 216) ay = 215;                                      /* 216 は停止マーカ */
        *p++ = ay; *p++ = (u8)(cx + ((k & 1) ? 16 : 0)); *p++ = (u8)(SPR_CLOUD0 + k * 4); *p++ = 0;
    }

    /* 色表: 置き方が変わったとき/クラッシュ(津波が色表を上書き)の後だけ書く */
    key = (u8)(slot * 16 + k0 * 4 + n);
    if (key != last_key || g_crush != last_crush) {
        for (k = 0; k < n; k++) {
            u8 r, c = cl_col[curstage][((k0 + k) & 2) ? 1 : 0];
            for (r = 0; r < 16; r++) col[r] = c;
            vdp_sprite_color_tab((u8)(slot + k), col);
        }
        ent_spr_cache_inval(slot);   /* ★この先の slot の色キャッシュは当てにならない(雲が書いた/枚数が変わった) */
        last_key = key; last_crush = g_crush;
    }

    /* 属性: セット A と(分割で使う)セット B の両方へ。★0x98 への連続書き込みは C のループで(vdp.c の注意と同じ) */
    k = (u8)(n * 4);
    vdp_write_addr((u16)(SAT_A + (u16)slot * 4));
    for (p = buf; p < buf + k; p++) vdp_data(*p);
    if (g_spr_dual) {
        vdp_write_addr((u16)(SAT_B + (u16)slot * 4));
        for (p = buf; p < buf + k; p++) vdp_data(*p);
    }
    return n;
}
