/* sprites.c — スプライトの「常駐」部分: 毎フレーム参照する色表(hot)と、パターン投入のラッパ。
   ★パターンのビットマップ(pat_*, 約768B)は面開始で1回VRAMへ流すだけの cold data なので、
     常駐節約のため banked/ship_render.c(bank16, mode4) へ移設し、sprites_load は薄い bcall ラッパにした。
     hot な色表(zcol/barrel_col/barrel_flash=毎フレーム coltab 参照)だけ常駐に残す。
   mode2 16x16: 前半16B=左列(cols0-7, MSB=col0) rows0-15 / 後半16B=右列(cols8-15) rows0-15。 */
#include "sprites.h"
#include "ship.h"    /* g_shipargs / SHIP_RENDER_BANK(パターン投入を bank16 へ bcall) */
#include "bank.h"    /* bcall_to */
#include "vdp.h"     /* vdp_sprite_pattern(戦闘機8方向の手続き生成でVRAM投入) */

/* 零戦の16行カラーテーブル(旧 zcol): 3/8/10=緑系, 14=ハイライト, 5=暗(プロペラ/尾)。
   ★自機の e->coltab=zcol として毎フレーム参照される=常駐に残す。 */
const u8 zcol[16] = { 5, 5, 3, 8, 14, 8, 10, 8, 8, 3, 8, 8, 3, 8, 3, 3 };

/* 砲身の行別シェード(金属感): 黒縁→暗灰→中灰 の対称バンド。★白(15)/淡灰(14)は使わない=
   ドームの白っぽいテカリと同化して砲身がちょん切れて見えるのを防ぐ(ハイライトは中灰4止まり)。
   ★bh_turret が e->coltab=barrel_col/flash として毎フレーム参照する=常駐に残す。 */
const u8 barrel_col[16]   = { 13,1,1,5,5,4,4,4,4,4,4,5,5,1,1,13 };  /* 暗いガンメタル砲身(ドームの白と明確に対比) */
const u8 barrel_flash[16] = { 15,15,15,15,15,15,15,15, 15,15,15,15,15,15,15,15 };

/* スプライトパターン投入(各シーンが init で1回呼ぶ)。重いビットマップ(pat_*)は常駐節約のため
   bank16 に置いたので、引数なしの cold 経路として mode4 で bcall(カード拡大等と同じ機構)。 */
/* ===== 敵戦闘機/艦載機の8方向スプライトを手続き生成(旧版 build_plane_pattern 移植・常駐) =====
   胴体＋主翼＋尾翼＋エンジンを線分で描く。8方向×3サイズ(小/中/大)を面別に生成しVRAMへ。setup時=cartで実行。
   ★縞々対策(旧版で踏んだ罠): 斜め方向は線が1pxの対角線になり隣接ピクセルが角接触で市松に途切れる。
     斜め時だけ進行X方向へ+1した補填ピクセルを足して隙間を埋める(pl_plot呼びの ★印)。 */
static const s8 pl_ddx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };   /* 0=上,時計回り(dir8/dirdx8と一致) */
static const s8 pl_ddy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
static const u8 pl_sz[3][3] = { {2,2,3}, {4,3,5}, {6,5,7} };  /* {noseL,tailL,wing} 小/中/大 */
static const s8 pl_wingd[5] = { -1, +1, 0, +1, +1 };         /* 面別の翼幅デルタ(独細/米幅広/英中/独幅広/米幅広) */

static void pl_plot(u16 *bar, s8 x, s8 y) {   /* 関数化=マクロ多重展開の肥大を回避(常駐節約) */
    if (x >= 0 && x < 16 && y >= 0 && y < 16) bar[y] |= (u16)(0x8000u >> x);
}
static void build_plane(u8 dir, s8 noseL, s8 tailL, s8 wing, u8 patnum) {
    u16 bar[16]; u8 i, buf[32], k = 0;
    s8 ddx = pl_ddx[dir], ddy = pl_ddy[dir], px = (s8)-ddy, py = ddx, t;
    for (i = 0; i < 16; i++) bar[i] = 0;
    for (t = (s8)-tailL; t <= noseL; t++) {                    /* 胴体(2px幅) */
        s8 fx = (s8)(8 + ddx*t), fy = (s8)(8 + ddy*t);
        pl_plot(bar, fx, fy); pl_plot(bar, (s8)(fx+px), (s8)(fy+py));
        if (ddx && ddy) { pl_plot(bar, (s8)(fx+ddx), fy); pl_plot(bar, (s8)(fx+ddx+px), (s8)(fy+py)); }  /* ★斜め隙間埋め */
    }
    { s8 wx = (s8)(8 + ddx), wy = (s8)(8 + ddy);               /* 主翼 */
      for (t = (s8)-wing; t <= wing; t++) {
          pl_plot(bar, (s8)(wx+px*t), (s8)(wy+py*t)); pl_plot(bar, (s8)(wx+ddx+px*t), (s8)(wy+ddy+py*t));
          if (ddx && ddy) pl_plot(bar, (s8)(wx+px*t+ddx), (s8)(wy+py*t));                                /* ★斜め隙間埋め */
      } }
    { s8 tx = (s8)(8 - ddx*tailL), ty = (s8)(8 - ddy*tailL), w2 = (s8)((wing>>1)+1);  /* 水平尾翼 */
      for (t = (s8)-w2; t <= w2; t++) pl_plot(bar, (s8)(tx+px*t), (s8)(ty+py*t)); }
    { s8 nx = (s8)(8 + ddx*noseL), ny = (s8)(8 + ddy*noseL);   /* エンジン(機首先端3px) */
      pl_plot(bar, nx, ny); pl_plot(bar, (s8)(nx+px), (s8)(ny+py)); pl_plot(bar, (s8)(nx-px), (s8)(ny-py)); }
    for (i = 0; i < 8; i++)  buf[k++] = (u8)(bar[i] >> 8);      /* TL / BL / TR / BR */
    for (i = 8; i < 16; i++) buf[k++] = (u8)(bar[i] >> 8);
    for (i = 0; i < 8; i++)  buf[k++] = (u8)(bar[i] & 0xFF);
    for (i = 8; i < 16; i++) buf[k++] = (u8)(bar[i] & 0xFF);
    vdp_sprite_pattern(patnum, buf);
}
static void load_planes(u8 stage) {
    static const u8 base[3] = { SPR_PLANE_S, SPR_PLANE_M, SPR_PLANE_L };
    s8 wd = pl_wingd[(stage < 5) ? stage : 0];
    u8 sz, d;
    for (sz = 0; sz < 3; sz++)
        for (d = 0; d < 8; d++)
            build_plane(d, (s8)pl_sz[sz][0], (s8)pl_sz[sz][1], (s8)((s8)pl_sz[sz][2] + wd), (u8)(base[sz] + d * 4));
}

void sprites_load(u8 stage) {
    g_shipargs.mode = 4;
    bcall_to(SHIP_RENDER_BANK);   /* 静的パターン(bank16) */
    load_planes(stage);           /* ★その面の戦闘機8方向×3サイズ(常駐生成) */
}
