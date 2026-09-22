/* ovl_bgbul.c — 背景に描く敵弾(bgbul.h)。5面の P-61(ovl12)専用(1面・4面は容量の都合で hot_hb.c の小型版を使う)。
   ★元は ovl_mb_p61.c にあったもの(実機で 1発 0.13ms と測った差分描き)をそのまま切り出した。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"     /* ent_player_hit */
#include "gamestate.h"  /* g_loop_t */
#include "player.h"     /* g_player_x/y */
#include "aa_hot.h"     /* cam */
#include "raster.h"     /* g_ras */
#include "sprites.h"
#include "bgbul.h"

__sfr __at(0x98) PB_DAT;
__sfr __at(0x99) PB_CTL;

/* 差分描き(asm)への受け渡し。非static(asm から名前で参照) */
u8 pb_ox, pb_oy, pb_nx, pb_ny, pb_w, pb_h, pb_oh, pb_oofs, pb_nofs, pb_cnt, pb_rowl, pb_r14, pb_col;

/* 前回の矩形(ox,oy,w×oh)を海へ戻し、新しい矩形(nx,ny,w×h)を pb_col で描く。重なって変わらない行は触らない(BGTEST の実測版)。 */
static void pb_blit(void) __naked {
    __asm
        ld   a, (_pb_ny)
        ld   hl, #_pb_oy
        sub  a, (hl)
        ld   b, (hl)               ; b = 開始行(下へ動くなら旧Y)
        bit  7, a
        jr   z, 09001$
        ld   a, (_pb_ny)          ; 上へ動くなら新Y
        ld   b, a
    09001$:
        ld   a, (hl)
        sub  a, b
        ld   (_pb_oofs), a
        ld   hl, #_pb_oh
        add  a, (hl)
        ld   c, a                  ; c = oofs + oh
        ld   a, (_pb_ny)
        sub  a, b
        ld   (_pb_nofs), a
        ld   hl, #_pb_h
        add  a, (hl)               ; a = nofs + h
        cp   a, c
        jr   nc, 09002$
        ld   a, c
    09002$:
        ld   (_pb_cnt), a
        ld   c, #0                 ; c = k(開始行からの行番号), b = リング行
    09010$:                        ; ── 行ループ
        ld   a, (_pb_oh)
        ld   e, a
        ld   a, (_pb_oofs)
        ld   d, a
        ld   a, c
        sub  a, d
        cp   a, e                  ; carry = 旧矩形の行
        ld   a, #0
        rla
        ld   d, a
        ld   a, (_pb_h)
        ld   e, a
        ld   a, (_pb_nofs)
        ld   l, a
        ld   a, c
        sub  a, l
        cp   a, e                  ; carry = 新矩形の行
        ld   a, d
        rla                        ; a = 旧<<1 | 新
        or   a, a
        jr   z, 09090$
        cp   a, #1
        jr   z, 09020$
        cp   a, #2
        jr   z, 09030$
        ld   a, (_pb_nx)          ; 両方: 列が同じなら変化なし
        ld   hl, #_pb_ox
        cp   a, (hl)
        jr   z, 09090$
        jr   09040$
    09090$:
        inc  b
        inc  c
        ld   a, (_pb_cnt)
        cp   a, c
        jr   nz, 09010$
        ret
    09020$:                        ; 新だけ: 白を w バイト
        ld   a, (_pb_nx)
        ld   e, a
        call 09100$
        ld   a, (_pb_w)
        ld   d, a
        ld   a, (_pb_col)
    09021$:
        out  (0x98), a
        dec  d
        jr   nz, 09021$
        jr   09090$
    09030$:                        ; 旧だけ: 海を w バイト
        ld   a, (_pb_ox)
        ld   e, a
        call 09100$
        call 09110$
        ld   a, (_pb_w)
        ld   d, a
    09031$:
        ld   a, e
        and  a, #15
        ld   l, a
        ld   a, (_pb_rowl)
        or   a, l
        ld   l, a
        ld   a, (hl)
        out  (0x98), a
        inc  e
        dec  d
        jr   nz, 09031$
        jr   09090$
    09040$:                        ; 両方で横に動いた: min(ox,nx) から w+|dx| バイト
        ld   a, (_pb_ox)
        ld   hl, #_pb_nx
        sub  a, (hl)
        jr   nc, 09041$
        neg
        ld   d, a
        ld   a, (_pb_ox)
        jr   09042$
    09041$:
        ld   d, a
        ld   a, (_pb_nx)
    09042$:
        ld   e, a
        ld   a, (_pb_w)
        add  a, d
        ld   d, a
        call 09100$
        call 09110$
    09043$:
        ld   a, (_pb_nx)
        ld   l, a
        ld   a, e
        sub  a, l
        ld   l, a
        ld   a, (_pb_w)
        dec  a
        cp   a, l                  ; (列-nx) <= w-1 なら新矩形の列
        jr   c, 09044$
        ld   a, (_pb_col)
        jr   09045$
    09044$:
        ld   a, e
        and  a, #15
        ld   l, a
        ld   a, (_pb_rowl)
        or   a, l
        ld   l, a
        ld   a, (hl)
    09045$:
        out  (0x98), a
        inc  e
        dec  d
        jr   nz, 09043$
        jp   09090$
    09100$:                        ; 書込みアドレス(b=リング行, e=バイト列)。R#14 は変わるときだけ
        di
        ld   a, b
        rlca
        and  a, #1
        or   a, #2
        push hl
        ld   hl, #_pb_r14
        cp   a, (hl)
        jr   z, 09101$
        ld   (hl), a
        out  (0x99), a
        ld   a, #0x8E
        out  (0x99), a
    09101$:
        pop  hl
        ld   a, b
        and  a, #1
        rrca
        or   a, e
        out  (0x99), a
        ld   a, b
        srl  a
        or   a, #0x40
        out  (0x99), a
        ei
        ret
    09110$:                        ; h/rowl = テンプレート行 (b&15) の先頭
        ld   a, b
        and  a, #15
        ld   l, a
        ld   h, #0
        add  hl, hl
        add  hl, hl
        add  hl, hl
        add  hl, hl
        ld   a, l
        ld   (_pb_rowl), a
        ld   a, h
        add  a, #PB_TMPL_HI     ; 海のひな形(16 行×16B=左 32 ドット。bgbul.h)
        ld   h, a
        ret
    __endasm;
}


