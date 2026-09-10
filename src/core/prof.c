/* prof.c — DEBUG_PROF 計測基盤(詳細は prof.h)。全体を #ifdef DEBUG_PROF で囲むので、
   通常ビルドには何も残らない。 */
#ifdef DEBUG_PROF
#include "prof.h"
#include "vdp.h"
#include "input.h"

/* ===== S1990 システムタイマ(turboR) ===== */
__sfr __at(0xE6) STMR_LO;   /* 読=カウンタ下位 / 書=リセット */
__sfr __at(0xE7) STMR_HI;   /* 読=カウンタ上位 */

/* ===== スロット/マッパー偵察(§4-3の全コードRAM化に必要) ===== */
__sfr __at(0xA8) PSLOT;     /* 基本スロットレジスタ: bit0-1=page0/2-3=p1/4-5=p2/6-7=p3 */
__sfr __at(0xFC) MAPSEG0;   /* メモリマッパー: page0の16KBセグメント番号 */
__sfr __at(0xFD) MAPSEG1;   /* page1 */
__sfr __at(0xFE) MAPSEG2;   /* page2 */
__sfr __at(0xFF) MAPSEG3;   /* page3 */

static void tmr_reset(void) { STMR_LO = 0; }
/* 上位を挟んで読み、桁上がりレースを排除(hi,lo,hi で hi 不変なら整合)。 */
static u16 tmr_read(void) {
    u8 h1, lo, h2;
    do { h1 = STMR_HI; lo = STMR_LO; h2 = STMR_HI; } while (h1 != h2);
    return (u16)(((u16)h2 << 8) | lo);
}

/* ===== (1) CPUモード比: 同一ループを ROM / RAM で走らせる ===== */
/* フェッチ律速にするため本体は多数の1バイト命令(nop)。位置独立(jr/djnz=相対)なので RAM へ丸コピー可。
   40×256=10240 内反復 × (16 nop + djnz)。R800: ROMフェッチ≒4×RAM。Z80: 差なし。 */
static void busyloop(void) __naked {
    __asm
        ld   d, #40
    00001$:
        ld   b, #0
    00002$:
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        djnz 00002$
        dec  d
        jr   nz, 00001$
        ret
    __endasm;
}
static u8 rambuf[48];   /* busyloop を RAM(page3)へコピーして実行する枠 */
static void copy_busyloop(void) {
    u8 i; const u8 *src = (const u8 *)busyloop;
    for (i = 0; i < 48; i++) rambuf[i] = src[i];
}

/* ===== (2) VDP I/O 単価: 0x99 へ 4096回 OUT。ループ overhead を null版で差し引く ===== */
static u16 time_vdpio(void) {
    tmr_reset();
    __asm
        ld   c, #16
    00011$:
        ld   b, #0
    00012$:
        xor  a
        out  (0x99), a       ; VDP コントロールへ書込(偶数回=フリップフロップは元に戻る)
        djnz 00012$
        dec  c
        jr   nz, 00011$
    __endasm;
    return tmr_read();
}
static u16 time_nullloop(void) {   /* 同反復・OUT無し(overhead分) */
    tmr_reset();
    __asm
        ld   c, #16
    00021$:
        ld   b, #0
    00022$:
        xor  a
        djnz 00022$
        dec  c
        jr   nz, 00021$
    __endasm;
    return tmr_read();
}

/* ===== (3) HMMM 1KB(256x8, SCREEN5=1024B相当)発行→完了までのtick ===== */
static u16 time_hmmm(void) {
    vdp_cmd_wait();
    tmr_reset();
    vdp_copy(0, 150, 0, 190, 256, 8);   /* 表示域下部で 256x8 を複製(診断画面の文字より下=無害) */
    vdp_cmd_wait();
    return tmr_read();
}

