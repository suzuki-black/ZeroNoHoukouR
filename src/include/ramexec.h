/* ramexec.h — §4-3: page1(0x4000-0x7FFF)の常駐ホットコードをマッパーRAMの空きセグメントへ
   コピーし、page1 のスロットを RAM スロットへ切替える。以降 page1 のコードフェッチが
   R800 で約3.8倍速(実測 RATIO=3.8)になる。ROMフェッチ律速の根本対策。
   ★仕組み(全て起動時・sound_init(ISR設置)より前・割込禁止で1回):
     1. page3(=RAM)のスロットを基本スロットレジスタ(0xA8 bit6-7)から得る。
     2. 空きセグメント(RAM_FREE_SEG)が実RAMか page2 窓でプローブ(非RAMなら中止=ROMのまま)。
     3. page2 に空きセグメントを一時マップし、page1 ROM(0x4000-0x7FFF)を LDIR で 0x8000 へコピー。
     4. page2 をカートリッジ(元)へ復元し、page1 を「RAMスロット＋空きセグメント」へ切替。
        以後 page1(0x4000-0x7FFF)は同一内容のRAM=同一番地なので再配置不要・呼び出しはそのまま有効。
   ★切替ルーチン自身は page1/2 を触るため page3(RAM)へ退避したコピーから実行する(位置独立asm)。
   ★C-BIOS(MSX2+)も slot1=cart/slot3=RAM/標準マッパーなので同じ経路で動く=openMSXで正当性検証可能
     (速度差はturboRのみ。挙動は不変)。マッパー/RAMが期待通りでなければ何もしない(安全側)。 */
#ifndef RAMEXEC_H
#define RAMEXEC_H

#include "types.h"

/* ★page2(0x8000-0x9FFF = 常駐 bank2)の RAM 実行(本版で追加)
   page1 だけを RAM 化していた頃は、常駐 24KB のうち 0x8000 以降(約4.5KB)が ROM フェッチのまま残り、
   そこに毎フレームの stage_update と SDCC の乗除算ランタイム(__mulint/__divuint/…)が落ちていた
   ＝「一番熱いコードが一番遅い側」。どの関数が 0x8000 を越えるかはリンク順の副作用でしかなく、
   熱さで選ばれていない。→ page2 も RAM スロットへ切替え、常駐 24KB 全体を RAM 実行にする。
   ★page2 を RAM にすると 0xA000-0xBFFF のスワップ窓が消えるが、バンキング前に cart へ戻す規律は
     page1 と全く同じなので、既存の出入口(ramx_use_ram/cart)にそのまま相乗りできる。
   ★page1 の切替と違い、page2 の切替コードは page1 に居るので page3 退避 blob が要らない
     (自分の足元を切らないため)。ただし複製時だけは 0x6000-0x7FFF 窓を一時 bank2 に差し替えるので、
     複製関数は 0x4000-0x5FFF に居ること(Makefile がリンク後に番地を検証する)。 */

extern u8 g_ramx_ok;    /* 1=page1 RAM化利用可(ramexec_page1_to_ram成功)。0なら切替は無効(ROMのまま) */
extern u8 g_ramx2_ok;   /* 1=page2 RAM化利用可(ramexec_page2_to_ram成功)。0なら page2 は cart のまま */
extern u8 s_ram_slot;   /* RAMスロットID(F000SSPP)。overlay.c が自前 ENASLT に使う */
extern u8 s_cart_slot;  /* カートリッジスロットID(F000SSPP) */

/* 起動時1回。page1 ROMを空きセグメントへコピー＋切替準備。成功で1。main が sound_init 前に呼ぶ。 */
u8 ramexec_page1_to_ram(void);
/* 起動時1回(page1 の後)。常駐 bank2 を別の空きセグメントへ複製＋切替準備。成功で1。
   複製後にチェックサムで一致を検証し、違えば g_ramx2_ok=0(=cartのまま=安全側)。 */
u8 ramexec_page2_to_ram(void);

/* ゲームループのホット区間の前後で: RAM=高速化 / CART=バンキング(data_read/bcall)可。
   ★use_ram と use_cart の間では data_read/bcall(0x6000-0x7800書込・0xA000読み)を一切呼ばないこと。 */
void ramx_use_ram(void);    /* page1＋page2 を RAM スロットへ(常駐24KB全体が RAM 実行) */
void ramx_use_cart(void);   /* page1＋page2 を カートリッジへ(バンキング可) */

/* 個別版(ramx_use_* の内訳。通常は上の対を使うこと)。 */
void page1_use_ram(void);
void page1_use_cart(void);
void page2_use_ram(void);
void page2_use_cart(void);

#endif /* RAMEXEC_H */
