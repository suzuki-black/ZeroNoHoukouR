/* prof.h — DEBUG_PROF: turboR実機で「実時間µs」を測る計測基盤。
   turboR の S1990 システムタイマ(ポート0xE6/0xE7, 255682Hz, 1tick≒3.911µs, 16bit)を使い、
   起動時に前提を実機で確定する自己診断を表示する:
     (1)CPUモード比: 同一ループをROM上/RAM上で走らせtick比較(≒4=R800 DRAM / ≒1=Z80)。
     (2)VDP I/O単価: 0x99への大量OUTのtickから1回あたりのウェイトを実測。
     (3)HMMM所要: 256x8コピー(1KB)の発行→完了までのtick。
   ★S1990タイマは turboR機種にのみ存在。Z80(C-BIOS/openMSX)ではポートが空=値は無意味(クラッシュはしない)。
   ★make DEBUG_PROF=1 でのみ有効。未指定ビルドには一切コード/負荷が乗らない(#ifdefで完全に消える)。 */
#ifndef PROF_H
#define PROF_H

#include "types.h"

#define PROF_BANK 20   /* banked/prof_bank.c を置くROMバンク(自己診断画面と区間表示=冷たい側) */
extern u8 g_prof_mode;  /* bank20 への用件: 0=自己診断 / 1=区間表示(引数を取れない bcall の代わり) */

void prof_selftest(void);   /* 起動時1回: 自己診断を画面表示しトリガ押下で抜ける(main が scene_run 前に呼ぶ) */

/* ===== 実行時 区間計測 =====
   ゲームループの各区間の滞在tickを60フレーム蓄積→凍結表示(Mキーで進む)。「何に何ms」を実機で確定する。 */
enum { PF_COMPUTE, PF_CMDWAIT, PF_WAIT, PF_DRAW, PF_UPDATE, PF_AA, PF_COL, PF_SEASCROLL, PF_FIRE, PF_SCROLL, PF_N };

/* ★計測用RAMは常駐_DATAでなく高位フリー帯(0xEB00-)へ固定する。
   常駐_DATA が 0xE000 を越えると、バンクシーンの static(--data-loc 0xE000)に踏み潰される
   ＝設定値(g_view 等)が化けて「タイトルでSPACE→即タイトルへ戻る」等の怪奇現象になる(実際に踏んだ)。
   DEBUG_PROF は 100B 強を足すのでこれを越えていた。高位の固定帯は
   g_card_ram(0xE100)/ship_ram(0xE700)/fb_ram(0xE900,512B) の直後＝0xEB00 以降が空き。
   Makefile が常駐_DATA末尾 < 0xE000 をリンク後に機械検証する。 */
#define PROF_RAM_ADDR 0xEB00
extern u32 __at(PROF_RAM_ADDR) g_prof_acc[PF_N];   /* 区間別 蓄積tick(60フレーム窓)。★u32(フルフレーム4200tick×60=25万でu16溢れ) */
extern u16 g_prof_over;        /* 窓内で1VBLANK(4262tick)を超えた計算フレーム数 */

u16  prof_tick(void);          /* S1990タイマ現在値(tick)。区間の前後で読んで差=滞在tick */
void prof_frame_end(u16 compute_ticks);  /* 各フレーム末に呼ぶ。60毎に凍結表示&リセット */

/* 区間マクロ: 非ネスト区間の前後で使う(t0はローカルに置く)。 */
#define PROF_T0(v)      u16 v = prof_tick()
#define PROF_ADD(i, v)  (g_prof_acc[i] += (u16)(prof_tick() - (v)))

#endif /* PROF_H */
