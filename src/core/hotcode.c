/* hotcode.c — ホットコードRAM実行の常駐側(RAM予約＋起動時コピー)。実体は banked/hot.c。詳細は hotcode.h。 */
#include "hotcode.h"
#include "bank.h"
#include "aa_hot.h"   /* aa_update/aa_collide ラッパの宣言 */
#include "entity.h"   /* ent_update_all ラッパの宣言 */

/* RAM実行領域。SDCC(z80)は未初期化globalを _DATA に置くので、常駐RAM末尾に HOT_CAP バイト確保される。
   その実番地(_hot_ram)を Makefile が rom.noi から拾い、hot.c の --code-loc に渡す(番地の二重管理を排除)。 */
u8 hot_ram[HOT_CAP];

/* 起動時に1回だけ。data_read はバンク窓(0xA000)を差替えて読むので常駐から呼ぶ必要がある(main から呼ぶ)。 */
void hot_load(void) {
    data_read(HOT_BANK, 0, hot_ram, HOT_CAP);
}

/* ジャンプテーブル該当スロットへ飛ぶ薄いラッパ(常駐)。実体は hot_ram(RAM)の hothead.s→本体。
   ★C の関数ポインタ呼び出しは定数畳み込みで `jp (nn+3)`(Z80に無い不正形)を吐くSDCCの癖があるため、
     __naked＋インラインasm の絶対 jp で確実に飛ばす(tail-jump=本体のretで元の呼び元へ戻る)。 */
void aa_update(void) __naked      { __asm jp _hot_ram __endasm; }       /* slot0 = HOT_SLOT_AA_UPD (hot_ram+0) */
void aa_collide(void) __naked     { __asm jp _hot_ram + 3 __endasm; }   /* slot1 = HOT_SLOT_AA_COL (hot_ram+3) */
void ent_update_all(void) __naked { __asm jp _hot_ram + 6 __endasm; }   /* slot2 = HOT_SLOT_UPDATE (hot_ram+6) */
