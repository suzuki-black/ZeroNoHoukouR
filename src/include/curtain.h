/* curtain.h — CPU 弾幕(設計メモ §2-D「本物の弾幕」)。
   ★狙い: 弾の"計算"は純 RAM 演算で VDP コストがゼロ＝実測で確定した天井(VDP 204B/ms)を避けられる。
     制約は表示側だけなので、ラスタ分割で確保した予約スロットへ帯ごとに流し込む。
     エンティティプール(ENT_MAX=30)とは別系統にしてあるのは、
       ・常駐 RAM が 0xE000 天井まで残り 24B しかなく Entity(数十B)を増やせない
       ・毎フレーム全プール走査するエンティティの重い behavior 経路に弾を載せたくない
     の 2 点による。1発 8B の軽い構造体を高位フリー帯へ固定配置する。

   ★座標は 1/16 px の固定小数(s16)。画面 0-255px = 0-4080 なので余裕がある。
     速度は 1/16 px/frame の s8(±127 = ±7.9px/frame)。
     方向は fire.c と同じ 32分割(dvx/dvy を共有)。

   ★当たり判定は ovl_curtain_collide。被弾処理は常駐 ent_player_hit に集約してあるので、
     無敵時間・耐久・ミス判定・手応えが既存の敵弾と完全に同じ挙動になる。 */
#ifndef CURTAIN_H
#define CURTAIN_H

#include "types.h"

#define CBUL_MAX  64          /* プールの大きさ(計算上の上限) */
#define CBUL_SOFT_MAX 20      /* ★同時生存の実効上限。表示能力(予約slot×帯数)を超えて撒くと、
                                 どの弾を出すかが毎フレーム変わって**ちらつき**になる。
                                 計算自体は 64発でも余裕だが、出せない弾は抱えない。 */
#define CBUL_ADDR 0xEC00      /* ★高位フリー帯へ固定配置(常駐DATAは 0xE000 天井で空きが無い)。
                                 既存: g_card_ram 0xE100 / ship_ram 0xE700 / fb_ram 0xE900 /
                                 prof 0xEB00。0xEC00-0xEDFF の 512B を使う(BIOSスタックは 0xF380 付近) */

typedef struct {
    s16 x, y;    /* 1/16 px */
    s8  vx, vy;  /* 1/16 px per frame */
    u8  alive;
    u8  col;     /* スプライト色 */
} CBul;          /* 8B */

extern CBul __at(CBUL_ADDR) g_cbul[CBUL_MAX];
extern u8 g_cbul_live;       /* 生存数(デバッグ表示/上限判定用) */

void curtain_reset(void);
/* cx,cy は px。n 方向へ等間隔に spd(1/16px単位の半径8基準)で撒く。ang は開始角(32分割)。 */
void curtain_ring(s16 cx, s16 cy, u8 n, u8 spd, u8 ang, u8 col);
void curtain_update(void);   /* 移動＋画面外カリング。VDP に一切触れない＝§4-1 の並列区間に置ける */
/* 予約スロットへ帯ごとに描く。base=最初の予約slot, nper=1帯あたりの枚数, line=分割行。
   上帯(y<line)はセットA、下帯はセットBへ。どちらも分割線をまたがない位置のものだけを選ぶ。 */
void curtain_draw(u8 base, u8 nper, u8 line);
void curtain_collide(void);  /* 自機との当たり(被弾処理は常駐 ent_player_hit に集約) */

#endif /* CURTAIN_H */
