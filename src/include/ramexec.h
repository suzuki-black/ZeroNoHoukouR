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

extern u8 g_ramx_ok;   /* 1=RAM化利用可(ramexec_page1_to_ram成功)。0なら切替は無効(ROMのまま) */

/* 起動時1回。page1 ROMを空きセグメントへコピー＋切替準備。成功で1。main が sound_init 前に呼ぶ。 */
u8 ramexec_page1_to_ram(void);

/* ゲームループのホット区間の前後で: RAM=高速化 / CART=バンキング(data_read/bcall)可。
   ★use_ram と use_cart の間では data_read/bcall(0x6000-0x7800書込)を一切呼ばないこと。 */
void page1_use_ram(void);
void page1_use_cart(void);

#endif /* RAMEXEC_H */
