/* ovl_shock.c — 衝撃波ディストーション(設計メモ §2-B / ROADMAP P1 項目7)。
   ★狙い: 砲台撃破・被弾の瞬間に、震源から上下へ広がる「縦のうねり」を走らせる。
   ★★出力は **R#23 を走査線の途中で書き換えるだけ＝VRAM 転送ゼロ**。
     R#23(縦スクロール)は「画面の先頭にどの VRAM 行を出すか」なので、帯ごとに値を変えると
     その帯から下の表示が縦にずれる。ずれを +→−→0 と並べれば「うねり」になる。
     設計メモは「低解像度の変位マップを R800 で計算して VDP コピーで提示」という案だったが、
     ROADMAP §2-B ② の調査で **R#23 は mid-frame 変更が即時反映される非シームレス・レジスタ**
     (Grauw の screensplit guide)と分かったので、**コピーを1回も発行せずに**同じ絵が出せる。
     実測で確定した天井は VDP 帯域(204 B/ms)なので、これは天井を完全に避けた演出になる。

   ★制約と作法:
     ・1分割=1割込み。HBLANK 中に書けるレジスタは4本(間接で8本)、全行で割り込むと CPU を
       食い潰す(split guide の警告)。なので **帯単位**＝リング2本×3分割に限定する。
     ・分割で R#23 を動かしたら、それ以降の分割の R#19 を新しい R#23 で計算し直す
       (raster.h 作法 3-b。ras_apply が追従する)。
     ・動かした R#23 は次フレームの先頭まで残る。**フレーム先頭(line==0)で必ず基準へ戻す**
       (g_ras[0] の reg2 で R#23 を戻している)。戻さないと画面上端が前フレームのゆがみのまま出る。
     ・R#23 はスプライトにも効くので、うねりの帯にいる機体も一緒にずれる。衝撃波としては
       むしろ好都合なので、そのまま利用する。 */
#include "types.h"
#include "vdp.h"        /* SPR_R5_A / SPR_R5_B / g_vscroll */
#include "raster.h"
#include "gamestate.h"  /* SHOCK_* / g_shock_t / g_shock_y */

/* ★リングは「帯の入口で外向きにずらし、出口で基準へ戻す」の **2枚組**。
   R#23 を +d すると、その行から下は VRAM を d 行先から表示する＝**中身が d px 上へ動く**。
   なので震源より上の帯は +d(上へ逃げる)、下の帯は -d(下へ逃げる)。これで衝撃波が
   「震源から外へ押し出す」ように見える。出口で必ず基準へ戻すこと(戻さないとその下が全部ずれる)。

   ★★帯の幅を 16 行取っているのは見た目の都合ではなく **分割が発火する下限**。
     分割の割込みが上がってから ras_apply が次の R#19 を仕込むまでに、割込み突入＋レジスタ退避＋
     S#1 読み＋構造体の添字計算で数走査線ぶん過ぎてしまう。6 行間隔で並べたら **2本目以降が
     まるごと発火しなかった**(リング分割でパレットを赤にする切り分けで確定)。
     SHOCK_MIN_GAP 未満の間隔の分割は捨てる。 */
#define SHOCK_HALF        8    /* 帯の半幅 px(帯幅=16 行) */
#define SHOCK_MIN_GAP    12    /* 直前の分割からこの行数より近い分割は捨てる(発火しないため) */
#define SHOCK_RINGS_MERGE 28   /* リング半径がこれ未満のうちは上下の帯が重なっているので1本で描く */

/* g_ras[] を組み立てて分割数を返す。行の昇順に:
     [0] フレーム先頭 = スプライト表セットA(R#5) ＋ 表示起点を基準へ(R#23)
     …  衝撃波リング(R#23)
     …  分割行 = スプライト表セットB(R#5)
   衝撃波が出ていないときは従来どおりの2分割になる。 */
