/* overlay.c — 演出コードの RAM オーバレイ(常駐側)。設計と制約は overlay.h を参照。 */
#include "overlay.h"
#include "ramexec.h"
#include "curtain.h"

u8 g_ovl_ok;

/* ★0x4000-0x5FFF に居ること(overlay.h の制約)。Makefile がリンク後に検証する。 */
void overlay_load(u8 bank) {
    g_ovl_ok = 0;
    if (!g_ramx2_ok) return;   /* page2 を RAM 化できない機械ではオーバレイも使えない(安全側) */

    /* ★複製中は割込みを止めない。8KB を di で囲んだら **223ms** 割込みが止まり、BGM ごと
       画面が固まった(openMSX で実測。実機でも「BGMから何から一瞬固まる」として報告された)。
       止めなくてよい根拠: この間 0x6000-0x7FFF は演出バンク・page2 は RAM になるが、
         ・割込み文脈で走るコードは全て 0x6000 未満(snd_isr/sfx_update/bgm_update/ras_isr/
           ras_apply/ras_rearm)。Makefile がリンク後に検証する。
         ・それらは 0x6000-0xBFFF を読まない(BGM データは bgm_ram=page3 へ複写済み、
           ラスタ ISR は VDP ポートと page3 のみ)。
       窓を差し替える一瞬だけ di する。 */
    __asm di __endasm;
    *(volatile u8 *)0x6800 = bank;     /* 0x6000-0x7FFF 窓 ← 演出バンク(複製元) */
    /* page2 → RAMスロット+seg5。ENASLT は AF/BC/DE/HL を壊すので前後で退避(苦労と教訓 §5-1)。 */
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
        ei
    __endasm;
    /* ★LDIR で複製(C のバイトループは同じ 8KB に 223ms 掛かっていた)。LDIR は割込みで中断・再開
       できる命令で、ISR はレジスタを全退避するので割込み許可のままで安全。 */
    __asm
        push af
        push bc
        push de
        push hl
        ld   hl, #0x6000
        ld   de, #0xA000
        ld   bc, #0x2000
        ldir
        pop  hl
        pop  de
        pop  bc
        pop  af
    __endasm;
    __asm
        di
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
void pal_update(void) __naked      { __asm jp 0xA00C __endasm; }
void pal_reset(void) __naked       { __asm jp 0xA00F __endasm; }
void curtain_volley(u8 active) __naked { (void)active; __asm jp 0xA012 __endasm; }
void curtain_present(u8 nper, u8 line) __naked { (void)nper; (void)line; __asm jp 0xA015 __endasm; }
void crush_bolts(u8 seed) __naked { (void)seed; __asm jp 0xA018 __endasm; }
void clear_enemy_bullets(void) __naked { __asm jp 0xA01B __endasm; }
void crush_wave_init(void) __naked { __asm jp 0xA01E __endasm; }
void crush_wave(u8 step) __naked { (void)step; __asm jp 0xA021 __endasm; }
void crush_wave_off(void) __naked { __asm jp 0xA024 __endasm; }
s16 crush_wave_y(u8 step) __naked { (void)step; __asm jp 0xA027 __endasm; }
