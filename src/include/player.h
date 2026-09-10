/* player.h — 自機(ET_PLAYER)の behavior と公開位置。
   入力/発砲/音を扱うためコア(entity.c)ではなくゲーム側モジュールに置く。
   g_player_x/y は run_fire の AIMED(自機狙い)や UI が参照する現在位置。 */
#ifndef PLAYER_H
#define PLAYER_H

#include "types.h"
#include "entity.h"

extern u8 g_player_x, g_player_y;   /* 自機の現在位置(bh_player が毎フレーム更新) */

void bh_player(Entity *e);          /* entity.c の behaviors[ET_PLAYER] から呼ばれる */

#endif /* PLAYER_H */
