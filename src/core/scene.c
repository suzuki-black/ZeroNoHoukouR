/* scene.c — シーンFSM ディスパッチャ(常駐)。
   registry を1か所に集約。update の戻り値でシーン遷移。巨大 main() を作らない。
   bank!=0 のシーンは「冷たいシーン」= 当該バンクの 0xA000 エントリを bcall で実行(常駐窓を食わない)。
   バンク側エントリは g_scene_phase(0=init/1=update)を見て分岐し、update 結果を g_scene_ret に書く。 */
#include "scene.h"
#include "rank.h"      /* RANK_BANK(ランキング画面のバンク) */
#include "pcm.h"       /* frame_sync の待ちで PCM を 1 サンプルずつ出す */
#include "raster.h"
#include "vdp.h"
#include "entity.h"   /* g_sat_dirty / g_spr_base / g_spr_used */
#include "hud.h"      /* hud_draw: VBLANK 中に描く */   /* g_sat_dirty / g_spr_base / g_spr_used */
#include "input.h"
#include "vdp.h"
#include "bank.h"
#include "sound.h"
#include "gamestate.h"  /* g_crush_t: メガクラッシュ中だけ 60fps へ上げる */
#ifdef DEBUG_PROF
#include "prof.h"
#endif

/* シーン入場時に鳴らす BGM トラック。BGM_KEEP=変えない(継続) / BGM_OFF=停止。
   ★bgm_play は data_read(窓差替え)を伴うので常駐(scene_run)から呼ぶ=ここが正しい場所。 */
#define BGM_KEEP 0xFF
#define BGM_OFF  0xFE
static const u8 scene_bgm[SC_COUNT] = {
#if defined(MAGTEST) || defined(HSTEST) || defined(S3TEST) || defined(PARTTEST)
    /* SC_TITLE  */ BGM_OFF,   /* ★検証ROM: 起動シーンを差し替えているので曲は鳴らさない
                                  (タイトル曲が延々と鳴り続けて検証の邪魔になる) */
#else
    /* SC_TITLE  */ 0,         /* タイトル曲 */
#endif
    /* SC_CONFIG */ BGM_KEEP,  /* タイトル曲を継続 */
    /* SC_STAGE  */ BGM_OFF,   /* 入場時は無音: 開始カードでファンファーレのみ→stage_introがメインBGMを開始 */
    /* SC_ENDING */ 2,         /* 静かなED曲(track2) */
    /* SC_RANKING */ BGM_OFF,  /* 無音(アトラクトのデモも無音。タイトルへ戻ると曲が頭から鳴る) */
};
static void scene_bgm_enter(u8 cur) {
    u8 t = scene_bgm[cur];
    /* ★曲を続ける場合(BGM_KEEP)でも **FM は一度切る**。
       FM の和音と主旋律の重ねは ISR が「次に鳴らす音」を予約するだけで、OPLL へ書くのは
       **本体のループ**(fm_flush)。ところがシーンの切替は画面モードの変更と画面の描き直しで
       **実測 1.5 秒**ブロックするので、その間ループが回らず、PSG だけが進んで**和音が古い音を
       伸ばしたまま取り残される**＝「コナミコマンドで設定画面へ行くと FM と PSG がずれる」
       (実機で指摘。苦労と教訓 §16-46 と同じ型)。
       ★切っておけば、切替が終わった次のフレームに**今の音で鳴り直す**(fm_silence は添字も
         捨てるので、次の ISR が必ず予約し直す)。ブロック中は PSG だけになる。
       ★BGM_OFF/曲の差替えの経路は bgm_stop/bgm_play が中で fm_silence を通るので、ここは
         BGM_KEEP の穴だけを塞いでいる。 */
    if (t == BGM_KEEP) { fm_silence(); return; }
    if (t == BGM_OFF)  bgm_stop();
    else               bgm_play(t);
}

/* シーン入場時のビデオモード。SC_TITLE のみ SCREEN12(YJK自然画)、他は SCREEN5。
   タイトル画(54272B, bank9..15)の VRAM流し込みは窓差替えを伴うため常駐文脈でしか行えない
   (バンクシーン内からだと窓復元で自シーンを追い出す)。よって bcall より前のここで済ませる。
   モードが変わる時だけ CHGMOD する(g_vmode で追跡)。SCREEN5復帰時はパレットも張り直す。 */
