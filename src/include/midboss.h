/* midboss.h — 中ボス(ROADMAP B4)。常駐(scene_stage.c)と中ボス用オーバレイ(ovl_midboss.c)の取り決め。
   流れ: 海の区間で出現位置に来たら常駐が g_mb=MB_LOAD → ホット区間の外でオーバレイを OVL8 へ入れ替え、
   砲身のパターン行を退避して mb_init → g_mb=MB_ACTIVE。オーバレイが終われば g_mb=MB_RESTORE →
   常駐がホット区間の外で砲身を書き戻し、通常面のオーバレイへ戻して g_mb=MB_OVER。 */
#ifndef MIDBOSS_H
#define MIDBOSS_H

#include "types.h"
#include "sprites.h"
#include "hud.h"

#define MB_NONE     0
#define MB_ACTIVE   1
#define MB_RESTORE  2
#define MB_OVER     3
#define MB_LOAD     4

#define OVL8_BANK    24    /* 中ボス用オーバレイ(通常面のものから主砲の弾幕を抜き、中ボスを足したもの) */
#define MB_FRAMES_BANK 23  /* Fw 200 の 64 コマ(8192B) */
#define MB_VRAM_Y    136   /* page0: コマを並べる先頭行(136..199) */
#define MB_SAVE_Y    200   /* page0: 借りる前の砲身パターン行の退避先 */
#define MB_PAT_LINE  247   /* スプライトパターン表のうち SPR_BARREL0..+12 が載る行(0x7800+112*8=247*128) */
#define MB_PAT       SPR_BARREL0
#define MB_SLOT      (HUD_SLOTS + 4)   /* 宙返りの4枠の後ろ */

extern u8 g_mb;

#endif /* MIDBOSS_H */
