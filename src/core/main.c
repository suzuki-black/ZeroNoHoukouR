/* main.c — エントリ。ここは「起動初期化 → シーンFSM へ委譲」だけ。
   ゲームロジックは書かない(巨大 main() を作らないという規律の起点)。 */
#include "sys.h"
#include "sound.h"
#include "scene.h"
#include "hotcode.h"
#include "ramexec.h"
#ifdef DEBUG_PROF
#include "prof.h"
#endif

void main(void) {
    sys_init();           /* turboR: R800 ブースト等          */
    hot_load();           /* AA(対空砲)処理の本体を bank→hot_ram(RAM実行)へコピー(機種非依存で常時) */
    ramexec_page1_to_ram(); /* ★§4-3: page1(常駐ホットコード)を空きRAMセグメントへ複製し動的切替を準備
                               (成功でg_ramx_ok=1。ISR設置前・page1=cartのうちに1回。失敗時はROMのまま=安全) */
#ifdef DEBUG_PROF
    prof_selftest();      /* 実機µs自己診断(CPUモード比/VDP I/O単価/HMMM所要)→トリガで抜ける */
#endif
    sound_init();         /* PSG初期化 + H.TIMI 60Hz ISR 設置 */
    scene_run(SC_TITLE);  /* タイトル(SCREEN12/YJK)から。以降ここから戻らない */
}