#define TITLE_BANK    9
#define TITLE_YJK_LEN 54272
static u8 g_vmode;   /* 現在のスクリーンモード(5/12)。0=未設定で初回に必ず張る。 */
#ifdef S3TEST
/* ★検証ROM(S3TEST): 1面の戦艦の絵を常駐側で RAM へ読んでおく。
   ★実体は scene_stage.c にある。**ここへ書いてはいけない。** assets_data.h を取り込むことになり、
     あのヘッダの表は static なので実体がもう一組複製される(実測 112B。ヘッダ自身にも注意書きがある)。
     scene_stage.c は元から同じ表を持っているので、向こうなら関数の分しか増えない。 */
extern void s3test_load_card(void);
#endif

static void scene_video_enter(u8 cur) {
#if defined(MAGTEST) || defined(HSTEST) || defined(S3TEST) || defined(PARTTEST)
    u8 want = 5;   /* ★検証ROM: 起動シーン(bank31)も SCREEN5 */
#else
    u8 want = (cur == SC_TITLE) ? 12 : 5;
#endif
#ifdef S3TEST
    s3test_load_card();
#endif
    if (want == g_vmode) return;
    g_vmode = want;
    if (want == 12) {
        vdp_screen12();
        vdp_blit_bank_vram(TITLE_BANK, TITLE_YJK_LEN, 0, 0);
    } else {
        vdp_screen5();
        vdp_palette_game();
    }
}

/* --- 常駐シーンの実体。バンクシーンは registry の init/update=0 で bank に番号を持つ --- */
extern void stage_init(void);
extern u8   stage_update(void);

/* シーンID順に登録。SC_COUNT と個数を一致させること。 */
static const Scene registry[SC_COUNT] = {
#ifdef MAGTEST
    /* SC_TITLE  */ { 0,          0,           31 },   /* ★検証ROM: MAG 分割テスト(scene_magtest, bank31) */
#elif defined(HSTEST)
    /* SC_TITLE  */ { 0,          0,           31 },   /* ★検証ROM: 横スクロール分割テスト(scene_hstest, bank31) */
#elif defined(S3TEST)
    /* SC_TITLE  */ { 0,          0,           31 },   /* ★検証ROM: SCREEN3 ロトズームテスト(scene_s3test, bank31) */
#elif defined(PARTTEST)
    /* SC_TITLE  */ { 0,          0,           31 },   /* ★検証ROM: SCREEN3 パーティクルテスト(scene_parttest, bank31) */

#else
    /* SC_TITLE  */ { 0,          0,            5 },   /* 冷たいシーン: bank5(bcall)。起動シーン */
#endif
    /* SC_CONFIG */ { 0,          0,            6 },   /* 冷たいシーン: bank6        */
    /* SC_STAGE  */ { stage_init, stage_update, 0 },   /* ★連続縦スクロール面        */
    /* SC_ENDING */ { 0,          0,            7 },   /* 冷たいシーン: bank7        */
    /* SC_RANKING*/ { 0,          0,     RANK_BANK },   /* 冷たいシーン: bank67(TOP5) */
};

u8 g_scene;         /* 現在のシーンID(デバッグ/HUD/検証用に公開)   */
u8 g_scene_phase;   /* バンクシーンへの指示: 0=init / 1=update       */
u8 g_scene_ret;     /* バンク/常駐 update の戻り値(次シーンID)       */

/* シーン cur の phase(0=init,1=update)を実行。バンクシーンは bcall 経由。 */
static void call_scene(u8 cur, u8 phase) {
    const Scene *s = &registry[cur];
    if (s->bank == 0) {
        if (phase == 0) s->init();
        else g_scene_ret = s->update();
    } else {
        g_scene_phase = phase;
        bcall_to(s->bank);   /* バンク側 banked_entry が g_scene_phase を見て分岐 */
    }
}