/* 分割表(slot14): 衝撃波は出さない(背景弾のオーバレイは枠が無い)。先頭=スプライト表A＋表示起点 / 分割行=表B の2本だけ */
u8 ovl_bgb_split(u8 split_line) {
    g_ras[0].line = 0;          g_ras[0].reg = 5; g_ras[0].val = SPR_R5_A; g_ras[0].reg2 = 23; g_ras[0].val2 = g_vscroll; g_ras[0].pidx = RAS_NOPAL;
    g_ras[1].line = split_line; g_ras[1].reg = 5; g_ras[1].val = SPR_R5_B; g_ras[1].reg2 = RAS_NOREG; g_ras[1].pidx = RAS_NOPAL;
    return 2;
}

void pb_init(void) {
    u8 r, i, *q = (u8 *)PB_TMPL;
    for (r = 0; r < 16; r++) {                     /* 海のひな形(y=512..527)の左 16B を RAM へ(弾を消すとき用) */
        __asm di __endasm;
        PB_CTL = 4; PB_CTL = 0x80 | 14;
        PB_CTL = (u8)((r & 1) << 7); PB_CTL = (u8)(r >> 1);
        __asm ei __endasm;
        for (i = 0; i < 16; i++) *q++ = PB_DAT;
    }
    pb_drop();
}

void pb_drop(void) { u8 i; for (i = 0; i < PB_N; i++) pbv[i].on = 0; }

void pb_add(s16 x, s16 y, s8 vx, s8 vy) {
    u8 i;
    PB *b = pbv;
    for (i = 0; i < PB_N; i++, b++) {
        if (b->on) continue;
        b->on = 1;
        b->qx = (s16)(x << 3); b->qy = (s16)(y << 3);
        b->vx = vx; b->vy = vy;
        pb_nx = pb_ox = b->sx = (u8)(x >> 1); pb_ny = pb_oy = b->ry = (u8)(y + (u8)cam);
        pb_w = 2; pb_h = 4; pb_oh = 0; pb_col = 0xCC;
        pb_r14 = 0xFF;
        pb_blit();
        return;
    }
}
static void pb_erase(PB *b) { pb_w = 2; pb_h = 0; pb_oh = 4; pb_ox = pb_nx = b->sx; pb_oy = pb_ny = b->ry; pb_blit(); b->on = 0; }
void pb_clear(void) { u8 i; for (i = 0; i < PB_N; i++) if (pbv[i].on) pb_erase(&pbv[i]); }
void pb_update(void) {
    u8 i, cl = (u8)cam;
    PB *b = pbv;
    s16 px = (s16)(g_player_x + 8), py = (s16)(g_player_y + 8);
    pb_r14 = 0xFF; pb_col = 0xCC;
    for (i = 0; i < PB_N; i++, b++) {
        s16 x, y, dx, dy;
        if (!b->on) continue;
        b->qx += b->vx; b->qy += b->vy;
        x = b->qx >> 3; y = b->qy >> 3;
        if (x < 4 || x > 248 || y < 8 || y > 204) { pb_erase(b); continue; }
        dx = (s16)(x + 2 - px); dy = (s16)(y + 2 - py);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 5 && dy < 5 && !g_loop_t) { pb_erase(b); ent_player_hit(g_player_x, g_player_y); continue; }
        pb_w = 2; pb_h = 4; pb_oh = 4;
        pb_ox = b->sx; pb_oy = b->ry;
        pb_nx = b->sx = (u8)(x >> 1); pb_ny = b->ry = (u8)(y + cl);
        pb_blit();
    }
}
