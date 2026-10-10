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

#define PROF_BANK 31   /* banked/prof_bank.c を置くROMバンク(自己診断画面と区間表示=冷たい側)。★Makefile の --bank 31 と揃える(29 は撃沈演出 ovl13) */

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
/* ★小さい変数も常駐_DATA に置かない(この版は計測ぶんだけ常駐RAM が増え、1B 超えて 0xE000 を踏んだ)。
   g_prof_acc(40B)の直後 0xEB28〜。0xEB40 からは prof_bank の rambuf */
#define PROF_OVER_ADDR (PROF_RAM_ADDR + 0x28)
#define PROF_MODE_ADDR (PROF_RAM_ADDR + 0x2A)
#define PROF_FC_ADDR   (PROF_RAM_ADDR + 0x2B)   /* prof_frame_end のフレーム数 */
#define PROF_T0_ADDR   (PROF_RAM_ADDR + 0x2C)   /* prof_begin の開始 tick(2B) */
#define PROF_PC_ADDR   (PROF_RAM_ADDR + 0x2E)   /* 計算区間の開始 tick(2B)。0xEB30〜0xEB3F は空き */
extern u8  __at(PROF_MODE_ADDR) g_prof_mode;   /* bank31 への用件: 0=自己診断 / 1=区間表示(引数を取れない bcall の代わり) */
extern u16 __at(PROF_OVER_ADDR) g_prof_over;   /* 窓内で1VBLANK(4262tick)を超えた計算フレーム数 */
extern u8  __at(PROF_FC_ADDR)   g_prof_fc;     /* ★これらは起動時に 0 にならない(__at)。自己診断(prof_bank.c)が 0 にする */

void prof_compute_begin(void);   /* 計算区間(入力+更新)の始まり。scene_run がフレームの頭で呼ぶ */
void prof_compute_end(void);     /* 同 終わり(フレーム同期の前): PF_COMPUTE へ足し、1VBLANK 超えを数える */
void prof_frame_end(void);       /* フレーム同期の後: 60 フレームごとに凍結表示&リセット */
void prof_add(u8 i, u16 t0);   /* g_prof_acc[i] += 今 - t0 */
void prof_begin(void);         /* 入れ子にしない区間の始まり(開始 tick は固定番地に 1 つだけ) */
void prof_end(u8 i);           /* 同 終わり: prof_begin からの tick を g_prof_acc[i] へ */
/* ★区間の計測は必ず関数呼び出しで。以前は u32 の足し算を呼ぶ所ごとに展開しており(1 か所 50B 前後)、
     この版の常駐が 24KB を 1KB 近く超えた。tick の読み取りは pcm の _pcm_now(HL を返す)。 */

#endif /* PROF_H */
