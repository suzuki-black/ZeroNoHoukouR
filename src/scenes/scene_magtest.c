/* scene_magtest.c — ★実機検証用: 走査線の途中で R#1 の MAG(スプライト拡大)を切り替える。
   `make clean && make MAGTEST=1` のときだけ起動シーンとしてリンクされる(通常 ROM には入らない)。

   ★目的: 画面の上 2/3 を拡大スプライト、下 1/3 を通常スプライトにできるか。
     巨大機ボス(ROADMAP P2-8)を「全体スプライト」で作れるかの前提を実機で確かめる。
   ★方式(資料で確認した上で選んだもの):
     - R#1 のモードビットは「行の終わりまで遅延して効く＝継ぎ目が出ない」(Grauw, Screensplit guide)。
     - 切替のタイミングはラスタ割込み(R#19＋S#1 の FH)。実機で実績のある raster.c をそのまま使う。
       フレーム先頭(VBLANK)で MAG=1、分割行で MAG=0。
     - ★除外: HR(S#2 bit5)を待って HBLANK を狙う方法。CPU 速度に依存する(同ガイドが非推奨)。
   ★見るところ:
     1. 上の帯: 32x32 に拡大された 8 枚が画面幅いっぱいに並ぶか(1走査線8枚の上限ちょうど)。
     2. 下の帯: 16x16 のままの 8 枚。
     3. 境目をまたいで上下に動く 2 枚: 境目で何が起きるか(上半分だけ拡大/1行ずれ/ちらつき/化け)。
     4. 行ごとの色(拡大側は 2 行ずつになるはず)。
   操作: SPACE=モード切替(分割 / 全部通常 / 全部拡大)  上下=分割行を 4 行ずつ移動 */
#include "vdp.h"
#include "input.h"
#include "scene.h"
#include "raster.h"

#define RG1SAV   ((volatile u8 *)0xF3E0)
#define SPLIT_MIN 40
#define SPLIT_MAX 196

static u8 mode;      /* 0=分割(上=MAG) / 1=全部通常 / 2=全部MAG */
static u8 split;     /* 分割行(画面行) */
static u8 r1n;       /* MAG を落とした R#1 */
static u8 t;         /* フレームカウンタ */

/* 枠＋対角線＋中央の市松。拡大されると 1 ドットが 2x2 になるのが一目で分かる形。 */
static const u8 pat_frame[32] = {
    0xFF,0xFF, 0xC0,0x03, 0xA0,0x05, 0x90,0x09, 0x88,0x11, 0x84,0x21, 0x82,0x41, 0x81,0x81,
    0x81,0x81, 0x82,0x41, 0x84,0x21, 0x88,0x11, 0x90,0x09, 0xA0,0x05, 0xC0,0x03, 0xFF,0xFF,
};
/* 横縞(1行おき)。行ごとの色と、拡大で縞が 2 行ずつになるかを見る。 */
static const u8 pat_stripe[32] = {
    0xFF,0xFF, 0x00,0x00, 0xFF,0xFF, 0x00,0x00, 0xFF,0xFF, 0x00,0x00, 0xFF,0xFF, 0x00,0x00,
    0xFF,0xFF, 0x00,0x00, 0xFF,0xFF, 0x00,0x00, 0xFF,0xFF, 0x00,0x00, 0xFF,0xFF, 0x00,0x00,
};
static const u8 grad[16] = { 15,14,4,5, 13,5,4,14, 15,12,11,6, 9,3,8,10 };
static const u8 grad2[16] = { 11,11,12,12, 6,6,9,9, 3,3,8,8, 10,10,15,15 };

static const char *const mode_name[3] = { "SPLIT     ", "ALL NORMAL", "ALL MAG   " };

static void num3(u8 x, u8 y, u8 v) {
    char s[4];
    s[0] = (char)('0' + v / 100); s[1] = (char)('0' + (v / 10) % 10); s[2] = (char)('0' + v % 10); s[3] = 0;
    vdp_text(x, y, 15, 1, s);
}

