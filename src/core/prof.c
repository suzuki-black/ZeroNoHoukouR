/* prof.c — DEBUG_PROF 計測基盤(詳細は prof.h)。全体を #ifdef DEBUG_PROF で囲むので、
   通常ビルドには何も残らない。
   ★冷たい部分(自己診断画面・区間別µsの凍結表示)は banked/prof_bank.c(bank31)へ移した。
     演出を常時オンに畳んだ結果 DEBUG_PROF ビルドだけが常駐24KBを超えたため
     (性能と高速化 §3-C の常駐リクレイム)。ここに残すのは
       ・タイマ読み(区間計測の毎フレーム呼び)
       ・蓄積カウンタ
       ・キー待ち(_bcall は di で囲むのでバンク内で待ってはいけない)
     だけ。 */
#ifdef DEBUG_PROF
#include "prof.h"
#include "vdp.h"
#include "input.h"
#include "bank.h"


/* ★この版の常駐は 24KB ぎりぎりなので、毎フレーム呼ぶ小物(tick 読み・u32 の足し込み)は asm で書く。
     C だと prof_add だけで 85B あった。tick は _pcm_now(上位を挟んで読む。A/H/L だけ壊す)で読む。 */
u32 __at(PROF_RAM_ADDR) g_prof_acc[PF_N];
u16 __at(PROF_OVER_ADDR) g_prof_over;
u8  __at(PROF_MODE_ADDR) g_prof_mode;   /* bank31 への用件: 0=自己診断 / 1=区間表示 */
u8  __at(PROF_FC_ADDR)   g_prof_fc;
static u16 __at(PROF_T0_ADDR) prof_t0;  /* prof_begin の開始 tick */
static u16 __at(PROF_PC_ADDR) prof_pc;  /* 計算区間の開始 tick */

/* g_prof_acc[i] += d(A=i, DE=d)。g_prof_acc は 0xEB00 で i*4 < 256 なので上位は固定 */
static void prof_acc(u8 i, u16 d) __naked {
    (void)i; (void)d;
    __asm
        add  a, a
        add  a, a
        ld   l, a
        ld   h, #>(_g_prof_acc)
        ld   a, (hl)
        add  a, e
        ld   (hl), a
        inc  hl
        ld   a, (hl)
        adc  a, d
        ld   (hl), a
        inc  hl
        ld   a, (hl)
        adc  a, #0
        ld   (hl), a
        inc  hl
        ld   a, (hl)
        adc  a, #0
        ld   (hl), a
        ret
    __endasm;
}

/* g_prof_acc[i] += 今 - t0(A=i, DE=t0) */
void prof_add(u8 i, u16 t0) __naked {
    (void)i; (void)t0;
    __asm
        ld   c, a
        push de
        call _pcm_now           ; HL = 今(A/H/L だけ壊す)
        pop  de
        or   a, a
        sbc  hl, de
        ex   de, hl
        ld   a, c
        jp   _prof_acc
    __endasm;
}

void prof_begin(void) __naked {
    __asm
        call _pcm_now
        ld   (_prof_t0), hl
        ret
    __endasm;
}

void prof_end(u8 i) __naked {
    (void)i;
    __asm
        ld   de, (_prof_t0)
        jp   _prof_add
    __endasm;
}

/* 自己診断/区間表示を bank31 に描かせ、キー(mask)の押下→離しで抜ける。
   ★キー待ちはバンクの外で(_bcall は di で囲むのでバンク内で待ってはいけない)。 */
static void prof_show(u8 mode, u8 mask) {
    g_prof_mode = mode;
    bcall_to(PROF_BANK);
    for (;;) { input_poll(); if (g_input & mask) break; }
    for (;;) { input_poll(); if (!(g_input & mask)) break; }
}

/* 起動時1回: 実機µs自己診断を描かせ、トリガ押下→離しで抜ける。 */
void prof_selftest(void) {
    prof_show(0, INP_TRIG);
}

void prof_compute_begin(void) __naked {
    __asm
        call _pcm_now
        ld   (_prof_pc), hl
        ret
    __endasm;
}

/* 計算区間の tick(cmd_wait 含む)を PF_COMPUTE へ。1VBLANK(4262tick=16.7ms)を超えたら g_prof_over++ */
void prof_compute_end(void) __naked {
    __asm
        call _pcm_now
        ld   de, (_prof_pc)
        or   a, a
        sbc  hl, de
        ex   de, hl             ; DE = 計算区間の tick
        ld   hl, #4262
        or   a, a
        sbc  hl, de
        jr   nc, 00001$
        ld   hl, (_g_prof_over)
        inc  hl
        ld   (_g_prof_over), hl
    00001$:
        xor  a, a               ; PF_COMPUTE
        jp   _prof_acc
    __endasm;
}

/* 60フレーム(=約1秒)ごとに画面を凍結して区間別µsを表示、Mキーで再開。
   ★表示はpage0へ切替→Mで復帰時にpage1へ戻す(ゲームは次フレームのスクロールでpage確定)。 */
void prof_frame_end(void) {
    if (++g_prof_fc < 60) return;
    g_prof_fc = 0;
    prof_show(1, INP_TRIGB);   /* 値の整形と描画(蓄積のクリアも向こう側)。Mキーで進む(発砲トリガと別) */
    vdp_set_display_page(1);
}
#endif /* DEBUG_PROF */
