/* ovl_curtain.c — CPU弾幕の本体。★常駐ではなく RAM オーバレイ(0xA000, page2=RAM時)で動く。
   設計と制約は overlay.h / curtain.h を参照。要点:
     ・ホット区間(ramx_use_ram〜ramx_use_cart)の中でしか呼ばれない
     ・ここから data_read/bcall は呼べない(page2 が RAM＝スワップ窓が無い)
     ・常駐の関数/データ(vdp_sprite_*, g_cbul, dvx/dvy, g_scroll_dy)は通常どおり呼べる
       (page1 は同一内容の RAM 複製＝番地はそのまま) */
#include "curtain.h"
#include "fire.h"
#include "vdp.h"
#include "sprites.h"
#include "scroll.h"
#include "entity.h"   /* ent_player_hit: 被弾の共通処理 */
#include "player.h"   /* g_player_x/y */

/* 画面外カリングの範囲(1/16 px)。16px ぶん外へ出たら捨てる。 */
#define CB_XMIN (-16 * 16)
#define CB_XMAX (272 * 16)
#define CB_YMIN (-16 * 16)
#define CB_YMAX (228 * 16)

void ovl_curtain_ring(s16 cx, s16 cy, u8 n, u8 spd, u8 ang, u8 col) {
    u8 i, k = 0;
    if (n == 0) return;
    /* ★表示能力(帯あたりの予約slot×帯数)を超えて撒かないこと。超えると「今フレームどの弾を
       出すか」が毎フレーム変わって**ちらつき**になる。計算は 64発でも余裕だが、出せない弾を
       抱えても見た目が悪くなるだけ。実機で指摘されて入れた上限。 */
    if ((u8)(g_cbul_live + n) > CBUL_SOFT_MAX) return;   /* ★リング単位で空きを見る(1発ずつ見ると
                                                            リングが欠けて非対称になる。発射前に一度
                                                            だけ見る書き方だと n 発ぶん超過する) */
    for (i = 0; i < n; i++) {
        u8 d = (u8)((ang + (u8)((u16)i * 32 / n)) & 31);
        /* 空きスロットを探す(前回の続きから見るほど速いが、まずは素直に) */
        while (k < CBUL_MAX && g_cbul[k].alive) k++;
        if (k >= CBUL_MAX) return;              /* 満杯: 以降は捨てる(上限で頭打ち=安全側) */
        g_cbul[k].x = (s16)(cx << 4);
        g_cbul[k].y = (s16)(cy << 4);
        g_cbul[k].vx = (s8)((s16)dvx[d] * spd);  /* 1/16px/frame。dvx は半径8基準 */
        g_cbul[k].vy = (s8)((s16)dvy[d] * spd);
        g_cbul[k].col = col;
        g_cbul[k].alive = 1;
        g_cbul_live++;
        k++;
    }
}

/* ★VDP に一切触れない純 RAM 演算。§4-1(VDPコマンドの裏でCPUを回す)の区間に置ける。
   ★スクロール追従(既存 bh_bullet と同じ規約): 敵弾は艦(背景)と一緒に縦スクロールへ流れる。
     論理Y自体を流すので、表示・当たり判定・画面外消滅が全て視覚と一致する
     (自機弾=TEAM_PLAYER だけが画面固定)。これを入れないと、スクロール中に背景に対する
     見かけの速度が変わってしまう(上りで遅く、下りで速く見える)。 */
void ovl_curtain_update(void) {
    u8 i;
    s16 dy16 = (s16)(g_scroll_dy << 4);   /* px → 1/16px */
    CBul *b = g_cbul;
    for (i = 0; i < CBUL_MAX; i++, b++) {
        s16 x, y;
        if (!b->alive) continue;
        x = (s16)(b->x + b->vx);
        y = (s16)(b->y + b->vy - dy16);
        if (x < CB_XMIN || x > CB_XMAX || y < CB_YMIN || y > CB_YMAX) {
            b->alive = 0;
            if (g_cbul_live) g_cbul_live--;
            continue;
        }
        b->x = x; b->y = y;
    }
}

