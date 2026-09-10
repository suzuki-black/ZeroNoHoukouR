/* scene_title.c — ★冷たいシーン(bank5)。YJK自然画タイトルの入力受付だけを担う。
   常駐(bank0-2, 24KB)を食わずに ROM バンクへ置き、bcall で実行する。
   ★描画はしない: タイトル画(零の咆哮 YJK, 54272B)は SCREEN12 で常駐(scene_video_enter)が
     bcall 前に VRAM へ流し込み済み。"PRESS SPACE KEY" 等の文字も画像に焼き込まれている。
     ここで SCREEN5 の描画(vdp_fill/run_ops/vdp_text)を出すと YJK画を壊すので一切行わない。
   規律:
     - リンクは 0xA000(bankhead が先頭 jp _banked_entry を確定)。self-contained。
     - 常駐 global(g_input_edge/g_scene_* 等)は resident_syms(絶対番地)で解決。データ窓は使わない。
   banked_entry が g_scene_phase(0=init/1=update)で分岐し、update 結果を g_scene_ret に書く。 */
#include "input.h"
#include "scene.h"

/* 隠しコマンド(コナミ): 上上下下左右左右 B A。成立で設定メニュー(SC_CONFIG)を開く。
   B=INP_TRIGB(キーB / ジョイ トリガ2 / キーM), A=INP_TRIG(キーA / スペース / ジョイ トリガ1)。 */
static const u8 konami[10] = {
    INP_UP, INP_UP, INP_DOWN, INP_DOWN, INP_LEFT, INP_RIGHT, INP_LEFT, INP_RIGHT, INP_TRIGB, INP_TRIG
};
static u8 kidx;   /* コナミ入力の進捗。RAM(data-loc)。init で0。 */

static void title_init(void) {
    kidx = 0;   /* 画は常駐が表示済み。ここは進捗リセットのみ(SCREEN12を壊さぬよう描画しない)。 */
}

static u8 title_update(void) {
    u8 e = g_input_edge;
    if (e) {
        /* コナミ進捗: 期待キーが押下エッジに含まれれば前進、外れたらリセット
           (押したのが先頭キー=UP ならそこから再開)。成立でコンフィグへ。 */
        if (e & konami[kidx]) {
            if (++kidx >= 10) { kidx = 0; return SC_CONFIG; }
        } else {
            kidx = (e & INP_UP) ? 1 : 0;
        }
    }
    /* 通常トリガ(A)でゲーム開始。※コナミ成立時は上で return 済み(こちらへ来ない)。 */
    if (e & INP_TRIG) return SC_STAGE;
    return SCENE_NONE;
}

/* バンク単一エントリ(bankhead の jp 先)。g_scene_phase で init/update を分岐。 */
void banked_entry(void) {
    if (g_scene_phase == 0) title_init();
    else g_scene_ret = title_update();
}
