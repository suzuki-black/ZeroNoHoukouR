/* hotcode.h — ホットコードのRAM実行(ROM→RAM)。
   R800はカートリッジROM上のコードフェッチにウェイトが入りZ80相当まで落ちる(内蔵RAMのみ高速)。
   毎フレームのホットパスを起動時にRAMへコピーし、そこから実行して律速を外す。
   現在RAM実行しているのは AA(対空砲)処理: aa_update / aa_collide(実機で改善を確認)。
   ※当たり判定(ent_resolve_collisions)もRAM化を試したが速度不変だったため ROM実行(entity.c)へ戻した。
   仕組み:
     ・banked/hot.c を hot_ram[] の実行番地に --code-loc してリンク(Makefile が rom.noi から番地供給)
     ・先頭は hothead.s のジャンプテーブル(1関数=3バイトの jp。番地 hot_ram+3*slot が各入口)
     ・rompack が hot.bin を bank HOT_BANK へ格納 → 起動時 hot_load() が hot_ram[] へコピー
     ・常駐 hotcode.c の aa_update/aa_collide が hot_ram のスロットへ飛ぶ薄いラッパ
   ★コピーは機種非依存で常時行う(Z80のC-BIOS/openMSXでも同一コードが走る=正当性を実機外で検証可能)。 */
#ifndef HOTCODE_H
#define HOTCODE_H

#include "types.h"

#define HOT_BANK 17     /* rompack --bank 17 build/hot.bin(冷たいバンク帯 bank4+ の空き) */
#define HOT_CAP  5312   /* hot_ram 予約バイト数。hot.bin(実測=aa_update+aa_collide+ent_update_all+behavior群+表)以上、
                           かつ常駐DATA末尾が 0xE000(バンクデータ)未満に収まること。★page3のRAM実行枠は 0xE000 が天井。
                           冷データ g_card_ram(0xE100)/ship_ram(0xE700)/fb_ram(0xE900) を高位固定へ退避して枠を確保済み。
                           ★hot.c 肥大時はここを必ず更新すること(不足すると hot_load のコピーが末尾を落とし、
                             かつ hot_ram[] を超えて後続の常駐グローバル g_scene/curstage を破壊→海イントロで暴走。
                             8方向スプライト化で hot.c=5199B に増えたため 5120→5312 へ。Makefile が超過をビルド時検出)。 */

/* hot_ram 先頭は hothead.s のジャンプテーブル(1関数=3バイトの jp)。番地 hot_ram+3*slot が各関数の入口。
   関数を1つRAM化するたびに hothead.s へ jp を1行、下の HOT_SLOT_* を1つ追加し、対応ラッパを増やす。 */
#define HOT_SLOT_AA_UPD   0   /* aa_update(対空砲の走査＋発砲) */
#define HOT_SLOT_AA_COL   1   /* aa_collide(自機弾×対空砲) */
#define HOT_SLOT_UPDATE   2   /* ent_update_all(全エンティティの behavior=移動/AI/発砲) */

extern u8 hot_ram[HOT_CAP];   /* RAM実行領域(常駐_DATAに予約)。リンク番地は rom.noi の _hot_ram を参照 */
void hot_load(void);          /* 起動時1回: bank HOT_BANK の先頭 HOT_CAP バイトを hot_ram[] へ転写 */

#endif /* HOTCODE_H */
