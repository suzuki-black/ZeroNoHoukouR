/* pcm.h — turbo R 内蔵 PCM(8bit DAC)の非同期再生。実体は src/core/pcm.s。
   ★鳴り終わるまで待たない。ゲームの**待ちループ**が通りがかりに 1 サンプルずつ出す。
     止まるのは PCM の長さではなく、待ちを 1 周するたびの数十サイクルだけ。
   ★声は 1 本。面中のスネアと、画面で鳴らす叫びは同時に鳴らないので足りる。
   ★標本化は 7993Hz 固定(システムタイマ 32 カウント = 125.1us)。 */
#ifndef PCM_H
#define PCM_H

#include "types.h"

extern u8  g_pcm;        /* 1=PCM を使ってよい。起動時の検出と設定メニューが立てる。0 なら一切触らない */
extern u8  g_pcm_hw;     /* 検出結果(システムタイマが動いたか)。設定で g_pcm を戻すための控え */
extern u8  pcm_active;   /* 0 以外=再生中。鳴り終わりを待つ側が見る(★必ず上限フレーム数を付けて待つこと) */
extern u8  pcm_lock;     /* 0 以外=叫びが鳴っている。スネアは譲る(1 声しか無いので優先順位を決めてある) */
extern u16 g_pcm_src;    /* 鳴らす前に入れる: サンプルの先頭(RAM 番地) */
extern u16 g_pcm_len;    /* 同: 長さ(バイト) */

/* ★面中のスネア。番地と長さは pcm.s が持っている(ISR から呼ぶので呼ぶ側を小さくするため)。
   ★素材は page3 の RAM に居なければならない。ホット区間はページ1・2 とも RAM の複製で、
     カートの窓が 1 つも見えないので、バンクから直接は読めない。
   ★番地 0xEB00 は **DEBUG_PROF の計測用 RAM と同じ場所**。通常ビルドでは誰も触らない。
     ここしか空いていない: 0xC000-0xDF19 は常駐_DATA(うち hot_ram が 5696B)、0xE100 は曲データ、
     0xE700 は中ボスの向きデータ、0xEC00 は弾幕、0xEF00 以降はスタック(実測で 0xEF59 まで下りる)。
     RAM の実測地図は 苦労と教訓 §16-49。
   ★**枠**は 256B。実際に使う長さと標本化は pcm.s(PCM_SNARE_LEN / PCM_PERIOD)が決め、
     tools/gen_snare.py がそれを読んで波形を作る。C 側は枠の大きさしか知らなくてよい。 */
#define PCM_SNARE_ADDR 0xEB00u
#define PCM_SNARE_SLOT 256      /* 0xEB00 に取れる枠。実長はこれ以下(pcm.s が決める) */
extern u8 __at(PCM_SNARE_ADDR) pcm_snare_buf[PCM_SNARE_SLOT];   /* 実体は resram.c。__at は番地を決めるだけで _DATA を食わない */
void pcm_snare(void);

/* ★pcm_start は **ISR から**呼ぶ前提で割込みを触らない(ISR の中で ei してはいけない)。
     本体ループから呼ぶときは**呼ぶ側が di〜ei で囲うこと**(ISR の pcm_snare に割り込まれると
     状態が半端なまま書き換わる)。voice_play がその例。 */
void pcm_start(void);    /* g_pcm_src / g_pcm_len を入れてから呼ぶ。鳴っていても差し替える */
void pcm_stop(void);

/* ★叫び(いまはタイトルの「けんこんいってき」だけ)。音声バンクの先頭から**窓(0xA000)から直接**
   鳴らし、鳴り終わるまで待つ。
   ・RAM へ写さないので hot_ram を借りる必要も hot_load() で戻す必要も無い。
   ・**必ずフレーム上限で抜ける**(鳴らない機械・時刻が進まない環境でもハングしない)。
   ・BGM は ISR が鳴らし続けるので、タイトル曲を止めずに叫べる。
   ・呼ぶのは常駐から。バンク窓を差し替えるので、バンクシーンからは呼べない。 */
void voice_play(void);
/* ★送出口。ゲームの**待ちループ**から呼ぶ。鳴っていなければ 3 命令で戻る。
   壊すのは A とフラグだけ(HL/DE は中で退避する)。 */
void pcm_service(void);

#endif /* PCM_H */
