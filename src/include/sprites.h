/* sprites.h — スプライトパターンの集約。各シーンは init で sprites_load() を1回呼ぶ。
   パターン番号は 4 の倍数(mode2 16x16 は 4パターン=32B 消費)。 */
#ifndef SPRITES_H
#define SPRITES_H

#include "types.h"

#define SPR_BLOCK   0   /* 16x16 中実(自機/汎用マーカ) */
#define SPR_BULLET  4   /* 弾(中央 6x6)                */
#define SPR_FIGHTER 8   /* 敵戦闘機(下向き△)          */
#define SPR_DIGIT0  12  /* 数字0のパターン番号。数字d = SPR_DIGIT0 + d*4(HUD用, BIOSフォント) */
#define SPR_TURRET  52  /* 戦艦の主砲塔(灰の砲塔＋2連装砲身)。数字は12..48を占有→次は52 */

/* 海イントロ敵機=各面ボス艦の所属国の当時の典型機(上面視・機首下向き)。番号は4刻み。
   ハイブリッド識別: 形(主翼の平面形)＋視認性優先色(scene_stage の fighter_col)。 */
#define SPR_BF109    56  /* 独: Bf109 = 細い先細り翼 */
#define SPR_CORSAIR  60  /* 米: F4U = 逆ガル翼(曲がった翼) */
#define SPR_SPITFIRE 64  /* 英: Spitfire = 楕円翼 */
#define SPR_FW190    68  /* 独: Fw190 = 幅広翼＋太い機首 */
#define SPR_HELLCAT  72  /* 米: F6F = 幅広角形の直線翼(ずんぐり) */

/* 自機=零戦(A6M, 機首上向き)。プロペラ回転の2コマ(先頭行を交互)。色は zcol の行別陰影。 */
#define SPR_ZERO    76   /* 零戦 コマA(プロペラ細) */
#define SPR_ZERO2   80   /* 零戦 コマB(プロペラ太=回転ブラー) */
#define SPR_PBULLET 84   /* 自機弾=赤い縦ストリーク(旧pat44) */
#define SPR_EBSHELL 88   /* 敵の時限信管弾=太いカプセル(旧pat100)。通常敵弾より大きく予告的 */
#define SPR_EXP0    92   /* 爆発アニメ4コマ(核→炸裂→輪→残火)。撃破/被弾で使用 */
#define SPR_EXP1    96
#define SPR_EXP2    100
#define SPR_EXP3    104
#define SPR_FLASH   108  /* マズルフラッシュ(自機発砲時の一瞬の光) */
#define SPR_BARREL0 112  /* 主砲の可動砲身(8方向, dir d = SPR_BARREL0 + d*4)。自機を狙って回転。112..140 */
/* ★敵戦闘機/艦載機の8方向スプライト(手続き生成, 面別ロード)。各サイズ 8方向×4パターン=32枠。
   小=浮上初期/中=浮上後期/大=飛行。パターン番号 = base + dir*4 (dir 0=上,時計回り=dir8と一致)。 */
#define SPR_PLANE_S 160  /* 小(発艦ホバー初期)   160..188 */
#define SPR_PLANE_M 192  /* 中(発艦ホバー後期)   192..220 */
#define SPR_PLANE_L 224  /* 大(通常飛行)         224..252 */
extern const u8 zcol[16];        /* 零戦の16行カラーテーブル(緑系+ハイライト) */
extern const u8 barrel_col[16];  /* 砲身の行別シェード(金属感の多色) */
extern const u8 barrel_flash[16];/* 砲身の命中フラッシュ(白) */

void sprites_load(u8 stage);   /* 全静的パターン＋その面の戦闘機8方向×3サイズ を VRAM へ投入 */

#endif /* SPRITES_H */
