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
/* ★asm(LDIR)。C のループは 1 バイトごとに ROM からコードを読み直すので、中ボスの向きの 1 回(約 1.1KB)に 20ms かかり、
     双子の中ボスが向きを変える間はフレームが半分に落ちていた(2026-10-31。openMSX で実行位置を標本取りして確認)。
   A = bank、DE = off、スタックに dst・len(呼ばれた側が片付ける) */
void data_read(u8 bank, u16 off, u8 *dst, u16 len) __naked {
    (void)bank; (void)off; (void)dst; (void)len;
    __asm
        di
        ld   (0x7800), a
        ld   a, d
        add  a, #0xA0
        ld   h, a
        ld   l, e                ; HL = 0xA000 + off
        pop  af                  ; 戻り番地
        pop  de                  ; DE = dst
        pop  bc                  ; BC = len
        push af
        ld   a, b
        or   c
        jr   z, 00001$
        ldir
    00001$:
        ld   a, #BANK_DEFAULT
        ld   (0x7800), a
        ei
        ret
    __endasm;
}
