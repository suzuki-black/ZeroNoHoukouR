/* rank.h — ランキング(TOP5)の表。置き場は gamestate.h の RANK_ADDR(0xE0E0〜0xE0FF の 32B)。
   ★常駐には何も置かない(表は固定番地、触るのはバンクのコードだけ)。
   ・初期値は起動時に coldsetup(bank30)が入れる(FM の検出と同じ経路。COLD_OPLL)。
   ・表示は scene_ranking(RANK_BANK)。ネームエントリーも同じバンクに置く予定。
   ・電源を切ると消える(SRAM への保存は docs/ハイスコア設計.md の別件)。
   設計は docs/デモとランキング設計.md。 */
#ifndef RANK_H
#define RANK_H
#include "types.h"
#include "gamestate.h"   /* RANK_ADDR / RANK_BYTES */

#define RANK_N    5      /* 何位まで残すか */
#define RANK_NAME 3      /* 名前の文字数 */
#define RANK_BANK 67     /* ランキング画面(とネームエントリー)のバンク。1MB にして空いた 67〜127 の先頭 */

typedef struct {
    u16  score;
    char name[RANK_NAME];   /* 'A'〜'Z'。終端の 0 は持たない */
} RankEnt;                  /* 5B */

#define g_rank ((RankEnt *)RANK_ADDR)   /* g_rank[0] が 1 位 */
typedef char rank_fits[(RANK_N * sizeof(RankEnt) <= RANK_BYTES) ? 1 : -1];   /* ★予約の 32B に収まること */

#endif /* RANK_H */
