/* ramexec.c — §4-3: page1(0x4000-0x7FFF)の常駐ホットコードをマッパーRAMへ載せ、ゲームループ中だけ
   page1をRAMスロットへ切替える(準備/バンキング時はカートリッジへ戻す動的切替)。詳細は ramexec.h。 */
#include "ramexec.h"
#include "msx.h"

#define RAM_FREE_SEG  4   /* 空きマッパーセグメント(BIOS=seg0-3。turboR MAP0-3=C3C2C1C0=seg0-3使用→4は空き) */
#define RAM_FREE_SEG2 5   /* page2(常駐bank2)の複製先。seg4 の隣=同様に空き。実在検証は複製後の
                             チェックサム一致＋「cartとは別の実RAM」マーカ検証の両方(偽陽性よけ) */

u8 g_ramx_ok;             /* 1=RAM化利用可(initで確定)。0なら切替は何もしない(ROMのまま=安全) */
u8 g_ramx2_ok;            /* 1=page2もRAM化可(initで確定)。0なら page2 は cart のまま(安全) */

/* init で確定する値(切替に使う)。★overlay.c も使うので非static
   (overlay は複製中に ei を挟みたくないので page2_use_ram/cart を呼ばず自前で ENASLT する)。 */
u8 s_ram_slot;            /* RAMスロットID (F000SSPP) */
u8 s_cart_slot;           /* カートリッジスロットID (F000SSPP) */

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

/* ===== page2(0x8000-0x9FFF = 常駐 bank2)の RAM 実行 =====
   page1 と違い、切替コードは page1 に居る=自分の足元を切らないので page3 退避 blob は不要。
   ★呼び元(stage_update)は page2 に居るが、複製内容は bank2 と同一なので戻り番地はそのまま有効。 */

/* 起動時1回。常駐 bank2 を RAM_FREE_SEG2 へ複製し、チェックサム一致で g_ramx2_ok=1。
   ★この関数は 0x4000-0x5FFF に居ること: 複製元として 0x6000-0x7FFF 窓を一時 bank2 へ差し替えるため、
     0x6000 以降に居ると自分自身が窓ごと消えて暴走する。Makefile がリンク後に番地を検証する。 */