#ifdef DEBUG_FPS
u8  g_fps;         /* 実FPS(JIFFY増分を校正して算出)。hud_drawが2桁表示 */
u16 g_frame;       /* ★JIFFY非依存: ループ毎+1。ストップウォッチ実測用(検証)。hud_drawが4桁表示 */
static u16 fps_n = 1;  /* JIFFYの1VBLANKあたり増分(実機turboRでは1でない疑い=起動時に実測校正) */
#endif
/* ★フレームの同期。**フレーム先頭(t0)から n ティック目まで**待つ＝絶対時刻での同期。
   ★これを「処理のあとで vdp_wait_frame() を n 回」にしてはいけない。あの形だと処理が 1 VBLANK を
     超えたぶんがそのまま尺に足し算され、30fps のつもりが 20fps/15fps に落ちる
     (実機BIOS の openMSX で 17fps を実測した原因がこれだった。2026-10-01)。
   ★すでに n ティック以上かかっているフレームは**待たずに出る**。遅れを次フレームへ持ち越さない
     (持ち越すと一度詰まると延々 20fps のままになる)。VBLANK 整列は保てないが、処理が 1 VBLANK を
     超えている時点で描画は表示区間へかかっているので、整列しても得は無い。
   ★JIFFY が止まってもハングしないよう、待ちはガード付き。 */
static u16 ft0;

static void frame_sync(u16 t0, u8 n) {
    volatile u16 *jf = (volatile u16 *)0xFC9E;
    while ((u16)(*jf - t0) < n) { }   /* 既に n ティック過ぎていたら即出る(遅れを引きずらない) */
}

