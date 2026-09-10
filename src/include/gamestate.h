/* gamestate.h — ゲーム全体で共有する状態(常駐)。config(バンク)が設定し、gameplay が読む。 */
#ifndef GAMESTATE_H
#define GAMESTATE_H

#include "types.h"

extern u8  g_difficulty;   /* 0=EASY / 1=NORMAL / 2=HARD */
extern u8  g_lives_idx;    /* 残機テーブル添字(0=2 / 1=3 / 2=5) */
extern u8  g_durability;   /* 1機あたりの耐久HP(1..9, 既定3)。設定メニュー(耐久) */
extern u8  g_stage_sel;    /* 開始ステージ(0基点。現状0のみ)。設定メニュー(ステージ選択) */
extern u8  g_continue;     /* 1=ゲームオーバーでコンティニュー可(既定1)。設定メニュー(継続) */
extern u8  g_invinc;       /* 1=無敵(被弾しても残機/耐久を減らさない)。設定メニュー(無敵) */
extern u16 g_score;        /* スコア(撃破で加算) */
extern u16 g_hiscore;      /* ハイスコア(セッション内。将来SRAM保存) */
extern u8  g_lives;        /* 現在の残機(面開始で g_lives_idx から設定) */
extern u8  g_php;          /* 現在の耐久HP(面開始/ミスで g_durability から補充) */

extern u8  g_rage;         /* 1=レイジ(ボス最後の砲台=速射の最終抵抗)。diff_interval が更に短縮 */

/* ★画面ビューア(デバッグ/確認用): config が設定し stage_init が分岐。各画面を個別に表示できる。
   0=通常ゲーム / 1=ステージ説明カードのみ / 2=撃破結果(SUNK)画面のみ。エンディングは config が SC_ENDING を直接返す。 */
extern u8  g_view;

/* 難易度で発火/出現間隔をスケール(EASY=遅い/HARD=速い)。base×{5,4,3}/4、下限1。全系統の発砲・出現に適用。
   レイジ中は更に×2/3。 */
u8 diff_interval(u8 base);

#endif /* GAMESTATE_H */
