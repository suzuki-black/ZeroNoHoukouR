/* rank.h — ランキング(TOP5)の表。置き場は gamestate.h の RANK_ADDR(0xE0E0〜0xE0FF の 32B)。
   ★常駐には何も置かない(表は固定番地、触るのはバンクのコードだけ)。
   ・初期値は起動時に coldsetup(bank30)が入れる(FM の検出と同じ経路。COLD_OPLL)。
   ・表示は scene_ranking(RANK_BANK)。ネームエントリーも同じバンクに置く予定。
   ・電源を切ると消える(SRAM への保存は docs/ハイスコア設計.md の別件)。
   設計は docs/デモとランキング設計.md。 */
#ifndef RANK_H
#define RANK_H
#include "types.h"
#include "gamestate.h"   /* RANK_ADDR / RANK_BYTES / g_stage_sel / g_invinc / g_fm */
#include "pcm.h"         /* g_pcm(デモ中は切る) */
#include "opll.h"        /* g_opll / g_opll_hw(デモ中は FM を切る) */
#include "hud.h"         /* SPR_TOP */

#define RANK_N    5      /* 何位まで残すか */
#define RANK_NAME 3      /* 名前の文字数 */
#define RANK_BANK 67     /* ランキング画面(とネームエントリー)のバンク。1MB にして空いた 67〜127 の先頭 */

typedef struct {
    u16  score;
    char name[RANK_NAME];   /* 'A'〜'Z'。終端の 0 は持たない */
} RankEnt;                  /* 5B */

#define g_rank ((RankEnt *)RANK_ADDR)   /* g_rank[0] が 1 位 */
typedef char rank_fits[(RANK_N * sizeof(RankEnt) <= RANK_BYTES) ? 1 : -1];   /* ★予約の 32B に収まること */

/* ★アトラクトモード(デモ)の状態。表の後ろに余る 7B(0xE0F9〜0xE0FF)に置く。常駐 RAM は残り 3B しか無い。
   常駐のコードからも 1 命令で読めるよう、番地は定数にしてある(DEMO_ADDR は asm からも使う)。 */
#define ATT_ADDR     (RANK_ADDR + RANK_N * 5)        /* 0xE0F9 */
#define DEMO_ADDR    0xE0F9                          /* = ATT_ADDR。asm 用の数字(下の検査で一致を確かめる) */
#define g_demo       (*(volatile u8 *)(ATT_ADDR + 0)) /* 0 以外 = デモ中(1 = 始まったばかり、2 = 走っている) */
#define g_demo_ret   (*(volatile u8 *)(ATT_ADDR + 1)) /* (空き。以前は抜け先を置いていた。いまはデモのバンクが g_scene_ret を直接書く) */
#define g_demo_next  (*(volatile u8 *)(ATT_ADDR + 2)) /* 次に見せる面(0〜4)。最終面は見せない */
#define g_demo_sv    (*(volatile u8 *)(ATT_ADDR + 3)) /* 退避した設定を 1B に詰めたもの(下の DEMO_SAVE) */
#define g_demo_t0    (*(volatile u16 *)(ATT_ADDR + 4))/* デモが始まった JIFFY */
#define g_demo_prev  (*(volatile u8 *)(ATT_ADDR + 6)) /* 前のフレームの自動操縦の入力(押した瞬間を作る) */
/* ★デモの状態はすべてここ。デモのバンクの static(0xE000〜)は使えない: 面の途中で呼ぶ他のバンクのコード
   (警報の文字・中ボスの準備など)も同じ帯に static を置くので、取り合って壊れる。 */
typedef char att_fits[(ATT_ADDR + 7 <= RANK_ADDR + RANK_BYTES) ? 1 : -1];
typedef char att_addr_ok[(ATT_ADDR == DEMO_ADDR) ? 1 : -1];
#define DEMO_BANK    68      /* デモの自動操縦(banked/demo.c) */
/* ゲーム(エンティティ・中ボス・弾幕)が使ってよいスプライトの枠の上限。デモ中は HUD を描かないので 32 まで
   (先頭 8 枠を PRESS SPACE KEY に譲るぶん)。常駐の scene_stage.c と同じ決まり。 */
#define SPR_GAME_TOP ((u8)(g_demo ? 32 : SPR_TOP))
#define DEMO_STAGES  5       /* デモで回す面の数(1〜5 面。最終面は内緒) */
#define DEMO_SHIP_FROM 3     /* この面(0 基点)からはデモで中ボスを飛ばして戦艦を見せる(4・5 面。ユーザー指定 2026-10-09)。
                                ★4 面の He 111 は絵の表を切り替え、5 面の P-61 はスプライトを拡大するので、
                                  中ボスの間は PRESS SPACE KEY をスプライトで出せない */

/* デモを始める(タイトルから)／終える(ランキング・タイトルの入場で)。どちらもバンクのコードから呼ぶ。
   ★設定は退避して戻す(デモは開始面・無敵・FM・PCM を一時的に書き換える)。 */
/* デモの間だけ書き換える設定: 開始面(0〜5, 3bit) / 無敵(1bit) / PCM(0〜2, 2bit) を 1B に詰めて退避する。
   ★FM は設定(g_fm)ではなく「いま使ってよいか」(g_opll)を 0 にする。音を出す側は g_opll しか見ない。
     戻すときは設定どおり(g_fm が ON ならハードの値)。0 にする前に fm_silence で鳴っている音を止めること。
   ★マクロにしてある: ヘッダに static 関数を置くと、使わない常駐のファイルにも本体ができて常駐を食う。 */
#define DEMO_SAVE() (g_demo_sv = (u8)((g_stage_sel & 7) | ((g_invinc & 1) << 3) | ((g_pcm & 3) << 5)))
/* ★戻す前に、鳴っている途中の効果音が終わるのを待つ。デモ中は音量だけ 0 にして効果音は裏で進めているので、
     残り時間のあるうちに戻すと、その残り(敵の発砲の「プッ」など)が聞こえた(2026-10-10 ユーザー指摘)。
     割込みで進むので ei してから(バンクのシーンは di のまま来る)。使う側は sound.h と vdp.h を取り込むこと。 */
#define DEMO_END() do { if (g_demo) { u8 _v = g_demo_sv; \
    __asm ei __endasm; while (snd_active) vdp_wait_frame(); \
    g_stage_sel = (u8)(_v & 7); g_invinc = (u8)((_v >> 3) & 1); g_pcm = (u8)((_v >> 5) & 3); \
    g_opll = g_fm ? g_opll_hw : OPLL_NONE; \
    g_demo = 0; g_demo_ret = 0; } } while (0)

#endif /* RANK_H */