/* ===== 表示ヘルパ(u16→10進, ラベル前置) ===== */
static char pbuf[24];
static char hxd(u8 n) { return (char)(n < 10 ? '0' + n : 'A' + (n - 10)); }
/* ラベル＋4個の8bit値を16進で表示(スロット/マッパー偵察用)。 */
static void put_hex4(u8 px, u8 py, const char *label, u8 a, u8 b, u8 c, u8 d) {
    u8 i = 0; const char *l = label; u8 vs[4]; u8 k;
    vs[0] = a; vs[1] = b; vs[2] = c; vs[3] = d;
    while (*l) pbuf[i++] = *l++;
    for (k = 0; k < 4; k++) { pbuf[i++] = hxd((u8)(vs[k] >> 4)); pbuf[i++] = hxd((u8)(vs[k] & 15)); pbuf[i++] = ' '; }
    pbuf[i] = 0;
    vdp_text(px, py, 15, 0, pbuf);
}
static void put_num(u8 px, u8 py, const char *label, u16 v) {
    u8 i = 0; const char *l = label; char tmp[6]; u8 n = 0;
    while (*l) pbuf[i++] = *l++;
    if (v == 0) tmp[n++] = '0';
    else while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) pbuf[i++] = tmp[--n];
    pbuf[i] = 0;
    vdp_text(px, py, 15, 0, pbuf);
}

void prof_selftest(void) {
    u16 rom_t, ram_t, io_t, null_t, hmmm_t, ratio;

    /* --- 計測(画面設定前に。VDP I/O計測は画面確立後の方が安全なので後段) --- */
    tmr_reset(); busyloop();                    rom_t = tmr_read();   /* ROM実行 */
    copy_busyloop();
    tmr_reset(); ((void (*)(void))rambuf)();     ram_t = tmr_read();   /* RAM実行 */

    /* --- 診断画面(SCREEN5) --- */
    vdp_screen5();
    vdp_palette_game();
    vdp_set_display_page(0);
    vdp_fill(0, 0, 256, 212, 0);                /* 黒でクリア */

    io_t   = time_vdpio();
    null_t = time_nullloop();
    hmmm_t = time_hmmm();
    ratio  = ram_t ? (u16)((u32)rom_t * 10 / ram_t) : 0;   /* ×10(=40なら4.0倍=R800) */

    vdp_text(16, 8, 11, 0, "TURBOR SELF CHECK");
    put_num(16, 24, "ROM TICK ", rom_t);
    put_num(16, 32, "RAM TICK ", ram_t);
    put_num(16, 40, "RATIO X10 ", ratio);       /* 40=R800(x4.0) / 10=Z80(x1.0) */
    put_num(16, 56, "VDPIO 4096 T ", (u16)(io_t - null_t));   /* 4096回OUTの正味tick */
    put_num(16, 64, "HMMM 1KB T ", hmmm_t);     /* 256x8コピーのtick(1tick=3.9us) */
    /* ★スロット/マッパー偵察(§4-3=page1をRAMへ切替えるのに必要)。
       SLOTREG(0xA8): bit2-3=page1スロット / bit6-7=page3(=RAM)スロット。両者が違えば page1 を RAM スロットへ差替える。
       MAP: 現在の page0-3 の16KBマッパーセグメント番号。空きセグメントに page1 コードを置く。 */
    { u8 ps = PSLOT;   /* A8 / page1スロット / page3(RAM)スロット / 0 */
      put_hex4(16, 78, "A8/P1/P3 ", ps, (u8)((ps >> 2) & 3), (u8)((ps >> 6) & 3), 0); }
    put_hex4(16, 86, "MAP0-3=  ", MAPSEG0, MAPSEG1, MAPSEG2, MAPSEG3);
    vdp_text(16, 98, 8, 0, "PRESS TRIGGER");

    /* トリガ押下→離しで抜ける */
    for (;;) { input_poll(); if (g_input & INP_TRIG) break; }
    for (;;) { input_poll(); if (!(g_input & INP_TRIG)) break; }
}

/* ===== 実行時 区間計測 ===== */
u32 g_prof_acc[PF_N];
u16 g_prof_over;
u16 prof_tick(void) { return tmr_read(); }

/* tick平均→µs/フレーム: acc/60フレーム × 3.911µs/tick。 */
static u16 acc_us(u32 acc) { return (u16)(acc * 3911u / 1000u / 60u); }