u8 ovl_shock_build(u8 split_line) {
    u8  base = g_vscroll;
    u8  n, i, k, m = 0, put_b = 0;
    s16 r, amp, last, L;
    s16 lines[4];
    s8  w[4];
    u8  lastval = base;

    g_ras[0].line = 0;
    g_ras[0].reg  = 5;  g_ras[0].val  = SPR_R5_A;
    g_ras[0].reg2 = 23; g_ras[0].val2 = base;    /* ★前フレームのゆがみを必ず解く */
    g_ras[0].pidx = RAS_NOPAL;
    n = 1; last = 0;

    amp = 0;
    if (g_shock_t) {
        r   = (s16)((u16)(SHOCK_FRAMES - g_shock_t) * SHOCK_SPEED);
        amp = (s16)(((u16)g_shock_t * SHOCK_AMP + SHOCK_FRAMES / 2) / SHOCK_FRAMES);
        if (amp) {
            if (r < SHOCK_RINGS_MERGE) {          /* 出だしは上下が重なっている=1本 */
                lines[m] = (s16)g_shock_y - SHOCK_HALF; w[m] =  4; m++;
                lines[m] = (s16)g_shock_y + SHOCK_HALF; w[m] =  0; m++;
            } else {                              /* 上下2本に分かれて外へ広がる */
                lines[m] = (s16)g_shock_y - r - SHOCK_HALF; w[m] =  4; m++;  /* 上の帯=上へ逃げる */
                lines[m] = (s16)g_shock_y - r + SHOCK_HALF; w[m] =  0; m++;
                lines[m] = (s16)g_shock_y + r - SHOCK_HALF; w[m] = -4; m++;  /* 下の帯=下へ逃げる */
                lines[m] = (s16)g_shock_y + r + SHOCK_HALF; w[m] =  0; m++;
            }
        }
    }

    for (i = 0; i < m; i++) {
        L = lines[i];
        /* 分割行(セットB切替)は絵より優先。行順を保つためここで先に入れる。 */
        if (!put_b && L >= (s16)split_line) {
            if (n < RAS_MAX && (s16)split_line > last) {
                g_ras[n].line = split_line;
                g_ras[n].reg  = 5;  g_ras[n].val  = SPR_R5_B;
                g_ras[n].reg2 = RAS_NOREG;
                g_ras[n].pidx = RAS_NOPAL;
                last = (s16)split_line; n++;
            }
            put_b = 1;
        }
        if (L < 2 || L > 210) continue;      /* 画面外(行0は先頭確定用に予約) */
        if (L <= last + SHOCK_MIN_GAP - 1) continue;   /* 近すぎ/逆行=発火しないので捨てる */
        if (n >= RAS_MAX) break;
        g_ras[n].line = (u8)L;
        g_ras[n].reg  = 23; g_ras[n].val = (u8)((s16)base + (amp * (s16)w[i]) / 4);
        g_ras[n].reg2 = RAS_NOREG;
        g_ras[n].pidx = RAS_NOPAL;
        lastval = g_ras[n].val;
        last = L; n++;
    }
    if (!put_b && n < RAS_MAX) {             /* 分割行がリングより下だった場合 */
        g_ras[n].line = split_line;
        g_ras[n].reg  = 5;  g_ras[n].val  = SPR_R5_B;
        g_ras[n].reg2 = RAS_NOREG;
        g_ras[n].pidx = RAS_NOPAL;
        n++;
    }
    /* ★末尾が基準に戻っていない(プロファイルの 0 が画面外で落ちた等)なら戻す分割を足す。
       これを怠ると画面下側がずれたままになる。 */
    if (lastval != base && n < RAS_MAX && last < 209) {
        g_ras[n].line = (u8)(last + 2);
        g_ras[n].reg  = 23; g_ras[n].val = base;
        g_ras[n].reg2 = RAS_NOREG;
        g_ras[n].pidx = RAS_NOPAL;
        n++;
    }
    return n;
}
