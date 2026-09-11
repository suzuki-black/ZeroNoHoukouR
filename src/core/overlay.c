/* overlay.c — 演出コードの RAM オーバレイ(常駐側)。設計と制約は overlay.h を参照。 */
#include "overlay.h"
#include "ramexec.h"
#include "curtain.h"

u8 g_ovl_ok;

/* ★0x4000-0x5FFF に居ること(overlay.h の制約)。Makefile がリンク後に検証する。 */
void overlay_load(u8 bank) {
    u16 k;
    const volatile u8 *s;
    volatile u8 *d;

    g_ovl_ok = 0;
    if (!g_ramx2_ok) return;   /* page2 を RAM 化できない機械ではオーバレイも使えない(安全側) */

    __asm di __endasm;
    *(volatile u8 *)0x6800 = bank;     /* 0x6000-0x7FFF 窓 ← 演出バンク(複製元) */
    /* page2 → RAMスロット+seg5。★ei を挟みたくないので page2_use_ram() は使わず自前で。
       ENASLT は AF/BC/DE/HL を壊すので前後で退避する(苦労と教訓 §5-1)。 */
    __asm
        push af
        push bc
        push de
        push hl
        ld   a, #5
        out  (0xFE), a
        ld   a, (_s_ram_slot)
        ld   h, #0x80
        call 0x0024
        pop  hl
        pop  de
        pop  bc
        pop  af
    __endasm;
    s = (const volatile u8 *)0x6000;   /* 演出バンク(ROM) */
    d = (volatile u8 *)OVL_ADDR;       /* seg5 の上位8KB(RAM) */
    for (k = 0; k < OVL_CAP; k++) d[k] = s[k];
    __asm
        push af
        push bc
        push de
        push hl
        ld   a, (_s_cart_slot)
        ld   h, #0x80
        call 0x0024                    ; page2 → cart へ復元
        pop  hl
        pop  de
        pop  bc
        pop  af
    __endasm;
    *(volatile u8 *)0x6800 = 1;        /* 0x6000-0x7FFF ← bank1(既定) */
    *(volatile u8 *)0x7000 = 2;        /* 0x8000-0x9FFF ← bank2 */
    *(volatile u8 *)0x7800 = 3;        /* 0xA000-0xBFFF ← bank3(スワップ窓既定) */
    __asm ei __endasm;

    g_ovl_ok = 1;
}

/* ---- オーバレイ入口への薄いラッパ(常駐) ----
   ★ホット区間(page2=RAM)の中でしか呼べない。外で呼ぶと 0xA000 は ASCII8 スワップ窓＝暴走。
   ★hotcode.c と同じく __naked + 絶対 jp。tail-jump なので本体の ret が元の呼び元へ戻る。 */
void curtain_update(void) __naked { __asm jp 0xA000 __endasm; }
void curtain_ring(s16 cx, s16 cy, u8 n, u8 spd, u8 ang, u8 col) __naked {
    (void)cx; (void)cy; (void)n; (void)spd; (void)ang; (void)col;
    __asm jp 0xA003 __endasm;
}
void curtain_draw(u8 base, u8 nper, u8 line) __naked {
    (void)base; (void)nper; (void)line;
    __asm jp 0xA006 __endasm;
}
void curtain_collide(void) __naked { __asm jp 0xA009 __endasm; }
