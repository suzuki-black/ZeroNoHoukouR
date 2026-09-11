/* curtain.c — CPU 弾幕。設計と制約は curtain.h を参照。 */
#include "curtain.h"
#include "fire.h"     /* dvx/dvy: 32分割方向の単位速度(fire.c と共有) */
#include "vdp.h"
#include "sprites.h"

CBul __at(CBUL_ADDR) g_cbul[CBUL_MAX];
u8 g_cbul_live;

/* 画面外カリングの範囲(1/16 px)。16px ぶん外へ出たら捨てる。 */
#define CB_XMIN (-16 * 16)
#define CB_XMAX (272 * 16)
#define CB_YMIN (-16 * 16)
#define CB_YMAX (228 * 16)

void curtain_reset(void) {
    u8 i;
    for (i = 0; i < CBUL_MAX; i++) g_cbul[i].alive = 0;
    g_cbul_live = 0;
}

void curtain_ring(s16 cx, s16 cy, u8 n, u8 spd, u8 ang, u8 col) {
    u8 i, k = 0;
    if (n == 0) return;
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

/* ★VDP に一切触れない純 RAM 演算。§4-1(VDPコマンドの裏でCPUを回す)の区間に置ける。 */
void curtain_update(void) {
    u8 i;
    CBul *b = g_cbul;
    for (i = 0; i < CBUL_MAX; i++, b++) {
        s16 x, y;
        if (!b->alive) continue;
        x = (s16)(b->x + b->vx);
        y = (s16)(b->y + b->vy);
        if (x < CB_XMIN || x > CB_XMAX || y < CB_YMIN || y > CB_YMAX) {
            b->alive = 0;
            if (g_cbul_live) g_cbul_live--;
            continue;
        }
        b->x = x; b->y = y;
    }
}

void curtain_draw(u8 base, u8 nper, u8 line) {
    u8 i, na = 0, nb = 0;
    const CBul *b = g_cbul;
    for (i = 0; i < CBUL_MAX; i++, b++) {
        u8 px, py;
        if (!b->alive) continue;
        { s16 sx = (s16)(b->x >> 4), sy = (s16)(b->y >> 4);
          if (sx < 0 || sx > 255 || sy < 0 || sy > 211) continue;   /* 画面内のみ */
          px = (u8)sx; py = (u8)sy; }
        /* ★分割線をまたぐ位置(スプライトは16px高)は、どちらの帯にも入れない。
           両セットの同じslotに別内容が入っている以上、またぐと境界で化けるため(split guide)。 */
        if ((u8)(py + 16) < line) {
            if (na < nper) { u8 s = (u8)(base + na);
                vdp_sprite_color_a(s, b->col); vdp_sprite_pos_a(s, px, py, SPR_BULLET); na++; }
        } else if (py >= line) {
            if (nb < nper) { u8 s = (u8)(base + nb);
                vdp_sprite_color_b(s, b->col); vdp_sprite_pos_b(s, px, py, SPR_BULLET); nb++; }
        }
    }
    /* 使わなかった予約slotは停止マーカで隠す(セットごとに独立) */
    if ((u8)(base + na) < 32) vdp_sprite_hide_from_a((u8)(base + na));
    if ((u8)(base + nb) < 32) vdp_sprite_hide_from_b((u8)(base + nb));
}
