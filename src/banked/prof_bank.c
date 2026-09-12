/* prof_bank.c — DEBUG_PROF の「冷たい側」(自己診断画面と区間表示)を bank20 へ追い出したもの。
   ★理由: 演出(ラスタ分割/CPU弾幕/パレットエンジン/メガクラッシュ)を常時オンに畳んだ結果、
     DEBUG_PROF ビルドだけが常駐窓(bank0-2=24KB)を数百バイト超過するようになった。
     ここに居るのは **起動時1回** と **60フレームに1回の凍結表示** だけなので、
     ROM実行(3.84倍遅い)でもまったく問題にならない＝常駐リクレイムの理想的な対象
     (性能と高速化 §3-C。gen_planes.c と同じ手口)。
   ★入口は 0xA000 単一(bankhead.s)。resident の g_prof_mode で用件を分ける:
       0 = 起動時の自己診断 / 1 = 区間別µsの凍結表示
   ★キー待ちは常駐側(prof.c)に残す。_bcall はバンクコールの全区間を di で囲むので、
     ここでキー押下を待つと割込みを数秒止めてしまう。描いて即 ret する。
   ★このバンク内から常駐関数(vdp_*)は呼んでよい。データ窓(0xA000)は差し替えない。 */
#include "prof.h"
#include "vdp.h"

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
   40×256=10240 内反復 × (16 nop + djnz)。R800: ROMフェッチ≒4×RAM。Z80: 差なし。
   ★ここがバンク(0xA000窓)に居ても「カートリッジROM実行」であることは常駐と同じなので比は変わらない。 */
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
static u8 __at(PROF_RAM_ADDR + 0x40) rambuf[48];   /* busyloop を RAM(page3)へコピーして実行する枠(高位フリー帯) */
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

/* ===== (4) R800 のハード乗算 MULUB/MULUW の検証(★C アフィン変形ボスのスパイク) =====
   ★sdasz80 は mulub/muluw のニモニックを知らない(実測: "mnemonic error")。**生バイトで出す**。
       MULUB A,r   = ED (C1 + 8*r)   … r は 0=B,1=C,2=D,3=E,4=H,5=L,7=A
       MULUW HL,BC = ED C3
       MULUW HL,SP = ED F3
     (出典: Z80/R800 instruction set, MSX Assembly Page https://map.grauw.nl/resources/z80instr.php)
   ★Z80 では ED の未定義オペコードは無視される(NOP2個相当)ので、答えが合わなければ
     「R800 で動いていない(またはこの機械が turboR でない)」の判定にもなる。
   ★結果の置き場(どのレジスタ対に何が入るか)は資料の記述がぶれていたので、
     **実機/エミュで実測して確定する**のがこのスパイクの目的。
     1234 * 5678 = 7,006,652 = 0x006A_EF7C なので、上位=0x006A / 下位=0xEF7C がどちらに入るかを見る。 */
static u16 __at(PROF_RAM_ADDR + 0xB0) mul_res[6];
/* [0]=MULUW後のHL / [1]=MULUW後のDE / [2]=MULUB後のHL / [3]下位バイト=MULUB後のA
   [4]=MULUW×256 のtick / [5]=ソフト乗算×256 のtick(エミュ実測をスクリプトで読むため) */

static void test_mul(void) {
    __asm
        ld   hl, #1234
        ld   bc, #5678
        .db  0xED, 0xC3          ; MULUW HL,BC
        ld   (_mul_res), hl
        ld   (_mul_res + 2), de
        ld   a, #200
        ld   b, #3
        .db  0xED, 0xC1          ; MULUB A,B
        ld   (_mul_res + 4), hl
        ld   (_mul_res + 6), a
    __endasm;
}

