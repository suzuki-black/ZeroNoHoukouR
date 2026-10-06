/* resram.c — 常駐 RAM の「予約」だけを置くモジュール。
   ★RESIDENT_RELS の**最後**に置くこと。理由は番地の積み上がり方にある。

   _DATA はリンク順に積まれる。hot_ram(5696B) より**前**のモジュールで配列を増やすと
   hot_ram の開始番地がその分だけ下がる。ところが ovl_spin はきりもみの演出で
   hot_ram の 0xC600-0xD9FF を借りており、hot_ram が 0xC600 を越えて下がると収まらない
   (Makefile がリンク後に検査して止める。実際 g_ras をここへ移す最初の試みは
    hot_ram=0xC608 となって弾かれた。余白はわずか 100B しか無い)。

   末尾に置けば hot_ram の番地は動かず、_DATA の末尾に残っている空き
   (0xDEAE 以降。天井は 0xE000)をそのまま使える。
   ★ここに足すときは必ずビルドして「0xE000 まで残り」を確認すること。 */
#include "types.h"
#include "raster.h"
#include "pcm.h"
#include "bank.h"      /* bank_data/bank_restore: 叫びを窓から直接鳴らす */
#include "voice_data.h" /* 自動生成: 叫びのバンク番号と長さ */
#include "sound.h"      /* play_fanfare: 叫びと重ねる */

/* ラスタ分割表。以前は 0xEF00 へ固定していたが、そこはスタックの領域だった(raster.h 参照)。 */
RasSplit g_ras[RAS_MAX];

/* スネアの素材を置く枠 256B。★__at の配列は番地を決めるだけで _DATA を食わない。
   中身は起動時に coldsetup.c(冷たいバンク)が写す。 */
u8 __at(PCM_SNARE_ADDR) pcm_snare_buf[PCM_SNARE_SLOT];

/* ───────── 叫び(PCM 音声)を鳴らす ─────────
   ★素材はバンクに置いたまま、窓(0xA000)から直接読んで鳴らす。RAM へ写さない。
     1 語 1 バンク(8KB)に収めてあるので、窓を向けるだけで先頭から終わりまで読める。
   ★**呼ぶのは常駐から。** 窓を差し替えるので、バンクシーン(タイトル等)からは呼べない。
   ★語は id で選ぶ(0=乾坤一擲 / 1=敵撃破 / 2=総大将撃破)。バンク番号と長さは自動生成の
     voice_data.h から取る(Makefile がバンク番号を決め、生成器がヘッダへ出す。二重に持たない)。
   ★★**page2=cart の文脈で呼ぶこと。ここで ramx を触ってはいけない。**
     ramexec.h の規律: 「use_ram と use_cart の間では data_read/bcall を一切呼ばないこと」。
     最初ここで ramx_use_cart() 〜 ramx_use_ram() と囲ったら、**抜けたあとが RAM 側のまま**になり、
     直後の scene_video_enter(タイトル画の流し込み)と scene_bgm_enter(bgm_play→data_read)が
     窓の見えない状態でバンキングし、曲データの代わりに RAM のゴミを読んでゲームが壊れた
     (g_scene が 130 や 61 といった不正な値になった)。
     呼び出し位置(シーン切替・結果画面)はどちらも元からカート側なので、触る必要が無い。
   ★★待ちには**必ずフレーム上限**を付ける。「鳴り終わるまで待つ」だけだと、PCM が鳴らない機械や
     システムタイマが動かない環境で永久に戻らない(2026-10-05 に同じ型で起動直後に固まった)。
     上限に当たったら音を切って抜ける。音が途中で切れるだけで、ゲームは必ず進む。 */
#define VOICE_CAP_TICKS 150   /* 約2.5秒。いちばん長い「そうだいしょうげきは」で1.3秒 */

static const u8  voice_bank[VOICE_N] = {
    VOICE_KENKON_ITTEKI_BANK, VOICE_TEKI_GEKIHA_BANK, VOICE_SOUDAISHOU_GEKIHA_BANK };
static const u16 voice_len[VOICE_N]  = {
    VOICE_KENKON_ITTEKI_LEN,  VOICE_TEKI_GEKIHA_LEN,  VOICE_SOUDAISHOU_GEKIHA_LEN  };

/* 叫びを鳴らし始める(待たない)。鳴らせない機械では何もしない。
   ★窓は音声バンクを向いたまま戻る。戻すのは呼んだ側(voice_play / voice_fanfare)。 */
static void voice_start(u8 id) {
    if (!g_pcm) return;
    /* ★スネアに譲らせる。1 声しか無いので、叫んでいる間に拍が来ると上書きされて
       **拍 1 回で叫びが死ぬ**(pcm.s の経緯を参照)。 */
    pcm_lock = 1;
    bank_data(voice_bank[id]);
    g_pcm_src = (u16)BANK_SWAP_WIN;
    g_pcm_len = voice_len[id];
    /* ★本体ループから鳴らすので di〜ei で囲う(ISR の pcm_snare に割り込まれると状態が壊れる)。
       専用の入口を作ると常駐を 6B 食うので、ここで直接囲む。 */
    __asm di __endasm;
    pcm_start();
    __asm ei __endasm;
}

void voice_play(u8 id) {
    volatile u16 *jf = (volatile u16 *)0xFC9E;
    u16 t0 = *jf;
    voice_start(id);
    while (pcm_active && (u16)(*jf - t0) < VOICE_CAP_TICKS) pcm_service();
    pcm_stop();         /* ★錠も外れる。鳴っていなければ何もしない(pcm.s) */
    bank_restore();
}

/* ★叫びとファンファーレを**重ねて**鳴らす(結果画面)。**バンク(results_impl)から呼ぶ。**
   ・ファンファーレは前景で尺を取る(fanfare_seq → vdp_wait_frame)。その待ちの中で
     pcm_service が回るので、PSG を鳴らしながら叫べる。
   ・呼び元のバンクコードは窓(0xA000)の中で動いている。叫びの素材も同じ窓から読むので、
     鳴らしている間は窓を音声バンクへ向け、**戻る前に呼び元のバンク(g_bank)へ向け直す**。
     ここで bank_restore()(既定 bank3)を使うと、呼び元のコードが消えて戻り先を失う。
   ・ファンファーレ(約 3 秒)はいちばん長い叫び(1.4 秒)より長いので、鳴り終わりは待たない。
     待ちループが無いので、鳴らない機械でも戻らなくなることは無い。 */
void voice_fanfare(u8 id) {
    voice_start(id);
    play_fanfare();
    pcm_stop();
    bank_data(g_bank);
}
