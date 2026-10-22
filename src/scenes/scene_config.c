/* scene_config.c — ★設定メニュー(冷たいシーン=bank6)。タイトルで隠しコマンド(コナミ)から開く。
   UP/DOWN でカーソル移動、L/R で値変更、SPACE で START→ゲーム開始(SC_STAGE)。
   各項目を常駐 gamestate へ書き、gameplay が参照する。
   ※描画は「入場時に全描画→以後は変化した行だけ再描画」。全画面再描画は遅く入力を取りこぼすため。 */
#include "vdp.h"
#include "input.h"
#include "scene.h"
#include "gamestate.h"
#include "opll.h"      /* g_opll / g_opll_hw(FM音源の有無と使用可否) */
#include "sound.h"     /* fm_silence(切る瞬間に伸びている音を止める) */
#include "pcm.h"       /* g_pcm / g_pcm_hw / pcm_stop(切る瞬間に鳴っている音を止める) */
#include "version.h"   /* BUILD_VER(gitハッシュ)。どのコミットのROMか判別用 */
/* サウンドテスト: sfx / SFX_* / play_fanfare / g_rumble_lv は sound.h、snd_test / VOICE_N は pcm.h */

#define CFG_SOUND 9      /* SOUND TEST(SPACE で別画面) */
#define CFG_START 10     /* 項目0..8 ＋ SOUND TEST(9) ＋ START(10) */
#define ROWS 11

static u8 cur;   /* 0=難易度/1=残機/2=耐久/3=ステージ/4=継続/5=無敵/6=FM/7=PCM/8=VIEW/9=SOUND TEST/10=START。RAM(data-loc)。 */
static u8 g_mode;/* VIEW: 0=GAME(通常)/1=CARD(説明のみ)/2=RESULT(撃破画面のみ)/3=ENDING。START時に g_view へ反映 */

static const char *const diffs[3]  = { "EASY  ", "NORMAL", "HARD  " };
static const char *const livess[3] = { "2", "3", "5" };
static const char *const onoff[2]  = { "OFF", "ON " };
static const char *const num1_9[10]= { "0","1","2","3","4","5","6","7","8","9" };
static const char *const modes[4]  = { "GAME  ", "CARD  ", "RESULT", "ENDING" };   /* 画面ビューア */
/* ★FM と PCM は 3 状態を出す(ハードが無ければ切り替えさせない)。幅は 4 文字で揃える。
   PCM は turboR 内蔵なので本来いつでもあるが、エミュレータによっては**システムタイマが動かず**
   鳴らせない。起動時の検出(coldsetup.c の pcm_detect)が落ちた場合も NONE を出す。 */
static const char *const fmval[4]  = { "OFF ", "ON  ", "NONE", "ONLY" };   /* ONLY は PCM だけ(PCM_ONLY) */
static const char *const labels[ROWS] = {
    "DIFFICULTY", "LIVES", "DURABILITY", "STAGE", "CONTINUE", "INVINCIBLE",
    "FM SOUND", "PCM SOUND", "VIEW", "SOUND TEST", "START GAME"
};
/* ★行が増えるたびに上へ詰めている(版表示 180 / 操作説明 194 とぶつからないように)。
   いまは 24 から 14 間隔で 10 項目(SOUND TEST まで)、START だけ 1 行空けて 168。 */
static const u8 rowy[ROWS] = { 24, 38, 52, 66, 80, 94, 108, 122, 136, 150, 168 };

/* 行 idx の値文字列(START行は値なし=NULL)。 */
static const char *val_of(u8 idx) {
    if (idx == 0) return diffs[g_difficulty];
    if (idx == 1) return livess[g_lives_idx];
    if (idx == 2) return num1_9[g_durability];
    if (idx == 3) return num1_9[g_stage_sel + 1];
    if (idx == 4) return onoff[g_continue ? 1 : 0];
    if (idx == 5) return onoff[g_invinc ? 1 : 0];
    if (idx == 6) return g_opll_hw ? fmval[g_fm ? 1 : 0] : fmval[2];
    if (idx == 7) return g_pcm_hw ? fmval[(g_pcm == PCM_ONLY) ? 3 : (g_pcm ? 1 : 0)] : fmval[2];
    if (idx == 8) return modes[g_mode];
    return (const char *)0;   /* SOUND TEST / START GAME */
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
    vdp_text(88, 10, 15, 1, "- CONFIG -");
    for (i = 0; i < ROWS; i++) draw_row(i);
    vdp_text(24, 194, 14, 1, "UP/DN:SEL  L/R:CHG  SPACE:OK");
    vdp_text(8, 180, 12, 1, "V" GAME_VERSION " " BUILD_VER);   /* ★版(semver)＋ビルドタグ(git短縮ハッシュ) */
}

