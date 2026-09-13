/* final.h — 最終面(巨大機 Douglas XB-19)。設計は docs/ROADMAP.md P2-8。
   ★画面を走査線で 3 つの帯に分ける(ラスタ割込み。R#1 のモードビットは行末まで遅延して効く＝継ぎ目が出ない):
       0 .. FINAL_TOP_LINE-1   : HUD 帯   … スプライト表 A・等倍(スコア/残機はいつもの位置)
       FINAL_TOP_LINE .. WALL  : ボス帯   … スプライト表 B・**拡大(MAG)**。ボス 24 枚＋壁 8 枚
       FINAL_WALL_LINE ..      : 自機帯   … スプライト表 A・等倍(自機/爆発/宙返り)
   ★自機とボスは帯をまたがない(またぐと上半分だけ拡大されて裂ける。MAG 分割テストで確認)。
     壁(ボス帯の最下段にスプライト 8 枚＝画面幅)がその境界の説明になる。
   ★弾はスプライトを使わず背景に描く(ボス帯は 1 走査線 8 枚をボスと壁が使い切る)。
     この面の背景は海しかないので、消すときは海の模様で塗り直すだけで済む(ovl_final.c)。 */
#ifndef FINAL_H
#define FINAL_H

#include "types.h"

#define STAGE_FINAL      5      /* curstage の値(0..4 が艦、5 が最終面) */
#define STAGE_TOTAL      6

#define FINAL_TOP_LINE   20     /* ここから下がボス帯(スコア数字は y=1..18) */
#define FINAL_WALL_LINE  144    /* 壁の下端＝ボス帯の終わり。自機はこの行より上へ行けない */
#define FINAL_BGM        8      /* イントロ(576f)→本編ループ */

#define BOSS_VRAM_BANK   32     /* コマ(page2/3 分, 約63KB)を bank32.. に連続で置き、面の準備で VRAM へ流す */
#define BOSS_VRAM0_BANK  40     /* 入り切らない残りのコマ(page0 分)。表示が page1 に切り替わってから流す */
#define BOSS_MISC_BANK   43     /* 開始カードの絵 */
#define OVL6_BANK        27     /* 最終面用の RAM オーバレイ */

/* ---- オーバレイ入口(ホット区間の中でのみ。overlay.h の制約に従う) ---- */
void final_init(void);          /* 面の準備: 海の写し/リング/ボスの状態/弱点エンティティ */
u8   final_frame(void);         /* 毎フレーム: スクロール/ボス/分割表。戻り 1=撃墜演出が終わった */
void final_bgbul(void);         /* 弾を背景に描く(ent_draw_all の前に呼ぶ＝スプライトでは描かせない) */

#endif /* FINAL_H */
