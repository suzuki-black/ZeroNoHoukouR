/* input.h — 入力(カーソル/ジョイ方向・トリガ)の常駐API。
   キーボード row8 直読み ＋ ジョイスティック port1(PSG R#14)を論理和で提供。 */
#ifndef INPUT_H
#define INPUT_H

#include "types.h"

/* 押下ビット(1=押下)。キーボード(row8)とジョイスティック(port1)の論理和。 */
#define INP_RIGHT 0x01
#define INP_DOWN  0x02
#define INP_UP    0x04
#define INP_LEFT  0x08
#define INP_TRIG  0x10   /* トリガA: スペース or ジョイ トリガ1(発砲/決定) */
#define INP_TRIGB 0x20   /* トリガB: ジョイ トリガ2 or キーM(コナミコマンドのB等) */

extern u8 g_input;       /* 今フレームの押下状態     */
extern u8 g_input_edge;  /* 今フレーム“押した瞬間”   */

/* 毎フレーム1回呼ぶ。g_input / g_input_edge を更新。 */
void input_poll(void);

#endif /* INPUT_H */
