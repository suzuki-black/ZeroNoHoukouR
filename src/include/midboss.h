/* midboss.h — 中ボス(ROADMAP B4)。常駐(scene_stage.c)と中ボス用オーバレイ(ovl_midboss.c)の取り決め。
   流れ: 海の区間で出現位置に来たら常駐が g_mb=MB_LOAD → ホット区間の外でオーバレイを OVL8 へ入れ替え、
   借りるパターン行を退避して mb_init → g_mb=MB_ACTIVE。オーバレイが終われば g_mb=MB_RESTORE →
   常駐がホット区間の外でパターン行を書き戻し、通常面のオーバレイへ戻して g_mb=MB_OVER。 */
#ifndef MIDBOSS_H
#define MIDBOSS_H

#include "types.h"
#include "sprites.h"

#define MB_NONE     0
#define MB_ACTIVE   1
#define MB_RESTORE  2
#define MB_OVER     3
#define MB_LOAD     4

#define OVL8_BANK      24   /* 中ボス用オーバレイ(通常面のものから主砲の弾幕を抜き、中ボスを足したもの) */
#define MB_FRAMES_BANK 29   /* Fw 200 の 32 コマ(1コマ512B。29,30 の2バンク=16384B) */
#define MB_NDIR        32
#define MB_VRAM_Y      40   /* page0: コマを並べる先頭行(1コマ4行 → 40..167) */
#define MB_SAVE_Y      168  /* page0: 借りる前のパターン4行の退避先(168..171) */
/* ★64x64 = 16x16 を 4x4 の16枚。パターンは 4 行(1行=横1列の4枚)ぶん借りる:
     247/248 行 = 主砲の砲身 SPR_BARREL0..(112..143) / 250/251 行 = 発艦中の小さい艦載機 SPR_PLANE_S..(160..191)。
     どちらも戦艦の区間でしか使わない。マス c(0..15, 行優先)のパターン番号は MB_CELL_PAT(c)。 */
#define MB_PAT_ROW_A   247
#define MB_PAT_ROW_B   250
#define MB_CELL_PAT(c) ((u8)((((c) < 8) ? SPR_BARREL0 : SPR_PLANE_S) + (((c) & 7) << 2)))

extern u8 g_mb;
extern u8 g_mb_n;   /* 中ボスがいま使っているスプライト枚数。32-g_mb_n 以降を使う(最低優先)。エンティティはその手前まで */

#endif /* MIDBOSS_H */
