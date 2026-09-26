/* spinfx.h — 面ごとの撃沈演出(OVL13)のうち、SCREEN3 で「撃破の画面」を作り直す部分の共有。
   ovl_spin.c(3面のきりもみ)が持っている土台を ovl_tilt.c(5面のパース)からも使う。
   ★同じバンクの中でだけ使う約束。常駐からは呼ばない。 */
#ifndef SPINFX_H
#define SPINFX_H

#include "types.h"

/* hot_ram を借りる置き場(演出中だけ。終わったら常駐が hot_load() で戻す)。 */
#define TEX_ADDR  0xC600   /* 元絵 64x128 テクセル(4bit 詰め) = 4,096B */
#define TEX       ((u8 *)TEX_ADDR)
#define SEA_ADDR  0xD600   /* 海タイル 16x16(1B=1テクセル) = 256B */
#define SEA       ((u8 *)SEA_ADDR)
#define TEX_H     124      /* 元絵に絵が入っている行数(496 ドット / 4)。124.. は海 */

#define S3_PAT    0x0000   /* SCREEN3 パターン(色)テーブル: 1,536B */
#define S3_NAME   0x0800   /* 名前テーブル: 768B */

/* 撃破の瞬間の画面(＋画面外の艦首・艦尾)を 4 ドット 1 テクセルで TEX へ吸い出す。
   SCREEN5 のうちに呼ぶこと(モードを変えると VRAM が消える)。 */
void spinfx_grab(void);
/* SCREEN3(MULTI COLOUR)へ切り替え、名前表を並べてパターン表を画面にする。 */
void spinfx_enter_s3(void);

#endif /* SPINFX_H */
