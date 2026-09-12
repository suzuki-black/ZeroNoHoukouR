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

/* ★メガクラッシュ(設計メモ §4-1「安い出力で最大の爽快」)。Bボタンで発動。
   画面全体を雷光が覆い(スクロールもBGMも止める)、数瞬のちに画面上の敵弾だけが消える。
   出力はパレット16色の書換だけ＝VDP帯域ほぼゼロ(性能と高速化 §0-0 の「重い計算・軽い出力」)。 */
#define CRUSH_MAX    3    /* 1回の挑戦で使える回数(画面下に表示) */
#define CRUSH_FRAMES 24   /* 発動から再開までの停止フレーム数(30fps で 0.8秒) */
#define CRUSH_WIPE   8    /* 残りフレームがこの値になった瞬間に敵弾を消す(稲妻が走りきった数瞬のち) */
#define CRUSH_ERASE  3    /* 画面に描いた稲妻を消す(世界の正本から引き直す)タイミング */
extern u8  g_crush;        /* 残り使用回数(画面下に表示) */
extern u8  g_crush_t;      /* >0=発動中の残りフレーム。パレットエンジンが雷光の強さに使う */

/* ★画面ビューア(デバッグ/確認用): config が設定し stage_init が分岐。各画面を個別に表示できる。
   0=通常ゲーム / 1=ステージ説明カードのみ / 2=撃破結果(SUNK)画面のみ。エンディングは config が SC_ENDING を直接返す。 */
extern u8  g_view;

/* 難易度で発火/出現間隔をスケール(EASY=遅い/HARD=速い)。base×{5,4,3}/4、下限1。全系統の発砲・出現に適用。
   レイジ中は更に×2/3。 */
u8 diff_interval(u8 base);

#endif /* GAMESTATE_H */
