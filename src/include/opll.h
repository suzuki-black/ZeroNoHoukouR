/* opll.h — MSX-MUSIC(YM2413 / OPLL)の土台。PSG の曲はそのままに、FM を**添えて**厚くする。

   ★turboR は MSX-MUSIC が**規格で必須**(FS-A1ST / FS-A1GT とも内蔵)。MSX2+ では規格には
     入ったが**オプション**で、載っていない機種がある。だから「あるものとして叩く」のではなく
     必ず検出する。

   ★検出(ページ1=0x4000 側の ROM ヘッダを見る):
       0x4018 に "APRLOPLL" … **内蔵 MSX-MUSIC**。I/O 0x7C/0x7D をそのまま叩いてよい
       0x4018 に "PAC2OPLL" … 外付け FM-PAC。I/O を使うには 0x7FF6 の bit0 を立てる
       0x401C に "OPLL"     … FM-BIOS ROM 全般の印(上の 2 つで判別がつくので本作では使わない)
     ★**内蔵が見つかったら 0x7FF6 には絶対に触らない**。触ると Panasonic の MSX2+ で
       互換性が壊れる。「外付けだけを探して内蔵を見ない/0x7FF6 の有効化を前提にする」のが
       MSX 版 R-TYPE 型の事故(外付け FM-PAC でしか鳴らない)。**内蔵を先に探すこと。**
     ★スロットの走査は BIOS の RDSLT(0x000C)で行う。自分のページを差し替えないので安全。

   ★書き込みにはウェイトが要る(OPLL の仕様):
       アドレスレジスタ(0x7C)書込み後 … 3.36us (Z80 の 12 ステート)
       データレジスタ  (0x7D)書込み後 … 23.52us(Z80 の 84 ステート)
     R800 は速いので、ウェイトを入れないと音が化ける(VDP に OTIR を使わないのと同じ理由)。
     本作は**速い方(R800)に合わせて**ループ回数を決める(遅い CPU では長めに待つ＝安全側)。 */
#ifndef OPLL_H
#define OPLL_H

#include "types.h"

#define OPLL_NONE 0
#define OPLL_INT  1   /* 内蔵 MSX-MUSIC */
#define OPLL_PAC  2   /* 外付け FM-PAC(0x7FF6 で I/O を有効にした) */

extern u8 g_opll;     /* 「いま FM を使ってよいか」(OPLL_NONE/INT/PAC)。0 なら以降 FM は一切触らない。
                         ★検出結果に加えて**設定メニュー(FM SOUND)**でも 0 になる。 */
extern u8 g_opll_hw;  /* 検出した**ハード**の方(設定で g_opll を戻すための控え)。設定は書き替えない */
extern u8 g_fm;       /* 設定メニュー: 1=FM を鳴らす(既定) / 0=PSG だけ。実体は gamestate.c */

/* ★検出は**冷たいバンク**(banked/coldsetup.c)にある。起動時に main が
   g_cold_mode=COLD_OPLL にして bcall_to(COLDSETUP_BANK) で1回だけ呼ぶ。
   起動時しか使わないものを常駐へ置くと、曲へ繋ぐぶんの枠が無くなるため。 */
#define COLD_STAGE 0   /* coldsetup の用件: 面の配置(既定) */
#define COLD_OPLL  1   /* 同: FM の検出＋消音(起動時1回) */
extern u8 g_cold_mode;
void opll_w(u8 reg, u8 val);   /* レジスタ書込み(規定のウェイト込み)。g_opll=0 なら何もしない */

#endif /* OPLL_H */