/* ★速度比較。**両方とも「オペランドをメモリから読んで掛ける」形に揃える**こと。
   最初はハード側だけ即値ロードにし、ソフト側は C の `a*b` にしたら、**SDCC が乗算ごと
   最適化で消して**しまい(結果を使っていなかった)、ソフトの方が速いという無意味な数字が出た。
   生成された .asm を見て気づいた。**計測対象が消えていないかは必ず生成コードを見る。** */
static volatile u16 mul_a, mul_b, mul_sink;   /* ★初期値付き static は避ける方針なので代入で入れる */

/* MULUW を 256 回(8展開×32周)。★展開しすぎると jr が届かない(実測: 16展開=128B で
   "Branching Range Exceeded")。 */
static u16 time_mulw_hw(void) {
    mul_a = 1234; mul_b = 5678;
    tmr_reset();
    __asm
        ld   a, #32
    00031$:
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        ld   hl, (_mul_a)
        ld   bc, (_mul_b)
        .db  0xED, 0xC3
        dec  a
        jr   nz, 00031$
    __endasm;
    return tmr_read();
}

/* 比較用: SDCC の u16*u16(__mulint 相当)を同じ 256 回。
   ★アフィン変形で実際に要るのはこの精度なので、これが正しい比較対象。
   ★結果を volatile へ書き出して最適化で消えないようにする。 */
static u16 time_mulw_sw(void) {
    u8  i;
    u16 s = 0;
    mul_a = 1234; mul_b = 5678;
    tmr_reset();
    for (i = 0; i < 32; i++) {
        s = (u16)(s + (u16)(mul_a * mul_b));
        s = (u16)(s + (u16)(mul_a * mul_b));
        s = (u16)(s + (u16)(mul_a * mul_b));
        s = (u16)(s + (u16)(mul_a * mul_b));
        s = (u16)(s + (u16)(mul_a * mul_b));
        s = (u16)(s + (u16)(mul_a * mul_b));
        s = (u16)(s + (u16)(mul_a * mul_b));
        s = (u16)(s + (u16)(mul_a * mul_b));
    }
    mul_sink = s;
    return tmr_read();
}

/* ===== 表示ヘルパ(u16→10進, ラベル前置) ===== */
static char __at(PROF_RAM_ADDR + 0x80) pbuf[24];   /* 表示整形バッファ(高位フリー帯) */
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

/* ===== 用件0: 起動時の自己診断 ===== */
static void selftest(void) {
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
    /* ★R800 ハード乗算の検証(★C アフィン変形ボスのスパイク)。
       1234*5678 = 0x006A_EF7C。MULUW の上位/下位がどのレジスタ対に入るかを実機で確定する。
       200*3 = 0x0258(MULUB)。Z80 なら ED 未定義=無視されるので答えが合わない。 */
    test_mul();
    put_hex4(16, 106, "MULUW HL/DE ", (u8)(mul_res[0] >> 8), (u8)mul_res[0],
                                      (u8)(mul_res[1] >> 8), (u8)mul_res[1]);
    put_hex4(16, 114, "MULUB HL/A  ", (u8)(mul_res[2] >> 8), (u8)mul_res[2],
                                      (u8)mul_res[3], 0);
    mul_res[4] = time_mulw_hw();
    mul_res[5] = time_mulw_sw();
    put_num(16, 122, "MUL256 HW T ", mul_res[4]);
    put_num(16, 130, "MUL256 SW T ", mul_res[5]);
    vdp_text(16, 142, 8, 0, "PRESS TRIGGER");
}

/* ===== 用件1: 区間別µsの凍結表示 ===== */
/* tick平均→µs/フレーム: acc/60フレーム × 3.911µs/tick。 */
static u16 acc_us(u32 acc) { return (u16)(acc * 3911u / 1000u / 60u); }

static void frame_disp(void) {
    u16 us_comp, us_cmd, us_draw, us_upd, us_aa, us_col, us_sea, us_fire, us_scr, us_idle, over;
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
}

void banked_entry(void) {
    if (g_prof_mode) frame_disp(); else selftest();
}
