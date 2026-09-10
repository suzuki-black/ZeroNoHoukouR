/* input.c — 入力の常駐実装。キーボード row8(カーソル/スペース/M)＋ジョイスティック port1 を
   直読みし、論理和で押下ビットへ整形。方向はキーボードとジョイ両方で操作可能。
   PSG(0xA0選択/0xA2読み)は音ドライバISRと共有ラッチなので、ジョイ読みは di 保護。 */
#include "input.h"

u8 g_input;
u8 g_input_edge;

static u8 g_raw;   /* row8 の生値(負論理) */
static u8 g_joy;   /* ジョイ port1 = PSG R#14(負論理: bit0上/1下/2左/3右/4トリガA/5トリガB) */
static u8 g_raw4;  /* row4 の生値(負論理。K L M N O P Q R。M=bit2 を追加ボタンに使う) */
static u8 g_raw2;  /* row2 の生値(負論理)。A=bit6→ボタン1(TRIG), B=bit7→ボタン2(TRIGB)。コナミB/A用 */

static void read_row8(void) __naked {
    __asm
        di
        in   a, (0xAA)
        and  #0xF0
        or   #8              ; row 8 を選択
        out  (0xAA), a
        in   a, (0xA9)
        ei
        ld   (_g_raw), a
        ret
    __endasm;
}

/* row4(K L M N O P Q R)を読む。M(bit2)を追加ボタン(トリガB)に使う。 */
static void read_row4(void) __naked {
    __asm
        di
        in   a, (0xAA)
        and  #0xF0
        or   #4              ; row 4 を選択
        out  (0xAA), a
        in   a, (0xA9)
        ei
        ld   (_g_raw4), a
        ret
    __endasm;
}

/* row2 を読む。A=bit6→ボタン1(トリガA), B=bit7→ボタン2(トリガB)。コナミの …BA 用。 */
static void read_row2(void) __naked {
    __asm
        di
        in   a, (0xAA)
        and  #0xF0
        or   #2              ; row 2 を選択
        out  (0xAA), a
        in   a, (0xA9)
        ei
        ld   (_g_raw2), a
        ret
    __endasm;
}

/* ジョイスティック port1 を PSG 経由で読む(R#15 bit6=0 で port1 選択 → R#14 読み)。
   ラッチ(0xA0)を音ISRと共有するので di で原子化。結果(負論理)を g_joy へ。 */
static void read_joy1(void) __naked {
    __asm
        di
        ld   a, #15
        out  (0xA0), a
        in   a, (0xA2)       ; R#15 現在値
        and  #0xBF           ; bit6=0 → ジョイ port1 を選択(他ビットは温存)
        out  (0xA1), a
        ld   a, #14
        out  (0xA0), a
        in   a, (0xA2)       ; R#14 = ジョイ port1 状態(負論理)
        ei
        ld   (_g_joy), a
        ret
    __endasm;
}

void input_poll(void) {
    u8 prev = g_input;
    u8 cur = 0;
    read_row8();
    read_row4();
    read_row2();
    read_joy1();
    /* キーボード row8: bit7=R,6=D,5=U,4=L,0=SPACE(押下で0) */
    if (!(g_raw & 0x80)) cur |= INP_RIGHT;
    if (!(g_raw & 0x40)) cur |= INP_DOWN;
    if (!(g_raw & 0x20)) cur |= INP_UP;
    if (!(g_raw & 0x10)) cur |= INP_LEFT;
    if (!(g_raw & 0x01)) cur |= INP_TRIG;
    /* ジョイ port1 R#14: bit0=U,1=D,2=L,3=R,4=トリガA,5=トリガB(押下で0) */
    if (!(g_joy & 0x01)) cur |= INP_UP;
    if (!(g_joy & 0x02)) cur |= INP_DOWN;
    if (!(g_joy & 0x04)) cur |= INP_LEFT;
    if (!(g_joy & 0x08)) cur |= INP_RIGHT;
    if (!(g_joy & 0x10)) cur |= INP_TRIG;
    if (!(g_joy & 0x20)) cur |= INP_TRIGB;
    /* キーボード row4: bit2 = M キー(押下で0) → トリガB(コナミの B 等) */
    if (!(g_raw4 & 0x04)) cur |= INP_TRIGB;
    /* キーボード row2: A=bit6 → ボタン1(トリガA), B=bit7 → ボタン2(トリガB)。コナミ …BA 用 */
    if (!(g_raw2 & 0x40)) cur |= INP_TRIG;
    if (!(g_raw2 & 0x80)) cur |= INP_TRIGB;
    g_input = cur;
    g_input_edge = (u8)(cur & ~prev);   /* 押した瞬間 */
}
