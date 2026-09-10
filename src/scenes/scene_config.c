/* scene_config.c — ★設定メニュー(冷たいシーン=bank6)。タイトルで隠しコマンド(コナミ)から開く。
   UP/DOWN でカーソル移動、L/R で値変更、SPACE で START→ゲーム開始(SC_STAGE)。
   6項目(難易度/残機/耐久/ステージ/継続/無敵)を常駐 gamestate へ書き、gameplay が参照する。
   ※描画は「入場時に全描画→以後は変化した行だけ再描画」。全画面再描画は遅く入力を取りこぼすため。 */
#include "vdp.h"
#include "input.h"
#include "scene.h"
#include "gamestate.h"
#include "version.h"   /* BUILD_VER(gitハッシュ)。どのコミットのROMか判別用 */

#define CFG_START 7      /* 項目0..6 ＋ START(7) */
#define ROWS 8

static u8 cur;   /* 0=難易度/1=残機/2=耐久/3=ステージ/4=継続/5=無敵/6=VIEW/7=START。RAM(data-loc)。 */
static u8 g_mode;/* VIEW: 0=GAME(通常)/1=CARD(説明のみ)/2=RESULT(撃破画面のみ)/3=ENDING。START時に g_view へ反映 */

static const char *const diffs[3]  = { "EASY  ", "NORMAL", "HARD  " };
static const char *const livess[3] = { "2", "3", "5" };
static const char *const onoff[2]  = { "OFF", "ON " };
static const char *const num1_9[10]= { "0","1","2","3","4","5","6","7","8","9" };
static const char *const modes[4]  = { "GAME  ", "CARD  ", "RESULT", "ENDING" };   /* 画面ビューア */
static const char *const labels[ROWS] = {
    "DIFFICULTY", "LIVES", "DURABILITY", "STAGE", "CONTINUE", "INVINCIBLE", "VIEW", "START GAME"
};
static const u8 rowy[ROWS] = { 36, 54, 72, 90, 108, 126, 144, 168 };

/* 行 idx の値文字列(START行は値なし=NULL)。 */
static const char *val_of(u8 idx) {
    if (idx == 0) return diffs[g_difficulty];
    if (idx == 1) return livess[g_lives_idx];
    if (idx == 2) return num1_9[g_durability];
    if (idx == 3) return num1_9[g_stage_sel + 1];
    if (idx == 4) return onoff[g_continue ? 1 : 0];
    if (idx == 5) return onoff[g_invinc ? 1 : 0];
    if (idx == 6) return modes[g_mode];
    return (const char *)0;   /* START GAME */
}

/* 1行だけ描画(カーソル/ラベル/値)。値は毎回上書きするので幅固定 or 末尾空白で残像を防ぐ。 */
static void draw_row(u8 idx) {
    u8 y = rowy[idx];
    const char *v = val_of(idx);
    vdp_text(40, y, (cur == idx) ? 11 : 14, 1, (cur == idx) ? ">" : " ");
    vdp_text(56, y, (cur == idx) ? 15 : 14, 1, labels[idx]);
    if (v) vdp_text(184, y, 15, 1, v);
}

static void draw_all(void) {
    u8 i;
    vdp_fill(0, 0, 256, 212, 1);
    vdp_text(88, 12, 15, 1, "- CONFIG -");
    for (i = 0; i < ROWS; i++) draw_row(i);
    vdp_text(24, 194, 14, 1, "UP/DN:SEL  L/R:CHG  SPACE:OK");
    vdp_text(8, 180, 12, 1, "V" GAME_VERSION " " BUILD_VER);   /* ★版(semver)＋ビルドタグ(git短縮ハッシュ) */
}

/* L/R で1項目の値を増減(範囲クランプ)。変化したら1を返す。 */
static u8 change(u8 idx, s8 d) {
    if (idx == 0) { s8 v = (s8)g_difficulty + d; if (v >= 0 && v <= 2) { g_difficulty = (u8)v; return 1; } }
    else if (idx == 1) { s8 v = (s8)g_lives_idx + d; if (v >= 0 && v <= 2) { g_lives_idx = (u8)v; return 1; } }
    else if (idx == 2) { s8 v = (s8)g_durability + d; if (v >= 1 && v <= 9) { g_durability = (u8)v; return 1; } }
    else if (idx == 3) { s8 v = (s8)g_stage_sel + d; if (v >= 0 && v < 5) { g_stage_sel = (u8)v; return 1; } }  /* 開始面(0..4=STAGE_COUNT-1)。面数変更時ここも更新 */
    else if (idx == 4) { u8 n = d > 0 ? 1 : (d < 0 ? 0 : g_continue); if (n != g_continue) { g_continue = n; return 1; } }
    else if (idx == 5) { u8 n = d > 0 ? 1 : (d < 0 ? 0 : g_invinc);   if (n != g_invinc)   { g_invinc = n;   return 1; } }
    else if (idx == 6) { s8 v = (s8)g_mode + d; if (v >= 0 && v <= 3) { g_mode = (u8)v; return 1; } }   /* VIEW: GAME/CARD/RESULT/ENDING */
    return 0;
}

static void config_init(void) {
    vdp_set_display_page(0);
    cur = 0;
    g_mode = 0;   /* ★VIEWを毎回GAMEへ確定。g_modeはbank6のstaticで0xE000共有RAM上=他バンクシーン
                     (ship_render等)が同域を使うため残留値になり得る。放置すると START時 g_view=残留 で
                     「カードのみ表示→タイトルへ戻る」誤動作(ステージ中リセット後に顕在化)を起こす。 */
    draw_all();
}

static u8 config_update(void) {
    u8 e = g_input_edge;
    if ((e & INP_DOWN) && cur < CFG_START) { u8 o = cur; cur++; draw_row(o); draw_row(cur); }
    if ((e & INP_UP)   && cur > 0)         { u8 o = cur; cur--; draw_row(o); draw_row(cur); }
    if (e & INP_RIGHT) { if (change(cur, +1)) draw_row(cur); }
    if (e & INP_LEFT)  { if (change(cur, -1)) draw_row(cur); }
    if ((e & INP_TRIG) && cur == CFG_START) {   /* START: VIEWモードに応じて画面を出す */
        if (g_mode == 3) { g_view = 0; return SC_ENDING; }   /* ENDING を直接表示 */
        g_view = g_mode;                                     /* 0=通常/1=カードのみ/2=結果のみ */
        return SC_STAGE;
    }
    return SCENE_NONE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) config_init();
    else g_scene_ret = config_update();
}
