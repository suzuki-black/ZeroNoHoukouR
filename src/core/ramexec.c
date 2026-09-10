/* ramexec.c — §4-3: page1(0x4000-0x7FFF)の常駐ホットコードをマッパーRAMへ載せ、ゲームループ中だけ
   page1をRAMスロットへ切替える(準備/バンキング時はカートリッジへ戻す動的切替)。詳細は ramexec.h。 */
#include "ramexec.h"
#include "msx.h"

#define RAM_FREE_SEG 4    /* 空きマッパーセグメント(BIOS=seg0-3。turboR MAP0-3=C3C2C1C0=seg0-3使用→4は空き) */

u8 g_ramx_ok;             /* 1=RAM化利用可(initで確定)。0なら切替は何もしない(ROMのまま=安全) */

/* init で確定する値(切替に使う)。 */
static u8 s_ram_slot;     /* page1へ入れる RAMスロットID (F000SSPP) */
static u8 s_cart_slot;    /* page1へ戻す カートリッジスロットID (F000SSPP) */

/* page1 のスロット切替を行う位置独立asm(page1自身を触るので page3 RAM へ退避して実行)。
   引数(呼出前にRAMの固定番地へ格納): 目標スロットID / マッパーセグメント。ENASLT(0x0024)で拡張スロット対応。 */
static u8 s_blow_slot;    /* blob が読む: 目標スロットID */
static u8 s_blow_seg;     /* blob が読む: page1マッパーセグメント(RAM時のみ意味) */
static u8 blob_buf[32];   /* 切替blobの page3(RAM)退避先 */

/* ---- 切替blob本体(このコードのバイト列を blob_buf へコピーして実行する) ---- */
static void switch_blob(void) __naked {
    __asm
        di
        ld   a, (_s_blow_seg)
        out  (0xFD), a        ; page1 マッパーセグメント(RAMスロット時に有効。cart時は無害)
        ld   a, (_s_blow_slot)
        ld   h, #0x40         ; page1 指定
        call 0x0024           ; ENASLT (A=スロットID, H=page → page1をそのスロットへ)
        ei
        ret
    __endasm;
}

static void call_blob(u8 slotid, u8 seg) {
    s_blow_slot = slotid; s_blow_seg = seg;
    ((void (*)(void))blob_buf)();
}

/* page3(RAM)のスロットID(F000SSPP)を算出(crt0のカート算出をpage3向けに)。 */
static u8 slotid_page3(void) __naked {
    __asm
        call 0x0138           ; RSLREG: A=基本スロットレジスタ(0xA8)
        rlca
        rlca                  ; page3(bit6-7) → bit0-1
        and  #0x03
        ld   c, a
        ld   b, #0
        ld   hl, #0xFCC1      ; EXPTBL
        add  hl, bc
        ld   c, a             ; C=基本スロット
        ld   a, (hl)          ; 拡張?
        and  #0x80
        or   c
        ld   c, a             ; F000_00PP
        ld   de, #4
        add  hl, de           ; HL→SLTTBL[slot]
        ld   a, (hl)
        and  #0xC0            ; page3(bit6-7)の2次スロット
        rlca
        rlca                  ; → bit2-3(SS位置)へ
        rlca
        rlca
        or   c                ; F000SSPP
        ld   l, a
        ld   h, #0
        ret                   ; 返り値 L(u8)
    __endasm;
}
/* page1(cart)のスロットID(F000SSPP)を算出(crt0と同一=page1/2)。 */
static u8 slotid_page1(void) __naked {
    __asm
        call 0x0138
        rrca
        rrca                  ; page1(bit2-3) → bit0-1
        and  #0x03
        ld   c, a
        ld   b, #0
        ld   hl, #0xFCC1
        add  hl, bc
        ld   c, a
        ld   a, (hl)
        and  #0x80
        or   c
        ld   c, a
        ld   de, #4
        add  hl, de
        ld   a, (hl)
        and  #0x0C            ; page1/2(bit2-3)の2次スロット
        or   c
        ld   l, a
        ld   h, #0
        ret
    __endasm;
}

