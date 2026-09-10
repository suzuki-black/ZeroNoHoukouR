/* aa_hot.h — AA(対空砲23基)サブシステムのRAM実行のための共有シンボル。
   本体 aa_update/aa_collide は banked/hot.c(hot_ram=RAM実行)へ移設。R800のROMフェッチ律速を外すため、
   毎フレーム23基を走査するこの2関数を内蔵RAMで実行する。状態・補助関数は scene_stage.c(常駐/ROM)に残し、
   ここで公開する(hot.c から参照/呼び出し。実体RAM化はしない=依存の連鎖を断つ)。 */
#ifndef AA_HOT_H
#define AA_HOT_H

#include "types.h"
#include "ship.h"   /* SHIP_NAAG */

/* scene_stage.c が定義(常駐DATA/ROM)。hot.c(RAM)から参照するため extern 公開。 */
extern u16 cam;                    /* 縦スクロールカメラ(世界Y) */
extern u8  curstage;               /* 現在面(0..STAGE_COUNT-1) */
extern u8  aa_fire[SHIP_NAAG];     /* AA発砲クールダウン */
extern u8  aa_hp[SHIP_NAAG];       /* AA耐久(0で破壊) */
extern u8  aa_dead[SHIP_NAAG];     /* 1=破壊(発砲/当たり無し) */
extern u8  aa_nvis;                /* このフレームの可視AA数 */
extern u8  aa_vis_i[SHIP_NAAG];    /* 可視AAの砲index */
extern s16 aa_vis_sx[SHIP_NAAG];   /* 可視AAの画面X(蛇行込み) */
extern s16 aa_vis_sy[SHIP_NAAG];   /* 可視AAの画面Y */
extern u8   rnd(void);                          /* 乱数(rng は scene_stage 内に保持) */
extern void burn_add(s16 cx, u16 worldY, u8 s); /* 撃破時の炎上サイト登録 */

/* 常駐ラッパ(hotcode.c)。scene_stage が呼ぶ。実体は hot_ram のジャンプテーブル該当スロット。 */
void aa_update(void);
void aa_collide(void);

#endif /* AA_HOT_H */