/* L/R で1項目の値を増減(範囲クランプ)。変化したら1を返す。 */
static u8 change(u8 idx, s8 d) {
    if (idx == 0) { s8 v = (s8)g_difficulty + d; if (v >= 0 && v <= 2) { g_difficulty = (u8)v; return 1; } }
    else if (idx == 1) { s8 v = (s8)g_lives_idx + d; if (v >= 0 && v <= 2) { g_lives_idx = (u8)v; return 1; } }
    else if (idx == 2) { s8 v = (s8)g_durability + d; if (v >= 1 && v <= 9) { g_durability = (u8)v; return 1; } }
    else if (idx == 3) { s8 v = (s8)g_stage_sel + d; if (v >= 0 && v < 6) { g_stage_sel = (u8)v; return 1; } }  /* 開始面(0..5。5=最終面)。面数変更時ここも更新 */
    else if (idx == 4) { u8 n = d > 0 ? 1 : (d < 0 ? 0 : g_continue); if (n != g_continue) { g_continue = n; return 1; } }
    else if (idx == 5) { u8 n = d > 0 ? 1 : (d < 0 ? 0 : g_invinc);   if (n != g_invinc)   { g_invinc = n;   return 1; } }
    else if (idx == 6) {   /* FM SOUND。ハードが無ければ触らせない(表示は NONE のまま) */
        u8 n = d > 0 ? 1 : (d < 0 ? 0 : g_fm);
        if (g_opll_hw && n != g_fm) {
            /* ★先に消す。g_opll を 0 にしてからでは fm_silence が何もしない(opll_w が素通り)ので、
               タイトル曲の和音が伸びたまま残る。 */
            if (!n) fm_silence();
            g_fm   = n;
            g_opll = n ? g_opll_hw : OPLL_NONE;   /* ★FM を触る所は全部 g_opll を見る */
            return 1;
        }
    }
    else if (idx == 7) {   /* PCM SOUND。OFF / ON / ONLY(ドラムは PCM だけ)。検出で落ちていれば触らせない(表示は NONE のまま) */
        s8 v = (s8)g_pcm + d;
        u8 n = (v < 0) ? 0 : (v > PCM_ONLY) ? PCM_ONLY : (u8)v;
        if (g_pcm_hw && n != g_pcm) {
            /* ★先に止める。g_pcm を 0 にしてからでは pcm_start が素通りするだけで、
               いま出している途中の音は止まらない(FM の fm_silence と同じ順序)。 */
            if (!n) pcm_stop();
            g_pcm = n;
            return 1;
        }
    }
    else if (idx == 8) { s8 v = (s8)g_mode + d; if (v >= 0 && v <= 3) { g_mode = (u8)v; return 1; } }   /* VIEW: GAME/CARD/RESULT/ENDING */
    return 0;
}

/* ───────── サウンドテスト(SOUND TEST の行で SPACE。別画面) ─────────
   L/R で番号を選び、SPACE で鳴らす。トリガB か EXIT の行の SPACE で設定画面へ戻る(タイトル曲に戻す)。
   ★曲と叫びは素材のバンクを窓に出して読むので、このバンクからは直接鳴らせない。常駐の snd_test(resram.c)が鳴らす。
     効果音(PSG)とファンファーレは窓を使わないので直接呼ぶ。
   ★FM・PCM は設定画面の FM SOUND / PCM SOUND のとおりに鳴る(切り替えて聴き比べられる)。 */
#define ST_BGM    0
#define ST_SE     1
#define ST_VOICE  2
#define ST_FAN    3
#define ST_STOP   4
#define ST_EXIT   5
#define ST_ROWS   6
#define BGM_N     11
#define SE_N      8
static u8 snd;   /* 1 = サウンドテストの画面 */
static u8 scur, s_bgm, s_se, s_voice;
static const char *const st_labels[ST_ROWS] = { "BGM", "SE", "VOICE", "FANFARE", "BGM STOP", "EXIT" };
static const u8 st_rowy[ST_ROWS] = { 40, 58, 76, 94, 112, 136 };
/* 曲の番号は tools/gen_assets.mjs の TRACKS の並び */
static const char *const bgm_names[BGM_N] = {
    "00 TITLE   ", "01 STAGE 1 ", "02 ENDING  ", "03 STAGE 2 ", "04 STAGE 3 ", "05 STAGE 4 ",
    "06 STAGE 5 ", "07 SEA     ", "08 FINAL   ", "09 MIDBOSS ", "10 ALERT   "
};
/* 効果音は sound.h の SFX_* の並び(SFX_SHOT=1 から)。ch は本編で鳴らしているチャンネル */
static const char *const se_names[SE_N] = {
    "1 SHOT     ", "2 DAMAGE   ", "3 LOOP     ", "4 HIT      ",
    "5 BOOM     ", "6 E.FIRE   ", "7 THUNDER  ", "8 RUMBLE   "
};
static const u8 se_ch[SE_N] = { 0, 0, 0, 1, 2, 1, 2, 2 };
static const char *const voice_names[VOICE_N] = { "1 KENKON   ", "2 TEKI     ", "3 SOUDAI   " };