/* 起動時1回。page1(0x4000-0x7FFF)ROMを空きセグメントへコピーし、切替の準備をする。
   ★この関数自身は page1(cart)から実行してよい(page2窓だけ一時操作、page1は触らない)。 */
u8 ramexec_page1_to_ram(void) {
    u8 i, m0, m4, ok;
    const volatile u8 *src;
    volatile u8 *dst;

    g_ramx_ok = 0;
    s_ram_slot  = slotid_page3();
    s_cart_slot = slotid_page1();

    /* 切替blobを page3(RAM)へコピー(位置独立)。 */
    { const u8 *b = (const u8 *)switch_blob; for (i = 0; i < 32; i++) blob_buf[i] = b[i]; }

    /* --- 空きセグメント RAM_FREE_SEG が実RAMかプローブ(page2窓を一時使用) ---
       page2 を RAMスロット＋seg4 にして 0x8000 へ書込→読戻し、かつ seg0(=page3現行)とエイリアスしないか確認。 */
    __asm di __endasm;
    { u8 h = 0x80; (void)h; }
    /* page2 → RAMスロット, seg=RAM_FREE_SEG */
    s_blow_seg = 0; s_blow_slot = 0;   /* blobはpage1用なのでここでは使わない。page2はENASLTを直接 */
    __asm
        ld   a, (_s_ram_slot)
        ld   h, #0x80         ; page2
        call 0x0024           ; ENASLT page2 → RAMスロット
        ld   a, #4            ; RAM_FREE_SEG
        out  (0xFE), a        ; page2 マッパーセグメント=4
    __endasm;
    dst = (volatile u8 *)0x8000;
    dst[0] = 0xA5; m4 = dst[0];        /* seg4 に書けるか */
    /* seg0(page3=0xC000)へ別マーカを置きエイリアス判定。0xC000は自分のDATAなので退避/復元 */
    { volatile u8 *p3 = (volatile u8 *)0xC000; u8 save = p3[0]; p3[0] = 0x5A; m0 = dst[0]; p3[0] = save; }
    ok = (m4 == 0xA5) && (m0 == 0xA5);  /* 書けて、かつ seg0書換でseg4が変わらない=独立RAM */

    if (ok) {
        /* --- page1 ROM(0x4000-0x7FFF)を seg4(page2窓)へコピー --- */
        src = (const volatile u8 *)0x4000;
        dst = (volatile u8 *)0x8000;
        { u16 k; for (k = 0; k < 0x4000; k++) dst[k] = src[k]; }
        g_ramx_ok = 1;
    }
    /* page2 をカートリッジへ復元し、ASCII8バンク窓(bank2/3)を再確定(選択レジスタはpage1=cartなので書ける) */
    __asm
        ld   a, (_s_cart_slot)
        ld   h, #0x80
        call 0x0024           ; ENASLT page2 → cart
    __endasm;
    *(volatile u8 *)0x7000 = 2;   /* 0x8000-0x9FFF = bank2 */
    *(volatile u8 *)0x7800 = 3;   /* 0xA000-0xBFFF = bank3(スワップ窓既定) */
    __asm ei __endasm;
    return g_ramx_ok;
}

/* ゲームループのホット区間だけ page1 を RAM へ。以後 page1(0x4000-0x7FFF)は同一内容RAM=同一番地で高速。 */
void page1_use_ram(void) {
    if (g_ramx_ok) call_blob(s_ram_slot, RAM_FREE_SEG);
}
/* バンキング/準備/被弾の前に page1 をカートリッジへ戻す(0x6000-0x7800の選択レジスタを生かす)。 */
void page1_use_cart(void) {
    if (g_ramx_ok) call_blob(s_cart_slot, 0);
}