void scene_run(u8 cur) {
    g_scene = cur;
    scene_video_enter(cur);  /* ビデオモード確立(SC_TITLEはSCREEN12化＋YJK流し込み)を先に */
    scene_bgm_enter(cur);    /* 窓=bank3 の常駐文脈で(bcall前に)曲を差替える */
    call_scene(cur, 0);
#ifdef DEBUG_FPS
    /* ★JIFFY(0xFC9E)の1VBLANKあたり増分を実測校正。実機turboRでは+1でない疑いがあり、
       これを測らないとFPS換算(60=1秒)が狂う。ガード(g)付きでJIFFY停止時もハングしない。 */
    { volatile u16 *jf = (volatile u16 *)0xFC9E; u16 a, b, g;
      a = *jf; g = 0; while (*jf == a && ++g) { }   /* 変化境界へ整列 */
      a = *jf; g = 0; while (*jf == a && ++g) { }   /* ちょうど1VBLANK分 */
      b = *jf; fps_n = (u16)(b - a); if (fps_n == 0) fps_n = 1; }
#endif
    ft0 = *(volatile u16 *)0xFC9E;   /* 最初のフレームの基準 */
    for (;;) {
#ifdef DEBUG_PROF
        u16 _pc = prof_tick();   /* 計算区間(input+update)開始 */
#endif
        /* ★デモ(アトラクトモード)の間は入力を読まない。入力は前のフレームの終わりにデモのバンクが置いている。 */
        if (!g_demo) input_poll();
        g_scene_ret = SCENE_NONE;
        call_scene(cur, 1);
        /* ★デモのバンク(banked/demo.c)は面の処理の**後**で呼ぶ。本物の入力を読んで抜けるか決め(抜けるなら
           g_scene_ret を直接書く)、次のフレームの自動操縦の入力を置く。前に呼ぶと、抜け先を面の処理の戻り値が
           上書きするので、印付きの抜け先を後で書き戻す手間が要った(常駐が足りなくなって組み直した)。 */
        if (g_demo) bcall_to(DEMO_BANK);
        if (g_scene_ret != SCENE_NONE && g_scene_ret != cur) {
            /* ★シーンを抜けるときは分割とスプライト表のミラーを必ず止める。
               分割を張ったままバンクシーン(タイトル/エンディング)へ行くと、分割行から下は
               セットB(ステージ用の内容)を見にいくのでそのシーンのスプライトが消える。 */
            raster_off();
            g_spr_dual = 0;
            /* ★スプライト属性の「溜め」も捨てる。溜め(sat_shadow)は**前のシーンの中身**で、
               ここで捨てないと、このループの最後の VBLANK の仕事が**新しいシーンの画面へ
               前のシーンのスプライトを書き戻す**。
               ★最終面をクリアしたときだけ実機で見えていた(「PUSH SPACE を押すと一瞬 自機などが
                 2〜3 個出てから消える」)。stage_update は ent_draw_all() で溜めを作った**後**に
                 結果画面で数秒ブロックし、戻って SC_ENDING を返す。1〜5面は次の面を組み直すので
                 上書きされて見えないが、最終面はエンディングへ抜けるのでそのまま出ていた。 */
            g_sat_dirty = 0;
            /* ★叫び「乾坤一擲！」。ゲームを始める瞬間(タイトル/設定画面 → ステージ)に鳴らす。
               ★ここに置くと**タイトル曲が鳴ったまま**叫べる(曲の差替えは下の scene_bgm_enter で、
                 まだ起きていない)。しかも既にある分岐なので常駐がほとんど増えない。
               ★ステージ間の面送り(stage_intro)はこの分岐を通らないので、面が変わるたびには鳴らない。 */
            if (g_scene_ret == SC_STAGE) voice_play(VOICE_KENKON);   /* ★デモでは鳴らない(PCM を切ってから来るので voice_play が素通り) */
            cur = g_scene_ret;
            g_scene = cur;
            scene_video_enter(cur);
            scene_bgm_enter(cur);
            call_scene(cur, 0);
        }
#ifdef DEBUG_FPS
        g_frame++;   /* ★JIFFY非依存の真フレーム数(ストップウォッチ検証用) */
        /* 実FPS: 校正済み fps_n を使い「JIFFYが 60*fps_n 進む=実時間1秒」あたりのループ数=FPS。 */
        { static u16 lastj; static u8 fc;
          u16 j = *(volatile u16 *)0xFC9E;
          fc++;
          if ((u16)(j - lastj) >= (u16)(60 * fps_n)) { g_fps = (fc > 99) ? 99 : fc; fc = 0; lastj = j; } }
#endif
    /* ★30fps固定(ステージのみ): §4-1/§4-3の高速化で1フレームの計算が1 VBLANK内に収まり60fps化した
       結果、ゲーム速度(=フレームレート依存の設計)が2倍になった。ステージは 2 VBLANK 待ちで意図した
       30fpsへ固定し、元の手触り(敵速/弾速/スクロール/spawn)を維持する。計算に余裕があるので"絶対に
       落ちない安定30fps"になる(従来の20〜30fps揺れの本質的解決)。他シーンは高速化対象外=従来通り1 VBLANK。
       ★60fpsぬるぬる化(全速度定数を1/2へ再調整)へ進めたくなったら、この2回目のwaitを外す。
       ★★メガクラッシュ中(g_crush_t)だけは 2回目の wait を外して 60fps で回す。ゲームは全部
         止まっているのでフレームレート依存の速度定数に影響しない。津波スプライトのコマ送りが
         倍細かくなって**ドット単位に滑らかに**見え、同時に演出の総尺も短くなる(実測 3.9秒→3秒台)。 */
#ifdef DEBUG_PROF
        if (cur == SC_STAGE) {   /* ★計測はステージ(SCREEN5)中のみ。タイトル(SCREEN12)でpage切替すると壊れる */
            u16 comp = (u16)(prof_tick() - _pc);   /* 計算区間tick(cmd_wait含む) */
            g_prof_acc[PF_COMPUTE] += comp;
            frame_sync(ft0, (u8)((!g_crush_t) ? 2 : 1));
            prof_frame_end(comp);
        } else {
            frame_sync(ft0, 1);
        }
#else
        frame_sync(ft0, (u8)((cur == SC_STAGE && !g_crush_t) ? 2 : 1));
#endif
        ft0 = *(volatile u16 *)0xFC9E;   /* 次のフレームの基準(同期の出口＝ティック境界) */
        /* ★スプライト属性の転送は**ここ**(VBLANK の直後)で。ent_draw_all の中で書くと
           フレームの中ほど(走査線 130 行目付近)になり、画面の上半分と下半分で座標が食い違って
           「走査線抜け」に見える(実機で多発。2026-10-01 に走査線番号で確認)。
           控え(sat_shadow)は RAM なのでいつ作ってもよく、VRAM へ出す時刻だけが問題。 */
        if (cur == SC_STAGE && g_hud_on && !g_demo) hud_draw(g_score, g_lives);   /* ★デモ中は HUD の枠に PRESS SPACE KEY(demo.c) */   /* ★HUD も VBLANK 中に(上端は最もラスタ競合しやすい) */
        if (g_sat_dirty) { g_sat_dirty = 0; vdp_sat_flush(g_spr_base, g_spr_used);
            /* ★色表も**ここ**で。ent_draw_all の中で書くと色だけ1フレーム先になり、表示の途中で
               枠の色が次の持ち主の色へ変わる(自機の影が白く点滅した。entity.c の cdirty 参照)。
               位置(SAT)の方を先に出すのは、遅れたときに目立つのが位置だから。 */
            ent_col_flush(); }
        fm_flush();   /* ★FM の和音は VBLANK の仕事を**終えてから**(ISR でやると転送を押し出す) */
    }
}