/* 各フレーム末に呼ぶ。60フレーム(=約1秒)蓄積したら画面を凍結して区間別µsを表示、Mキーで再開。
   ★表示はpage0へ切替→Mで復帰時にpage1へ戻す(ゲームは次フレームのスクロールでpage確定)。 */
void prof_frame_end(u16 compute_ticks) {
    static u8 fc;
    u16 us_comp, us_cmd, us_draw, us_upd, us_aa, us_col, us_sea, us_fire, us_scr, us_idle, over;
    if (compute_ticks > 4262) g_prof_over++;   /* 1VBLANK(16.7ms)超の計算フレーム数 */
    if (++fc < 60) return;
    fc = 0;
    /* ★表示(vdp_fill/vdp_text)自身が cmd_wait を呼び PF_CMDWAIT を汚すので、先に全値を確定してから描く。 */
    us_comp = acc_us(g_prof_acc[PF_COMPUTE]);
    us_cmd  = acc_us(g_prof_acc[PF_CMDWAIT]);
    us_draw = acc_us(g_prof_acc[PF_DRAW]);
    us_upd  = acc_us(g_prof_acc[PF_UPDATE]);
    us_aa   = acc_us(g_prof_acc[PF_AA]);
    us_col  = acc_us(g_prof_acc[PF_COL]);
    us_sea  = acc_us(g_prof_acc[PF_SEASCROLL]);
    us_fire = acc_us(g_prof_acc[PF_FIRE]);
    us_scr  = acc_us(g_prof_acc[PF_SCROLL]);
    us_idle = acc_us(g_prof_acc[PF_WAIT]);
    over    = g_prof_over;
    { u8 i; for (i = 0; i < PF_N; i++) g_prof_acc[i] = 0; g_prof_over = 0; }
    vdp_set_vscroll(0);          /* R#23=0: 縦スクロール解除(でないとpage0の文字がcam分ずれる) */
    vdp_set_hscroll(0, 0);       /* R#26/27=0: 横スクロール(蛇行weaveX)解除(でないと文字が横にずれる) */
    vdp_set_display_page(0);
    /* ★重要: page0 の y0..31 には「炎ソースタイル(bake_fireballs)」が保管されている(gameplay中は
       page1表示なので隠れているが burn_bake がここから毎回コピーする)。凍結表示で y0 から塗ると
       この炎タイルをフォント文字で上書きし、以後 burn_bake が文字化けした炎をコピーする不具合になる。
       ゆえに凍結表示は y32 以降だけを使い、炎タイル(y0..31)は絶対に触らない(表示には映るが無害)。 */
    vdp_fill(0, 32, 256, 120, 0);
    vdp_text(16, 40, 11, 0, "PROF US/FRAME (M=NEXT)");
    put_num(16, 52, "COMPUTE ", us_comp);
    put_num(16, 60, "UPDATE  ", us_upd);     /* behaviors(移動/AI/発砲) */
    put_num(16, 68, "AA      ", us_aa);      /* 対空砲 */
    put_num(16, 76, "COLLIDE ", us_col);     /* 当たり判定(recount+resolve) */
    put_num(16, 84, "DRAW    ", us_draw);
    put_num(16, 92, "CMDWAIT ", us_cmd);     /* VDPコマンド待ち(cmd_wait滞在, 全発行元の合計) */
    put_num(16, 100, "SEASCRL ", us_sea);    /* 海(cmd_wait含む) */
    put_num(16, 108, "SCROLL  ", us_scr);    /* スクロールdraw_row(cmd_wait含む) */
    put_num(16, 116, "FIRE    ", us_fire);   /* 炎上fire_draw(cmd_wait含む) */
    put_num(16, 124, "IDLEWAIT ", us_idle);  /* VBLANK空き(余裕) */
    put_num(16, 132, "OVER1VB/60 ", over);   /* 60中1VBLANK超だった数 */
    for (;;) { input_poll(); if (g_input & INP_TRIGB) break; }     /* Mキーで進む(発砲トリガと別) */
    for (;;) { input_poll(); if (!(g_input & INP_TRIGB)) break; }
    vdp_set_display_page(1);
}
#endif /* DEBUG_PROF */
