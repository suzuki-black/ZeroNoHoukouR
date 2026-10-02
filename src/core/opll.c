/* opll.c — MSX-MUSIC(OPLL)の検出と書き込み。仕様と注意点は opll.h を読むこと。 */
#include "opll.h"

__sfr __at(0x7C) OPLL_ADR;     /* アドレスレジスタ */
__sfr __at(0x7D) OPLL_DAT;     /* データレジスタ   */

u8 g_opll;
static u8 opll_slot;           /* 見つけたスロットID(FM-PAC の 0x7FF6 を書くのに使う) */

/* ★ウェイトのループ回数。1 周 = nop,nop,djnz。
   Z80(3.58MHz): 4+4+13 = 21 ステート = 5.87us/周 → 長い待ち(23.52us)は 4 周で足りる。
   R800(7.16MHz): 1 周がおよそ 0.56us → 同じ 23.52us に 42 周要る。
   ★**速い方(R800)に合わせて**決める。遅い CPU では必要以上に待つが、待ち過ぎは安全側。 */
#define W_LONG   48            /* データ書込み後(23.52us 以上) */
#define W_SHORT   8            /* アドレス書込み後(3.36us 以上) */
static u8 wcnt;

static void opll_delay(void) __naked {
    __asm
        ld   a, (_wcnt)
        ld   b, a
    opw_l:
        nop
        nop
        djnz opw_l
        ret
    __endasm;
}

void opll_w(u8 reg, u8 val) {
    if (!g_opll) return;
    OPLL_ADR = reg;  wcnt = W_SHORT; opll_delay();
    OPLL_DAT = val;  wcnt = W_LONG;  opll_delay();
}

/* ---- BIOS でスロットをまたいで読み書きする(自分のページを差し替えないので安全) ---- */
static u8  sl_slot;
static u16 sl_addr;
static u8  sl_val;

static void sl_read(void) __naked {
    __asm
        ld   a, (_sl_slot)
        ld   hl, (_sl_addr)
        push ix
        push iy
        call 0x000C            ; RDSLT (A=slotID, HL=番地 → A=値)
        pop  iy
        pop  ix
        ld   (_sl_val), a
        ret
    __endasm;
}
static void sl_write(void) __naked {
    __asm
        ld   a, (_sl_val)
        ld   e, a
        ld   a, (_sl_slot)
        ld   hl, (_sl_addr)
        push ix
        push iy
        call 0x0014            ; WRSLT (A=slotID, HL=番地, E=値)
        pop  iy
        pop  ix
        ret
    __endasm;
}

static u8 rd(u8 slot, u16 addr) {
    sl_slot = slot; sl_addr = addr; sl_read(); return sl_val;
}

/* ページ1(0x4000〜)のヘッダに識別文字列があるスロットを探す。見つからなければ 0xFF。
   ★sig は 8 バイト("APRLOPLL" / "PAC2OPLL")。番地は 0x4018。 */
static u8 find_sig(const u8 *sig) {
    u8 ps, ss, nss, slot, i;
    for (ps = 0; ps < 4; ps++) {
        nss = (u8)((*(volatile u8 *)(0xFCC1 + ps) & 0x80) ? 4 : 1);   /* EXPTBL: bit7=拡張スロット */
        for (ss = 0; ss < nss; ss++) {
            slot = (u8)((nss > 1) ? (0x80 | (ss << 2) | ps) : ps);
            for (i = 0; i < 8; i++)
                if (rd(slot, (u16)(0x4018 + i)) != sig[i]) break;
            if (i == 8) return slot;
        }
    }
    return 0xFF;
}

void opll_init(void) {
    static const u8 sig_int[8] = { 'A','P','R','L','O','P','L','L' };   /* 内蔵 MSX-MUSIC */
    static const u8 sig_pac[8] = { 'P','A','C','2','O','P','L','L' };   /* 外付け FM-PAC  */
    u8 slot, i;

    g_opll = OPLL_NONE;
    /* ★**内蔵を先に**探す。見つかったら 0x7FF6 には触らない(触ると Panasonic の MSX2+ で壊れる) */
    slot = find_sig(sig_int);
    if (slot != 0xFF) { g_opll = OPLL_INT; }
    else {
        slot = find_sig(sig_pac);
        if (slot == 0xFF) return;                      /* FM は無い。以降 opll_w は何もしない */
        g_opll = OPLL_PAC;
        /* FM-PAC は 0x7FF6 の bit0 を立てて I/O ポートを有効にする(読んで立てて書き戻す) */
        sl_slot = slot; sl_addr = 0x7FF6; sl_read();
        sl_val = (u8)(sl_val | 1); sl_write();
    }
    opll_slot = slot;

    /* 全レジスタを 0 にして黙らせる(0x00〜0x38。音色レジスタ 0x00-0x07 も含めて素に戻す) */
    for (i = 0; i <= 0x38; i++) opll_w(i, 0);
#ifdef OPLLTEST
    /* ★検証用(make clean && make OPLLTEST=1): 起動直後に和音を鳴らしっぱなしにする。
       録音して 440/660/880Hz が出ていれば「検出 → I/O 書込み → 発音」の経路が通っている
       (2026-10-02 に openMSX＋実機BIOS機で確認: 440=875 / 660=910 / 880=923、他は無し)。 */
    opll_w(0x30, 0x50); opll_w(0x10, 0x22); opll_w(0x20, 0x19);   /* 根音 */
    opll_w(0x31, 0x50); opll_w(0x11, 0xB3); opll_w(0x21, 0x19);   /* 5度  */
    opll_w(0x32, 0x50); opll_w(0x12, 0x22); opll_w(0x22, 0x1B);   /* 8度上 */
#endif
}
