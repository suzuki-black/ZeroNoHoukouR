/* ovl_rot.c — 宙返りの表示(ROADMAP P2 項目9 / 設計メモ §2-C の前哨戦)。
   ★コマは面の準備で bank19(gen_planes.c の loop_prerender)が **page0 の 212〜220行**へ焼いてある。
     ここは宙返りの各フレームで **1行(128B)を HMMM でスプライトパターン表(249行＝SPR_WAVE0..+12)へ
     コピーし、2×2 の4スプライトを置くだけ**。CPU の仕事はほぼゼロ。
   ★★経緯(苦労と教訓 §14):
     ・面内回転は 16x16 では中間角で読めない → 上から見た宙返りは「縦の圧縮＋背面反転」。
     ・見下ろし視点で上昇するので**上がるほど大きく見えるべき**(実機で指摘)→ 頂点で2倍に拡大。
     ・スプライトの拡大は MAG(R#1 bit0)が**全スプライト一律2倍**しか無く個別拡大は無い。
       有効な手は**複数スプライトの合成**(2×2 で 32×32)。
     ・毎フレーム描いたら重すぎた(エミュ R800 で 16コマ 1.85秒)→ 事前に焼いて VDP にコピーさせる。 */
#include "types.h"
#include "vdp.h"
#include "sprites.h"
#include "player.h"     /* g_player_x/y: 合成の中心 */
#include "hud.h"        /* HUD_SLOTS: 合成に使うスプライト slot の先頭 */
#include "gamestate.h"  /* g_loop_alt: 影を離す量 */

#define LOOP_VRAM_Y  212   /* bank19 が焼いた9コマの先頭行(gen_planes.c と一致させる) */
#define SPR_PAT_LINE 249   /* スプライトパターン表のうち SPR_WAVE0..+12 が載る行(0x7800+144*8=249*128) */

/* 宙返りは前後対称: k と 16-k は同じコマ(寸法も反転も同じ)。 */
static const u8 zmap[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1 };
static const u8 zw[9]    = { 16, 17, 18, 21, 24, 27, 30, 31, 32 };

void ovl_rot_zoom(u8 k) {
    u8  u = zmap[k & 15], w = zw[u], i;
    s16 px, py;
    vdp_copy(0, (u16)(LOOP_VRAM_Y + u), 0, SPR_PAT_LINE, 256, 1);   /* 128B=4枚ぶんのパターンを一括 */
    /* 合成を自機の 16x16 の中心へ。X は負にできない(EC ビット不使用)ので 0..224 に収める。
       Y は小さな負なら MSX の負Y で上端クリップされる。 */
    px = (s16)g_player_x - 8; if (px < 0) px = 0; else if (px > 224) px = 224;
    py = (s16)g_player_y - 8;
    for (i = 0; i < 4; i++) {
        u8 sl = (u8)(HUD_SLOTS + i);
        /* 頂点付近は淡い緑=光を受けて近い感じ。★色は毎フレーム置く(途中でクラッシュが
           32枚の色表を上書きしても、宙返りの再開時に自分で直るように)。 */
        vdp_sprite_color(sl, (u8)((w >= 28) ? 10 : 3));
        vdp_sprite_pos(sl, (u8)(px + ((i & 1) ? 16 : 0)), (u8)(py + ((i & 2) ? 16 : 0)),
                       (u8)(SPR_WAVE0 + (i << 2)));
    }
    g_loop_alt = (u8)(w - 16);               /* 高度=影を離す量(0..16) */
}
