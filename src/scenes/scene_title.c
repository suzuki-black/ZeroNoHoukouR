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
#include "vdp.h"        /* vdp_write_addr / vdp_data / vdp_glyph(常駐) */
#include "gamestate.h"  /* g_hiscore(電源が入っている間だけのハイスコア) */
#include "rank.h"       /* デモ(アトラクトモード)の状態 */
#include "sound.h"      /* fm_silence */

/* 隠しコマンド(コナミ): 上上下下左右左右 B A。成立で設定メニュー(SC_CONFIG)を開く。
   B=INP_TRIGB(キーB / ジョイ トリガ2 / キーM), A=INP_TRIG(キーA / スペース / ジョイ トリガ1)。 */
static const u8 konami[10] = {
    INP_UP, INP_UP, INP_DOWN, INP_DOWN, INP_LEFT, INP_RIGHT, INP_LEFT, INP_RIGHT, INP_TRIGB, INP_TRIG
};
static u8 kidx;   /* コナミ入力の進捗。RAM(data-loc)。init で0。 */
/* ★放置の時計(アトラクトモード)。何も触らずに TITLE_IDLE たったらデモへ(デモ → ランキング → タイトル)。
   JIFFY(割込みで進む 60Hz の時計)で測る。キーかジョイを触っている間は数え直す。 */
#define TITLE_IDLE 1200   /* 20 秒 */
static u16 idle_t0;
#define JIFFY (*(volatile u16 *)0xFC9E)

/* ★タイトルへハイスコアを「無理矢理」載せる。
   タイトルは SCREEN12(YJK 自然画)で、4 ドットごとに J/K を共有する。
   **J=K=0 のときバイトは「明度だけ」＝灰色**になるので、byte = Y<<3 で
   白(0xF8)と黒(0x00)を置けば、絵の上に白黒の文字を打てる(色はにじまない)。
   ★x は 4 の倍数にすること(J/K の組が 4 ドット単位。ずらすと隣の 4 ドットの色を壊す)。
   ★ここは SCREEN5 の描画(vdp_text/vdp_fill)を使ってはいけない(YJK 画が壊れる)。
     画そのものは常駐が bcall の前に毎回流し込むので、ここで書いた文字も入場のたびに描き直す。 */
static void yjk_text(u8 x, u8 y, const char *s) {
    u8 r, c, i;
    for (i = 0; s[i]; i++) {
        const u8 *g = vdp_glyph((u8)s[i]);
        for (r = 0; r < 8; r++) {
            u8 bits = g[r];
            vdp_write_addr((u16)((u16)(y + r) * 256 + x + ((u16)i << 3)));
            for (c = 0; c < 8; c++, bits = (u8)(bits << 1)) vdp_data((u8)((bits & 0x80) ? 0xF8 : 0x00));
        }
    }
}

static void title_hiscore(void) {
    char buf[6];
    u16 v = g_hiscore;
    u8 i;
    for (i = 5; i > 0; i--) { buf[i - 1] = (char)('0' + (u8)(v % 10)); v /= 10; }
    buf[5] = 0;
    yjk_text(8, 194, "HI");
    yjk_text(32, 194, buf);
}

static void title_init(void) {
    kidx = 0;   /* 画は常駐が表示済み。ここで描いてよいのは YJK の明度だけの文字(下の title_hiscore)。 */
    DEMO_END();   /* ★デモの途中でトリガを押して戻ってきたとき、設定を元に戻す */
    idle_t0 = JIFFY;
    title_hiscore();
}

static u8 title_update(void) {
    u8 e = g_input_edge;
    if (g_input) idle_t0 = JIFFY;                       /* 触っている間は放置とみなさない */
    else if ((u16)(JIFFY - idle_t0) >= TITLE_IDLE) {
        /* ★デモを始める: 設定を退避して、開始面・無敵・FM・PCM をデモ用に書き換える(戻すのは DEMO_END)。
           面は 1 周ごとに次へ(最終面は見せない)。音は psg() が止める(PSG の音量に 0 を書く) */
        DEMO_SAVE();
        g_stage_sel = g_demo_next;
        if (++g_demo_next >= DEMO_STAGES) g_demo_next = 0;
        fm_silence(); g_opll = OPLL_NONE;   /* ★先に止める(0 にしてからでは fm_silence が素通りして鳴りっぱなし) */
        g_invinc = 1; g_pcm = 0;
        g_demo_ret = 0; g_demo = 1;
        return SC_STAGE;
    }
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