static const char *st_val(u8 idx) {
    if (idx == ST_BGM) return bgm_names[s_bgm];
    if (idx == ST_SE) return se_names[s_se];
    if (idx == ST_VOICE) return g_pcm ? voice_names[s_voice] : "PCM OFF    ";
    return (const char *)0;
}
static void st_row(u8 idx) {
    u8 y = st_rowy[idx];
    const char *v = st_val(idx);
    vdp_text(40, y, (scur == idx) ? 11 : 14, 1, (scur == idx) ? ">" : " ");
    vdp_text(56, y, (scur == idx) ? 15 : 14, 1, st_labels[idx]);
    if (v) vdp_text(136, y, 15, 1, v);
}
static void st_draw_all(void) {
    u8 i;
    vdp_fill(0, 0, 256, 212, 1);
    vdp_text(76, 10, 15, 1, "- SOUND TEST -");
    for (i = 0; i < ST_ROWS; i++) st_row(i);
    vdp_text(56, 160, 12, 1, "FM");
    vdp_text(80, 160, 15, 1, g_opll_hw ? fmval[g_fm ? 1 : 0] : fmval[2]);
    vdp_text(128, 160, 12, 1, "PCM");
    vdp_text(160, 160, 15, 1, g_pcm_hw ? fmval[(g_pcm == PCM_ONLY) ? 3 : (g_pcm ? 1 : 0)] : fmval[2]);
    vdp_text(16, 194, 14, 1, "L/R:CHG SPACE:PLAY B:BACK");
}
static void st_change(s8 d) {
    if (scur == ST_BGM)   { s8 v = (s8)s_bgm + d;   if (v >= 0 && v < BGM_N)   s_bgm = (u8)v; }
    if (scur == ST_SE)    { s8 v = (s8)s_se + d;    if (v >= 0 && v < SE_N)    s_se = (u8)v; }
    if (scur == ST_VOICE) { s8 v = (s8)s_voice + d; if (v >= 0 && v < VOICE_N) s_voice = (u8)v; }
    st_row(scur);
}
/* 戻ったら 1 を返す */
static u8 st_update(u8 e) {
    if ((e & INP_DOWN) && scur < ST_EXIT) { u8 o = scur; scur++; st_row(o); st_row(scur); }
    if ((e & INP_UP)   && scur > 0)       { u8 o = scur; scur--; st_row(o); st_row(scur); }
    if (e & INP_RIGHT) st_change(+1);
    if (e & INP_LEFT)  st_change(-1);
    if ((e & INP_TRIGB) || ((e & INP_TRIG) && scur == ST_EXIT)) {
        snd_test(0);             /* ★タイトル曲に戻す(設定画面はタイトル曲を続けて鳴らしている) */
        snd = 0;
        draw_all();
        return 1;
    }
    if (e & INP_TRIG) {
        if (scur == ST_BGM) snd_test(s_bgm);
        else if (scur == ST_SE) {
            if (s_se + 1 == SFX_RUMBLE) g_rumble_lv = 12;   /* ★地鳴りの音量は鳴らす側が決める(本編は津波の進み具合) */
            sfx(se_ch[s_se], (u8)(s_se + 1));
        }
        else if (scur == ST_VOICE) snd_test((u8)(SNDT_VOICE | s_voice));   /* 鳴り終わるまで待つ(最長 1.4 秒) */
        else if (scur == ST_FAN) {
            /* ★鳴り終わるまで待つ(約 3 秒。曲は止まる)。尺はフレーム待ち(JIFFY)で取るので、先に割込みを許す。
                 バンクのシーンは di のまま呼ばれていて(bcall)、そのままだと JIFFY が進まず戻ってこなかった(openMSX)。
                 rank.h の DEMO_END と同じ作法 */
            __asm ei __endasm;
            play_fanfare();
        }
        else if (scur == ST_STOP) snd_test(SNDT_STOP);
    }
    return 0;
}

static void config_init(void) {
    vdp_set_display_page(0);
    cur = 0;
    snd = 0; scur = 0; s_bgm = 0; s_se = 0; s_voice = 0;   /* ★g_mode と同じ理由(0xE000 の共有 RAM)で毎回確定 */
    g_mode = 0;   /* ★VIEWを毎回GAMEへ確定。g_modeはbank6のstaticで0xE000共有RAM上=他バンクシーン
                     (ship_render等)が同域を使うため残留値になり得る。放置すると START時 g_view=残留 で
                     「カードのみ表示→タイトルへ戻る」誤動作(ステージ中リセット後に顕在化)を起こす。 */
    draw_all();
}

static u8 config_update(void) {
    u8 e = g_input_edge;
    if (snd) { st_update(e); return SCENE_NONE; }
    if ((e & INP_TRIG) && cur == CFG_SOUND) { snd = 1; scur = 0; st_draw_all(); return SCENE_NONE; }
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
