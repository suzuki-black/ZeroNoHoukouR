/* raster.c — ラスタ割り込みによる画面分割の土台。設計と作法は raster.h を参照。 */
#include "raster.h"
#include "vdp.h"
#include "msx.h"

__sfr __at(0x99) RAS_CTRL;   /* VDP コントロール(ステータス読みにも使う) */
__sfr __at(0x9A) RAS_PAL;    /* パレットデータ */

/* 割込み応答遅れの補正。BIOS ハンドラ→H.KEYI→本体までの分だけ分割線は下へずれるので、
   その分だけ手前で割り込ませる。★実測で決める値(openMSX と実機で確認すること)。 */
#define RAS_LINE_BIAS 2

RasSplit g_ras[RAS_MAX];
u8 g_ras_n;

u8 g_ras_i;          /* 今フレームで次に処理する分割の添字。★asm から参照するので非static */
static u8 s_armed;   /* 1=E1 を立てている(asm からは触らないので static でよい) */

/* R#0 の E1(bit4) を操作。BIOS の影(RG0SAV)と整合させる(作法 2)。 */
static void set_e1(u8 on) {
    volatile u8 *sav = (volatile u8 *)RG0SAV;
    u8 v = *sav;
    v = on ? (u8)(v | 0x10) : (u8)(v & ~0x10);
    *sav = v;
    vdp_wreg(0, v);
}

/* ---- 割込みハンドラ本体(H.KEYI から CALL される) ----
   ★割込み文脈なので全レジスタ退避。R#15 は 0 前提で入り、0 に戻して出る(作法 1)。 */
void ras_apply(void) {   /* ★asm から CALL するので非static */
    const RasSplit *s = &g_ras[g_ras_i];
    if (s->reg != RAS_NOREG) {
        RAS_CTRL = s->val;
        RAS_CTRL = (u8)(0x80 | s->reg);
    }
    if (s->pidx != RAS_NOPAL) {
        RAS_CTRL = s->pidx;
        RAS_CTRL = 0x80 | 16;              /* R#16 = パレットポインタ */
        RAS_PAL  = (u8)((s->pr << 4) | s->pb);
        RAS_PAL  = s->pg;
    }
    g_ras_i++;
    if (g_ras_i < g_ras_n) {
        u8 v = (u8)(g_ras[g_ras_i].line + g_vscroll - RAS_LINE_BIAS);
        RAS_CTRL = v;
        RAS_CTRL = 0x80 | 19;
    }
}

/* VBLANK 割込み(FH が立っていない割込み)で呼ぶ: 今フレームの分割を先頭から仕切り直す。
   ★これを本体側(stage_update)でやると駄目だった: stage_update はフレーム途中まで走っているので、
     1本目の分割行(画面上部)を既に通り過ぎており、その分割を毎フレーム取りこぼす。
     実際に「復帰用の分割が効かず画面全体が赤くなる」という形で踏んだ。分割の仕切り直しは
     必ず VBLANK 文脈で行うこと。 */
void ras_rearm(void) {
    g_ras_i = 0;
    if (!g_ras_n) return;
    /* ★line==0 は「フレーム先頭の状態」= VBLANK 中にここで適用する(分割では出せない)。
       割込み応答遅れのぶん、行2 などに置いた復帰用の分割は画面上端に数ライン取りこぼしが出る
       (実際にHUD帯が前フレームの色のまま残った)。フレーム先頭の確定は VBLANK でやること。
       ras_apply が「次の分割の R#19」まで面倒を見るので、ここでは呼ぶだけでよい。 */
    if (g_ras[0].line == 0) { ras_apply(); return; }
    { u8 v = (u8)(g_ras[0].line + g_vscroll - RAS_LINE_BIAS);
      RAS_CTRL = v;
      RAS_CTRL = 0x80 | 19; }
}

void ras_isr(void) __naked {
    __asm
        push af
        push bc
        push de
        push hl
        push ix
        push iy
        ; --- S#1 を選んで読む(読むと FH がクリアされる) ---
        ld   a, #1
        out  (0x99), a
        ld   a, #0x8F            ; R#15 = 1
        out  (0x99), a
        in   a, (0x99)           ; A = S#1
        ld   b, a
        xor  a
        out  (0x99), a
        ld   a, #0x8F            ; R#15 = 0 へ戻す(BIOS が S#0 を読むため。作法1)
        out  (0x99), a
        bit  0, b                ; FH?
        jr   z, 00002$           ; FH でない = VBLANK → 今フレームの分割を仕切り直す
        ld   a, (_g_ras_i)
        ld   hl, #_g_ras_n
        cp   (hl)
        jr   nc, 00001$          ; 表を使い切っている → 何もしない
        call _ras_apply
        jr   00001$
    00002$:
        call _ras_rearm
    00001$:
        pop  iy
        pop  ix
        pop  hl
        pop  de
        pop  bc
        pop  af
        ret
    __endasm;
}

/* H.KEYI(0xFD9A, 5バイトフック)へ JP ras_isr を仕込む。BIOS は毎割込みの最初にここを CALL する。 */
void raster_init(void) {
    g_ras_i = 0; g_ras_n = 0; s_armed = 0;
    __asm
        di
        ld   a, #0xC3            ; JP opcode
        ld   (0xFD9A), a
        ld   hl, #_ras_isr
        ld   (0xFD9B), hl
        ei
    __endasm;
}

/* 今フレームの分割数を確定する。★R#19 の仕込みはここではなく VBLANK 割込み(ras_rearm)が行う。
   本体はいつ呼んでもよい(表と n を更新するだけ)。 */
void raster_arm(u8 n) {
    if (n > RAS_MAX) n = RAS_MAX;
    g_ras_n = n;
    if (n == 0) { raster_off(); return; }
    if (!s_armed) { set_e1(1); s_armed = 1; }
}

void raster_off(void) {
    if (s_armed) { set_e1(0); s_armed = 0; }
    g_ras_n = 0;
    g_ras_i = 0;
}
