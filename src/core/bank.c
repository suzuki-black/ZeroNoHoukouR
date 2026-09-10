/* bank.c — ASCII8 バンク切替の常駐実装。
   バンク選択は 0x7800 への書込のみ(他窓に書くと暴走)。
   g_bank / bcall のトランポリン本体は crt0rom.s(_bcall)側。 */
#include "bank.h"

u8 g_bank;   /* crt0 の _bcall が参照。bcall() 前に呼ぶバンク番号を入れる。 */

void bank_data(u8 n) {
    *(volatile u8 *)0x7800 = n;
}

void bank_restore(void) {
    *(volatile u8 *)0x7800 = BANK_DEFAULT;
}

void bcall_to(u8 bank) {
    g_bank = bank;
    bcall();
}

/* データバンク bank のオフセット off から len バイトを dst(RAM)へコピー。
   窓(0xA000)を bank に差替え→読了→既定(bank3)へ復元。切替中は di(割込みISRが
   窓のコードを踏む/半差替えを読むのを防ぐ)。★呼び元は必ず常駐(バンクシーン内から
   呼ぶと復元でbank3になり自シーンを窓から追い出す)。 */
void data_read(u8 bank, u16 off, u8 *dst, u16 len) {
    const u8 *src = (const u8 *)(0xA000 + off);
    u16 i;
    __asm di __endasm;
    bank_data(bank);
    for (i = 0; i < len; i++) dst[i] = src[i];
    bank_restore();
    __asm ei __endasm;
}
