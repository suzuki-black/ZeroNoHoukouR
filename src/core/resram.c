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

/* ラスタ分割表。以前は 0xEF00 へ固定していたが、そこはスタックの領域だった(raster.h 参照)。 */
RasSplit g_ras[RAS_MAX];

/* スネアの素材を置く枠 256B。★__at の配列は番地を決めるだけで _DATA を食わない。
   中身は起動時に coldsetup.c(冷たいバンク)が写す。 */
u8 __at(PCM_SNARE_ADDR) pcm_snare_buf[PCM_SNARE_SLOT];

/* ───────── 叫び(PCM 音声)を鳴らす ─────────
   ★素材はバンクに置いたまま、窓(0xA000)から直接読んで鳴らす。RAM へ写さない。
     1 語 1 バンク(8KB)に収めてあるので、窓を向けるだけで先頭から終わりまで読める。
   ★**呼ぶのは常駐から。** 窓を差し替えるので、バンクシーン(タイトル等)からは呼べない。
   ★いまは「けんこんいってき」1 語だけ(常駐が尽きているため。結果画面の 2 語は常駐を空けてから)。
     語を選べるようにすると引数か表が要り、そのぶん常駐が増える。鳴る形を先に確かめる。
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

void voice_play(void) {
    volatile u16 *jf = (volatile u16 *)0xFC9E;
    u16 t0;
    if (!g_pcm) return;
    /* ★スネアに譲らせる。1 声しか無いので、叫んでいる間に拍が来ると上書きされて
       **拍 1 回で叫びが死ぬ**(pcm.s の経緯を参照)。 */
    pcm_lock = 1;
    bank_data(VOICE_KENKON_ITTEKI_BANK);
    g_pcm_src = (u16)BANK_SWAP_WIN;
    g_pcm_len = VOICE_KENKON_ITTEKI_LEN;
    /* ★本体ループから鳴らすので di〜ei で囲う(ISR の pcm_snare に割り込まれると状態が壊れる)。
       専用の入口を作ると常駐を 6B 食うので、ここで直接囲む。 */
    __asm di __endasm;
    pcm_start();
    __asm ei __endasm;
    t0 = *jf;
    while (pcm_active && (u16)(*jf - t0) < VOICE_CAP_TICKS) pcm_service();
    pcm_stop();
    pcm_lock = 0;
    bank_restore();
}
