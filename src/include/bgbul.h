/* bgbul.h — 背景に描く敵弾(ovl_bgbul.c)。中ボスのオーバレイ(5面 P-61 / 1面 Fw 200)が共有する。
   ★スプライトの枠を使わない: 1面の中ボスは 18 枚を使い、自機と弾に 4 枠しか残らず弾がちらついた(実機で指摘)。
   ★弾は 4x4 ドットの色 12 の四角。表示リングへ直接描き、消すときは海のひな形(左 32 ドット×16 行を RAM へ写したもの)で戻す。
     位置は**画面座標**(1/8 ドット)で持ち、描く行だけリングへ(y+cam)。スクロールに引きずられない(敵弾の規約)。
   ★表・弾数・ひな形の番地はオーバレイごとに -D で変える。5面はオーバレイの後ろ(0xBC00〜)、1面はオーバレイに空きが無いので
     CPU 弾幕の固定帯(0xEC00〜。中ボスの間は弾幕が出ない。最終面も同じ帯を借りている)。 */
#ifndef BGBUL_H
#define BGBUL_H

#include "types.h"

#ifndef PB_N
#define PB_N    48      /* 5面: 同時 48 発(20 では「弾数少ない」と指摘) */
#endif
#ifndef PB_RAM
#define PB_RAM  0xBC00  /* 5面: 48×9B=432B(0xBC00〜0xBDAF) */
#endif
#ifndef PB_TMPL_HI
#define PB_TMPL_HI 0xBF /* 5面: 海のひな形(左 32 ドット×16 行=256B)は 0xBF00〜。256 境界に置くこと(asm が上位バイトだけ足す) */
#endif
#define PB_TMPL ((u16)PB_TMPL_HI << 8)

typedef struct { s16 qx, qy; s8 vx, vy; u8 on, sx, ry; } PB;   /* 位置は 1/8 ドット。sx=バイト列 / ry=リング行 */
#define pbv ((PB *)PB_RAM)

void pb_init(void);                         /* 海のひな形を RAM へ写し、表を空に */
void pb_add(s16 x, s16 y, s8 vx, s8 vy);    /* 左上 (x,y) に 1 発。速度は 1/8 ドット/フレーム */
void pb_update(void);                       /* 動かす・画面外で消す・自機との当たり(宙返り中は当たらない) */
void pb_clear(void);                        /* 全部消す(海へ戻す) */
void pb_drop(void);                         /* 描いた跡を消さずに表だけ空に(リングを描き直した後) */
u8   ovl_bgb_split(u8 split_line);          /* 分割表(slot14)。衝撃波なしの2本(背景弾のオーバレイは衝撃波の枠が無い) */

#endif /* BGBUL_H */