/* 背景: 16 行ごとの目盛りと、分割行の赤線。 */
static void draw_bg(void) {
    u8 y;
    vdp_fill(0, 0, 256, 212, 1);
    for (y = 0; y < 212; y = (u8)(y + 16)) vdp_fill(0, y, 256, 1, 7);
    if (mode == 0) vdp_fill(0, (u16)(split - 1), 256, 1, 11);
    vdp_text(2, 2, 15, 1, "R#1 MAG SPLIT TEST");
    vdp_text(2, 200, 14, 1, "SPACE:MODE UP/DN:LINE");
    vdp_text(160, 2, 12, 1, mode_name[mode]);
    if (mode == 0) { vdp_text(160, 12, 15, 1, "LINE"); num3(200, 12, split); }
}

static void arm(void) {
    if (mode == 0) {
        g_ras[0].line = 0;     g_ras[0].reg = 1; g_ras[0].val = (u8)(r1n | 0x01);
        g_ras[0].reg2 = RAS_NOREG; g_ras[0].pidx = RAS_NOPAL;
        g_ras[1].line = split; g_ras[1].reg = 1; g_ras[1].val = r1n;
        g_ras[1].reg2 = RAS_NOREG; g_ras[1].pidx = RAS_NOPAL;
        raster_arm(2);
    } else {
        raster_off();
        vdp_wreg(1, (mode == 2) ? (u8)(r1n | 0x01) : r1n);
    }
}

static void magtest_init(void) {
    u8 i;
    vdp_set_vscroll(0);
    vdp_set_display_page(0);
    vdp_sprite_init();                          /* 16x16 化＋全消し */
    r1n = (u8)(*RG1SAV & 0xFE);
    vdp_sprite_pattern(0, pat_frame);
    vdp_sprite_pattern(4, pat_stripe);
    /* 上の帯: 32px 間隔で 8 枚(拡大なら画面幅ちょうど) */
    for (i = 0; i < 8; i++) {
        vdp_sprite_color_tab(i, (i & 1) ? grad2 : grad);
        vdp_sprite_pos(i, (u8)(i * 32), 40, (i & 1) ? 4 : 0);
    }
    /* 下の帯: 同じ 32px 間隔で 8 枚 */
    for (i = 0; i < 8; i++) {
        vdp_sprite_color_tab((u8)(8 + i), (i & 1) ? grad2 : grad);
        vdp_sprite_pos((u8)(8 + i), (u8)(i * 32), 176, (i & 1) ? 4 : 0);
    }
    /* 境目をまたぐ 2 枚 */
    vdp_sprite_color(16, 15);
    vdp_sprite_color_tab(17, grad);
    vdp_sprite_hide_from(18);
    mode = 0; split = 141; t = 0;
    draw_bg();
    arm();
}

static u8 magtest_update(void) {
    u8 y;
    t++;
    if (g_input_edge & INP_TRIG) { mode = (u8)((mode + 1) % 3); draw_bg(); arm(); }
    if (mode == 0) {
        if ((g_input_edge & INP_UP) && split > SPLIT_MIN)   { split = (u8)(split - 4); draw_bg(); arm(); }
        if ((g_input_edge & INP_DOWN) && split < SPLIT_MAX) { split = (u8)(split + 4); draw_bg(); arm(); }
    }
    /* 三角波で上下(y=90..153＝既定の分割行 141 をまたぐ)。2 枚は位相を半分ずらす。
       ★下の帯(y=176)とは行を共有しない範囲にしてある(共有すると 9 枚目として消え、境目の現象と区別できない)。 */
    y = (u8)(t & 0x7F); if (y > 63) y = (u8)(127 - y);
    vdp_sprite_pos(16, 104, (u8)(90 + y), 0);
    y = (u8)((t + 64) & 0x7F); if (y > 63) y = (u8)(127 - y);
    vdp_sprite_pos(17, 152, (u8)(90 + y), 4);
    return SCENE_NONE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) magtest_init();
    else g_scene_ret = magtest_update();
}
