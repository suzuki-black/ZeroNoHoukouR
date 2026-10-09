/* scene_ranking.c — ★ランキング(TOP5)の表示。冷たいシーン(RANK_BANK = bank67)。
   アトラクトモードの一部: タイトル(放置 20 秒)→ デモ → **ここ(8 秒)** → タイトル。
   設計は docs/デモとランキング設計.md。表の置き場と形は rank.h。
   ★エンディングと同じく update の中で描いて待ち、終わったら次のシーンを返す(ブロッキング)。
   ★音は鳴らさない(scene.c の scene_bgm で BGM_OFF)。デモも無音なので、タイトルへ戻ると曲が頭から鳴る。 */
#include "input.h"
#include "scene.h"
#include "vdp.h"
#include "rank.h"

#define RK_BG   1      /* 背景 = 濃紺(エンディングと同じ) */
#define RK_TX   15     /* 文字 = 白 */
#define RK_TOP  12     /* 1 位 = 橙 */
#define RK_HOLD 480    /* 8 秒(60 フレーム/秒) */

static const char *const ord[RANK_N] = { "1ST", "2ND", "3RD", "4TH", "5TH" };

/* 1 行 = 「1ST   10000   SZK」(17 文字 = 136 ドット)。点数は右寄せ、頭の 0 は空白にする。 */
static void rank_line(u8 i) {
    char b[18];
    u16 v = g_rank[i].score;
    u8 k;
    for (k = 0; k < 17; k++) b[k] = ' ';
    b[17] = 0;
    b[0] = ord[i][0]; b[1] = ord[i][1]; b[2] = ord[i][2];
    k = 10;                                   /* 点数の最後の桁(6〜10 桁目) */
    do { b[k--] = (char)('0' + (u8)(v % 10)); v /= 10; } while (v && k >= 6);
    b[14] = g_rank[i].name[0]; b[15] = g_rank[i].name[1]; b[16] = g_rank[i].name[2];
    vdp_text(60, (u8)(72 + i * 20), (i == 0) ? RK_TOP : RK_TX, RK_BG, b);
}

static u8 run_ranking(void) {
    u16 f;
    u8 i;
    __asm ei __endasm;            /* ★_bcall は di のまま来る。vdp_wait_frame は割込みで進む JIFFY を待つ */
    DEMO_END();                   /* ★デモから来たら、書き換えた設定を元に戻す */
    /* ★面(デモ)から来ると画面モードは同じ SCREEN5 なので CHGMOD が走らず、面の VDP の状態(スプライトの表・
       拡大・MSK・パレットの天候など)が残る。ここで SCREEN5 を張り直して素の状態にする */
    vdp_screen5();
    vdp_palette_game();
    vdp_sprite_hide_from(0);
    vdp_set_vscroll(0);
    vdp_set_hscroll(0, 0);
    vdp_set_display_page(0);
    vdp_fill(0, 0, 256, 212, RK_BG);
    vdp_text_s(80, 28, RK_TOP, RK_BG, 2, "BEST 5");   /* 6 字 × 16 = 96 ドット、中央 */
    for (i = 0; i < RANK_N; i++) rank_line(i);
    for (f = 0; f < RK_HOLD; f++) {
        vdp_wait_frame();
        input_poll();
        if (g_input_edge & INP_TRIG) break;
    }
    return SC_TITLE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) { vdp_set_display_page(0); }
    else g_scene_ret = run_ranking();
}