u8 ramexec_page2_to_ram(void) {
#ifdef NO_RAMX2
    g_ramx2_ok = 0;             /* 実機A/B計測用: make NO_RAMX2=1 で page2 の RAM 実行を切る(page1 のみ=旧挙動) */
    return 0;
#else
    u16 k, sum_rom = 0, sum_ram = 0;
    u8 indep = 0;              /* 1=page2 が cart とは別の実RAMになっている(偽陽性よけの直接検証) */
    const volatile u8 *s;
    volatile u8 *d;

    g_ramx2_ok = 0;
    if (!g_ramx_ok) return 0;   /* page1 すら RAM 化できない機械では page2 も諦める(前提が同じ) */

    /* 複製元の期待値(cart の 0x8000-0x9FFF = bank2)。 */
    s = (const volatile u8 *)0x8000;
    for (k = 0; k < 0x2000; k++) sum_rom = (u16)(sum_rom + s[k]);

    __asm di __endasm;
    *(volatile u8 *)0x6800 = 2;        /* 0x6000-0x7FFF 窓 ← bank2(複製元。この関数は 0x4000-0x5FFF に居る) */
    /* ★ENASLT(BIOS 0x0024)は AF/BC/DE/HL を破壊する。SDCC はインラインasmのレジスタ破壊を知らないので、
       前後で退避しないとコンパイラがレジスタに置いた変数(ここでは合計値)が飛ぶ。実際に一度踏んだ。 */
    __asm
        push af
        push bc
        push de
        push hl
        ld   a, #5                     ; RAM_FREE_SEG2
        out  (0xFE), a                 ; page2 マッパーセグメント(RAMスロット時に有効。cart時は無害)
        ld   a, (_s_ram_slot)
        ld   h, #0x80                  ; page2
        call 0x0024                    ; ENASLT page2 → RAMスロット(=0x8000 が seg5 の下位8KB)
        pop  hl
        pop  de
        pop  bc
        pop  af
    __endasm;
    s = (const volatile u8 *)0x6000;   /* bank2(ROM) */
    d = (volatile u8 *)0x8000;         /* seg5(RAM) */
    for (k = 0; k < 0x2000; k++) d[k] = s[k];
    s = (const volatile u8 *)0x8000;
    for (k = 0; k < 0x2000; k++) sum_ram = (u16)(sum_ram + s[k]);   /* 内容が bank2 と一致するか */
    /* ★独立RAM検証: チェックサム一致だけでは「ENASLTが効かず cart のままで、書込が黙って捨てられた」場合も
       一致してしまう(偽陽性)。末尾1バイトにマーカを書き、cart 側が元の値のままであることを確かめて
       「page2 が cart とは別の実RAMになっている」ことを直接示す。検証後は必ず元の値へ戻す。 */
    { volatile u8 *m = (volatile u8 *)0x9FFF;
      u8 org = *m, mark = (u8)~org, rd_ram, rd_cart;
      *m = mark; rd_ram = *m;                      /* RAM側: 書けるか */
      __asm
        push af
        push bc
        push de
        push hl
        ld   a, (_s_cart_slot)
        ld   h, #0x80
        call 0x0024                                ; page2 → cart(ROM側を覗く)
        pop  hl
        pop  de
        pop  bc
        pop  af
      __endasm;
      rd_cart = *m;                                /* cart側: 元の値のままのはず */
      __asm
        push af
        push bc
        push de
        push hl
        ld   a, (_s_ram_slot)
        ld   h, #0x80
        call 0x0024                                ; page2 → RAM へ戻す
        pop  hl
        pop  de
        pop  bc
        pop  af
      __endasm;
      *m = org;                                    /* マーカを消す(複製内容を元通りに) */
      indep = (rd_ram == mark) && (rd_cart == org);
    }
    __asm
        push af
        push bc
        push de
        push hl
        ld   a, (_s_cart_slot)
        ld   h, #0x80
        call 0x0024                    ; ENASLT page2 → cart へ復元
        pop  hl
        pop  de
        pop  bc
        pop  af
    __endasm;
    *(volatile u8 *)0x6800 = 1;        /* 0x6000-0x7FFF 窓 ← bank1(既定へ復元) */
    *(volatile u8 *)0x7000 = 2;        /* 0x8000-0x9FFF = bank2 (念のため再確定) */
    *(volatile u8 *)0x7800 = 3;        /* 0xA000-0xBFFF = bank3(スワップ窓既定) */
    __asm ei __endasm;

    g_ramx2_ok = (indep && sum_ram == sum_rom) ? 1 : 0;   /* 複製一致＋独立RAM の両方でのみ有効化 */
    return g_ramx2_ok;
#endif
}

/* ホット区間だけ page2 を RAM へ。★0xA000 スワップ窓が消えるので data_read/bcall は禁止(page1 と同条件)。 */
void page2_use_ram(void) {
    if (!g_ramx2_ok) return;
    __asm
        di
        push af
        push bc
        push de
        push hl
        ld   a, #5                     ; RAM_FREE_SEG2
        out  (0xFE), a
        ld   a, (_s_ram_slot)
        ld   h, #0x80
        call 0x0024                    ; ★ENASLTは AF/BC/DE/HL を壊す=前後で退避(上記の教訓)
        pop  hl
        pop  de
        pop  bc
        pop  af
        ei
    __endasm;
}
/* バンキング/準備の前に page2 をカートリッジへ戻す(0xA000 スワップ窓が復活する)。 */
void page2_use_cart(void) {
    if (!g_ramx2_ok) return;
    __asm
        di
        push af
        push bc
        push de
        push hl
        ld   a, (_s_cart_slot)
        ld   h, #0x80
        call 0x0024                    ; ★ENASLTは AF/BC/DE/HL を壊す=前後で退避
        pop  hl
        pop  de
        pop  bc
        pop  af
        ei
    __endasm;
}

/* ===== ホット区間の出入口(通常はこの対を使う) =====
   入りは page1→page2、出は page2→page1 の順(切替の瞬間に自分が居る側を後から切る)。 */
void ramx_use_ram(void)  { page1_use_ram();  page2_use_ram();  }
void ramx_use_cart(void) { page2_use_cart(); page1_use_cart(); }
