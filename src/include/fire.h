/* fire.h — 発砲プリミティブ emit と発砲スクリプト run_fire(データ駆動)。
   HANDOFF §4-2 の run_fire を骨格化。難易度メカ(予告/レイジ/抑え込み等)は将来ここへ足す。

   FireDesc(バイト列): { interval, suppress, [op, a, kind, spd] x N, 0 }
     interval : 発火間隔(フレーム)
     suppress : ★ゼロ距離抑え込み半径(px, 0=無効)。自機がこの半径内なら発射スキップ。
                実効半径は難易度で増減(EASY=広い/HARD=狭い, fire.c の supp_adj)。
     op       : 発砲オペコード(下記)
     a        : opの引数(FIXED=方向0-31 / RING=弾数 / AIMED=散らし半幅 / AIMFAN=弾数)
     kind     : 弾種(色/パターンの選択に使用)
     spd      : 弾速(方向テーブル基準の 1/8 スケール)
     終端      : op=0 で1レコード終わり
   方向 dir(0-31)=32分割。0=上、時計回り(11.25°刻み)。

   ★ゼロ距離抑え込み(1面ビスマルクの教育メカ, HANDOFF §7): 危険砲に肉薄すると撃てなくなり(発射スキップ)、
     近距離で安全に連射→最速撃破できる。抑え込み半径は難易度ダイヤルで変わる(解の有無でなく実行猶予)。

   ★狙い弾の散らし(前作の「狙いすぎ」反省 → 公平化):
     AIMED  = 自機方向に「不正確さの円錐」= 一様乱数 ±a ステップを足して1発
              (出典: dev.to tinygamedev "Simple Bullet Spread for AI" の aim+uniform offset)。
     AIMFAN = 自機中心の n-way 散弾(a発)。隣接2ステップ間隔で扇状、偶数なら自機直線上に隙間、
              さらに扇全体を乱数で微回転(danmaku設計の aimed spread + inconsistency)。 */
#ifndef FIRE_H
#define FIRE_H

#include "types.h"
#include "entity.h"

/* 発砲オペコード */
#define FIRE_END    0   /* レコード終端 */
#define FIRE_FIXED  1   /* a=固定方向(0-31)へ1発 */
#define FIRE_RING   2   /* a=弾数の全方位リング */
#define FIRE_AIMED  3   /* 自機狙い＋散らし円錐(a=散らし半幅ステップ。0で厳密狙い) */
#define FIRE_AIMFAN 4   /* 自機中心の n-way 狙い散弾(a=弾数)＋扇の乱数微回転 */

/* 1発生成。dir(0-31)/spd から速度を決め ET_BULLET を1つ spawn。戻り値は実体(満杯なら NULL)。 */
Entity *emit(s16 x, s16 y, u8 dir, u8 kind, u8 spd);
/* 時限信管弾(エアバースト): fuze フレーム後に下向き3破片へ炸裂する ET_AABURST を1つ spawn。 */
Entity *emit_burst(s16 x, s16 y, u8 dir, u8 spd, u8 fuze);

/* (ex,ey)から(px,py)への最近傍方向(0-31)。自機狙い用。 */
u8 aim_dir(s16 ex, s16 ey, s16 px, s16 py);

/* 射手 e の FireDesc(e->fire)を進め、interval 毎に発砲オペを実行する。 */
void run_fire(Entity *e);

#endif /* FIRE_H */
