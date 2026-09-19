/* midboss.h — 中ボス(ROADMAP B4)。常駐(scene_stage.c)と中ボス用オーバレイ(ovl_midboss.c)の取り決め。
   流れ: 海の区間で出現位置に来たら常駐が g_mb=MB_LOAD → ホット区間の外でオーバレイを OVL8 へ入れ替え、
   借りるパターン行を退避して mb_init → g_mb=MB_ACTIVE。オーバレイが終われば g_mb=MB_RESTORE →
   常駐がホット区間の外でパターン行を書き戻し、通常面のオーバレイへ戻して g_mb=MB_OVER。
   ★向きのデータ(1方向1024B, tools/gen_fw200.py): オーバレイは ROM を読めないので、向きを変えたいとき g_mb_req に
     番号を置く → 常駐がフレームの終わり(ホット区間の外)で MB_BUF へ読み g_mb_new=1 → 次のフレームでオーバレイが
     パターン・色・位置をまとめて書く(絵と位置が食い違うフレームを作らない)。 */
#ifndef MIDBOSS_H
#define MIDBOSS_H

#include "types.h"
#include "sprites.h"

#define MB_NONE     0
#define MB_ACTIVE   1
#define MB_RESTORE  2
#define MB_OVER     3
#define MB_LOAD     4

#define OVL8_BANK      24   /* 中ボス用オーバレイ(通常面のものから主砲の弾幕を抜き、中ボスを足したもの) */
#define MB_FRAMES_BANK 44   /* Fw 200 の 32 方向×1024B(44..47。1バンク8方向) */
#define FW_SH_BANK     62   /* Fw 200 の影 32方向×128B */
#define MB_NDIR        32
#define OVL9_BANK      25   /* 2面の中ボス(PBY カタリナ)用オーバレイ。入口は ovlhead8.s を共用 */
#define PBY_BANK       48   /* PBY の 6段階の大きさ×16方向×1024B(48..59。1バンク8件) */
#define PBY_SH_BANK    60   /* PBY の影 16方向×128B を2周(常駐は 添字&31 で引く) */
#define OVL10_BANK     26   /* 4面の中ボス(He 111 ×2)用オーバレイ */
#define OVL11_BANK     63   /* 3面の中ボス(駆逐艦)用オーバレイ */
#define OVL12_BANK     3    /* 5面の中ボス(P-61)用オーバレイ(最後の空きバンク。スワップ窓の既定ページだが --bank 3 は可) */
#define DD_BANK        60   /* 駆逐艦の絵は bank60 の 4096〜(前半は PBY の影) */
#define HE_BANK        20   /* He 111 の 32方向×1024B(20..23) */
#define HE_SH_BANK     61   /* He 111 の影 32方向×128B */
#define MB_TWIN_SPLIT  106  /* 4面: 2機は画面の中心(106行)について点対称=分割線はいつもここ */
#define MB_PATB        0x2000   /* 4面: 下の帯の絵の表(page0 の 64..79 行。R#6=0x04) */
#define MB_R6_B        0x04
#define MB_SBUF        0xEB80   /* 影のパターン128B の置き場(0xEB00〜は DEBUG_PROF の計測 40B。その後ろ) */
#define MB_BUF         0xE700   /* 向きのデータ1024B の置き場(ship_ram/fb_ram の番地。どちらも面の準備でしか使わない) */
#define MB_SAVE_Y      168  /* page0: 借りる前のパターン行の退避先(247,248 → 168,169 / 250..253 → 170..173) */
/* ★パターンは 16x16 を 22 枚ぶん借りる(本体16マス＋重ね6)。どれも戦艦の区間か 2面以降でしか使わない:
     247/248 行 = 主砲の砲身 SPR_BARREL0..(112..143)   = 本体 c=0..7
     250/251 行 = 発艦中の小さい艦載機 SPR_PLANE_S..   = 本体 c=8..15
     252/253 行 = 発艦中の中くらいの艦載機 SPR_PLANE_M.. = 重ね j=0..5 */
#define MB_PAT_ROW_A   247
#define MB_PAT_ROW_B   250
#define MB_CELL_PAT(c) ((u8)((((c) < 8) ? SPR_BARREL0 : SPR_PLANE_S) + (((c) & 7) << 2)))
#define MB_OV_PAT(j)   ((u8)(SPR_PLANE_M + ((j) << 2)))   /* 2面は重ね j=0,1 と影 j=2..5 */

extern u8 g_mb;
extern u8 g_mb_n;     /* 中ボスがいま使っているスプライト枚数。32-g_mb_n 以降を使う(最低優先)。エンティティはその手前まで */
extern u8 g_mb_req;   /* オーバレイ→常駐: 読んでほしい向き(0xFF=なし) */
extern u8 g_mb_new;   /* 常駐→オーバレイ: MB_BUF に新しい向きが入った */
extern u16 g_mb_pat_off;       /* mb_upload が書くパターン表の番地に足す量(0=表A / 4面の2機目は MB_PATB-0x7800) */
extern u16 g_mb_bm, g_mb_om;   /* いま VRAM に載っている絵の 本体のマス / 重ねのマス(mb_upload が更新) */
/* 常駐(両オーバレイ共用): MB_BUF の絵をパターン表へ書く。行A へ本体 256B、行B へ続く rowb バイト(本体の残り＋重ね＋影)。
   影を書くときは sh=1(MB_SBUF の 128B を続けて書く)。マスの表を更新し、スプライト枚数(本体＋重ね＋sh?4:0)が変わったら
   エンティティ側の色キャッシュを捨てる。g_mb_new を下ろす。 */
void mb_upload(u16 rowb, u8 sh);
/* 常駐(両オーバレイ共用): 中ボスの枠を全部隠して終わりを知らせる(g_mb=MB_RESTORE。以降は常駐が片付ける) */
void mb_finish(void);

#endif /* MIDBOSS_H */
