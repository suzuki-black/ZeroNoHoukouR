/* scene.h — シーンFSM(汎用)。title/空戦イントロ/戦艦ボス/撃破/ending/gameover を
   すべて {init, update} の関数対で表現し、巨大 main() を作らない(SDCC破綻回避)。

   将来の拡張(冷たいシーンをバンク化):
     Scene に bank を持たせ、bank!=0 の場合はディスパッチャが g_bank=bank; bcall() で
     当該バンクの 0xA000 エントリを呼ぶ(そのエントリが init/update を g_scene_phase で分岐)。
     今は常駐シーン(bank=0)のみ実装。詳細は docs/ARCHITECTURE.md。 */
#ifndef SCENE_H
#define SCENE_H

#include "types.h"

#define SCENE_NONE 0xFF   /* update の戻り値: シーン継続(遷移なし) */

/* シーンID(登録順)。追加時はここと scene.c の registry を対で更新。
   起動直後にタイトル(SCREEN12/YJK)から開始する。旧・疎通ブート(scene_boot)は土台実証済で撤去。 */
enum {
  SC_TITLE = 0, /* タイトル(冷たいシーン=バンク5)。起動シーン */
  SC_CONFIG,    /* 設定メニュー(冷たいシーン=バンク6) */
  SC_STAGE,     /* ★1本の連続縦スクロール面(海→戦艦 地続き) */
  SC_ENDING,    /* エンディング(冷たいシーン=バンク7) */
  SC_COUNT
};

typedef struct {
  void (*init)(void);     /* 常駐シーン: 遷移時に1回(バンクシーンは0)      */
  u8   (*update)(void);   /* 常駐シーン: 毎フレーム→次ID(バンクシーンは0)  */
  u8   bank;              /* 0=常駐 / 非0=当該バンクで bcall(冷たいシーン)  */
} Scene;

/* start シーンから開始し、以後メインループを回す(ROM: 戻らない)。 */
void scene_run(u8 start);
extern u8 g_scene;         /* 現在のシーンID(SC_*)。デバッグ/HUD/検証用     */
extern u8 g_scene_phase;   /* バンクシーンへの指示: 0=init / 1=update       */
extern u8 g_scene_ret;     /* update の戻り値(次シーンID)。バンクシーンが書く */

#endif /* SCENE_H */