void ovl_curtain_draw(u8 base, u8 nper, u8 line) {
    u8 i, ns = 0, na, nb;
    const CBul *b;
    /* --- 1) 分割線をまたぐ弾: 両セットの**同じslotに同じ内容**で置く ---
       ★またぐ弾を「どちらの帯にも入れない」で捨てると、弾が分割線を越える数フレームだけ
         消える＝ちらつきに見える(実機で指摘された)。split guide の要求は「またぐスプライトは
         両テーブルの同じ位置に同じ内容」なので、捨てずに両方へ同内容で置くのが正しい。 */
    b = g_cbul;
    for (i = 0; i < CBUL_MAX && ns < nper; i++, b++) {
        s16 sx, sy;
        if (!b->alive) continue;
        sx = (s16)(b->x >> 4); sy = (s16)(b->y >> 4);
        if (sx < 0 || sx > 255 || sy < 0 || sy > 211) continue;
        if ((u8)(sy + 16) >= line && (u8)sy < line) {     /* またいでいる */
            u8 s = (u8)(base + ns);
            vdp_sprite_color_a(s, b->col); vdp_sprite_pos_a(s, (u8)sx, (u8)sy, SPR_BULLET);
            vdp_sprite_color_b(s, b->col); vdp_sprite_pos_b(s, (u8)sx, (u8)sy, SPR_BULLET);
            ns++;
        }
    }
    /* --- 2) 片側に収まる弾: それぞれの帯のセットへ --- */
    na = ns; nb = ns;
    b = g_cbul;
    for (i = 0; i < CBUL_MAX; i++, b++) {
        s16 sx, sy;
        if (!b->alive) continue;
        sx = (s16)(b->x >> 4); sy = (s16)(b->y >> 4);
        if (sx < 0 || sx > 255 || sy < 0 || sy > 211) continue;
        if ((u8)(sy + 16) < line) {
            if (na < nper) { u8 s = (u8)(base + na);
                vdp_sprite_color_a(s, b->col); vdp_sprite_pos_a(s, (u8)sx, (u8)sy, SPR_BULLET); na++; }
        } else if ((u8)sy >= line) {
            if (nb < nper) { u8 s = (u8)(base + nb);
                vdp_sprite_color_b(s, b->col); vdp_sprite_pos_b(s, (u8)sx, (u8)sy, SPR_BULLET); nb++; }
        }
    }
    if ((u8)(base + na) < 32) vdp_sprite_hide_from_a((u8)(base + na));
    if ((u8)(base + nb) < 32) vdp_sprite_hide_from_b((u8)(base + nb));
}

/* 自機との当たり。★既存の敵弾と同じ許容(±6px, 弾の左上同士で比較)。
   被弾処理は常駐の ent_player_hit に集約してあるので、無敵時間・耐久・ミス判定・手応え(hitstop/shake)は
   既存の敵弾と完全に同じ挙動になる。1フレームに1発だけ処理すれば十分(被弾は無敵時間を張るため)。 */
void ovl_curtain_collide(void) {
    u8 i;
    CBul *b = g_cbul;
    s16 px = (s16)g_player_x, py = (s16)g_player_y;
    for (i = 0; i < CBUL_MAX; i++, b++) {
        s16 dx, dy;
        if (!b->alive) continue;
        dx = (s16)(b->x >> 4) - px; if (dx < 0) dx = -dx; if (dx >= 6) continue;
        dy = (s16)(b->y >> 4) - py; if (dy < 0) dy = -dy; if (dy >= 6) continue;
        b->alive = 0;                              /* 当たった弾は消す(すり抜け防止) */
        if (g_cbul_live) g_cbul_live--;
        ent_player_hit(px, py);
        return;
    }
}
