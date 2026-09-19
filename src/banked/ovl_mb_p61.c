/* ovl_mb_p61.c — 5面の中ボス P-61 ブラックウィドウ(米の双胴の夜間戦闘機, ROADMAP B4)。中ボス用オーバレイ(OVL12_BANK)で動く。
   ★見せ場: **スプライトの 2 倍拡大(MAG)で 128x128 の大きな中ボス**(ユーザー案)と、**背景に描く弾幕**(実機で 1発 0.13ms と測った差分描き)。
     MAG は全部のスプライトに掛かるので、自機・弾は半分に縮めた絵(絵の表B=0x2000、R#6=0x04)を拡大で元の大きさに見せる。
     スコア・残機(アイコンと数)・ボム残数は数字を半分にすると読めないので、中ボスの間だけ背景に描く(HUD のスプライトは出さない)。
     アイコンとボム棒は HUD のスプライトと同じ絵・同じ色(行ごとの色)で描く(bgspr)。
     戦艦の区間では拡大を切って元へ戻す。
   ★1〜4面との違い: 弾幕をかいくぐる。画面の上に居座って左右に動き、機首を自機へ向けながら渦巻きに弾をばらまく。
     時々、白く明滅(予告)してから扇に11発。背景の弾は同時に PB_N(48)発まで(20 では少ないと指摘)。
   ★中ボス共通の決まり: 被弾=1フレーム白く光る(その後3フレームは光らせない) / 予告=白く明滅 / 手負い=エンジン炎上・弾幕が濃く /
     撃墜=爆発して落ちる / 影は変えない(月明かりの影, 2x2 を拡大で 64x64)。
   ★絵(tools/gen_p61.py, 9方向)は影のバンクの後ろ半分に分けて置いてある(読み込みの添字 p61_req)。
   ★衝撃波はこの中ボスの間は出ない(オーバレイの枠のため。分割表は ovl_p61_split)。
   ★メガクラッシュもこの中ボスの間は使えない(津波自体が拡大を使う＋枠のため。常駐がボタンを受け付けない)。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"       /* aim_dir */
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "sound.h"
#include "aa_hot.h"     /* cam */
#include "scroll.h"     /* g_sea_skip / SC_SEATMPL_Y */
#include "hud.h"        /* hud_colors */
#include "sprites.h"    /* SPR_ZERO */
#include "raster.h"     /* g_ras */
#include "midboss.h"

__sfr __at(0x98) PB_DAT;
__sfr __at(0x99) PB_CTL;

extern u8 rnd(void);

#define P61_HP      240     /* 半分単位(通常弾 120発)。大きくて当てやすいぶん硬く */
#define P61_TIMEOUT 1350
#define P61_WARN_T  12
#define P61_PIERCE  0x7AC0
#define P61_SH_OFF  24      /* 影を右下へ(拡大後のドット) */
#define PB_N        48      /* 背景の弾の最大数(20 では「弾数少ない」と指摘) */
#define PB_TMPL     0xBF00  /* 海のひな形(左 32 ドット×16 行=256B)。オーバレイの後ろ=ovl12 は 7168B 以下(Makefile が検証) */
#define PB_RAM      0xBC00  /* 背景の弾の表(48×9B=432B)。オーバレイの後ろ(0xBC00〜0xBDAF) */
#define RG1SAV      (*(volatile u8 *)0xF3E0)

enum { ST_ENTER, ST_FIGHT, ST_WARN, ST_LEAVE, ST_DIE, ST_DONE };

typedef struct { s16 qx, qy; s8 vx, vy; u8 on, sx, ry; } PB;   /* 位置は 1/8 ドット。sx=バイト列 / ry=リング行 */
#define pbv ((PB *)PB_RAM)

static const u8 p61_req[9] = { 7, 12, 13, 14, 15, 20, 21, 22, 23 };   /* 9方向(32分割の 12..20)の読み込みの添字 */
static const s8 sin64[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125, 122, 117, 112, 106, 98,
    90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122,
    -125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };

/* 背景の弾の差分描き(asm)への受け渡し。非static(asm から名前で参照) */
u8 pb_ox, pb_oy, pb_nx, pb_ny, pb_w, pb_h, pb_oh, pb_oofs, pb_nofs, pb_cnt, pb_rowl, pb_r14, pb_col;

static u8  st, st_t, hurt, fr, flast, flash, fcool, coldirty, sang, ph, hud_sc_dirty;
static u16 t, hp, hud_last, kpts;
static u8  hud_lv, hud_cr;
static s16 cx, cy;

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
        add  a, #0xBF          ; 海のひな形 0xBF00(16 行×16B=左 32 ドット)
        ld   h, a
        ret
    __endasm;
}


/* 分割表(slot14): 衝撃波は出さない(オーバレイの枠のため)。先頭=スプライト表A＋表示起点 / 分割行=表B の2本だけ */
u8 ovl_p61_split(u8 split_line) {
    g_ras[0].line = 0;          g_ras[0].reg = 5; g_ras[0].val = SPR_R5_A; g_ras[0].reg2 = 23; g_ras[0].val2 = g_vscroll; g_ras[0].pidx = RAS_NOPAL;
    g_ras[1].line = split_line; g_ras[1].reg = 5; g_ras[1].val = SPR_R5_B; g_ras[1].reg2 = RAS_NOREG; g_ras[1].pidx = RAS_NOPAL;
    return 2;
}

static s16 bxx(void) { return (s16)(cx - 64); }
static s16 byy(void) { return (s16)(cy - 64); }

void ovl_mb_init(void) {
    u8 i;
    st = ST_ENTER; st_t = 0; t = 0; kpts = 0; hp = P61_HP; hurt = 0; fr = 4; flast = 4; flash = 0; fcool = 0; coldirty = 1; sang = 0; ph = 16;
    cx = 128; cy = -64;
    /* 絵の表A→表B の縮小は常駐が読み込みの前に済ませている(gen_planes.c の mag_table。枠のため ROM 実行) */
    /* 海のひな形(y=512..527)の左 16B を RAM へ(弾を消すとき用) */
    {   u8 r, *q = (u8 *)PB_TMPL;
        for (r = 0; r < 16; r++) {
            __asm di __endasm;
            PB_CTL = 4; PB_CTL = 0x80 | 14;
            PB_CTL = (u8)((r & 1) << 7); PB_CTL = (u8)(r >> 1);
            __asm ei __endasm;
            for (i = 0; i < 16; i++) *q++ = PB_DAT;
        } }
    for (i = 0; i < PB_N; i++) pbv[i].on = 0;
    /* スコア等を描く行を海の波の塗り直しから外す */
    g_sea_skip |= (u16)(1u << (((u8)(cam + 1)) >> 4)) | (u16)(1u << (((u8)(cam + 16)) >> 4))
                | (u16)(1u << (((u8)(cam + 190)) >> 4)) | (u16)(1u << (((u8)(cam + 205)) >> 4));
    hud_last = 0xFFFF; hud_lv = 0xFF; hud_cr = 0xFF; hud_sc_dirty = 0;
    g_mb_n = 22;
    g_mb_req = p61_req[fr]; g_mb_new = 0;
}

/* ---- 背景の HUD(スコア・残機・ボム残数)。海と重ねて 8x8 の字を描く ---- */
static void glyph(u8 x, u8 y, u8 ch, u8 fg) {
    const u8 *g = vdp_glyph(ch);
    u8 r, i, b[4];
    for (r = 0; r < 8; r++) {
        u16 a = (u16)(((u16)(256 + (u8)(cam + y + r)) << 7) + (x >> 1));
        u8 bits = g[r];
        vdp_read_addr(a);
        for (i = 0; i < 4; i++) b[i] = PB_DAT;
        vdp_write_addr(a);
        for (i = 0; i < 4; i++, bits <<= 2) {
            u8 v = b[i];
            if (bits & 0x80) v = (u8)((v & 0x0F) | (fg << 4));
            if (bits & 0x40) v = (u8)((v & 0xF0) | fg);
            PB_DAT = v;
        }
    }
}
static void sea_rows(u8 y, u8 n) {   /* 画面 y から n 行を海へ戻す */
    for (; n; n--, y++) { u8 ry = (u8)(cam + y); vdp_copy(0, (u16)(SC_SEATMPL_Y + (ry & 15)), 0, (u16)(256 + ry), 256, 1); }
}
static void sea_box(u8 x, u8 y, u8 w, u8 n) {   /* 画面 (x,y) から w ドット×n 行だけ海へ戻す(同じ行のほかの字は残す) */
    for (; n; n--, y++) { u8 ry = (u8)(cam + y); vdp_copy(x, (u16)(SC_SEATMPL_Y + (ry & 15)), x, (u16)(256 + ry), w, 1); }
}
/* 16x16 のスプライトの絵(32B)を、海と重ねて背景へ描く。rc=行ごとの色(NULL なら c1 の1色)。x は偶数 */
static void bgspr(u8 x, u8 y, const u8 *pat, const u8 *rc, u8 c1) {
    u8 r, i, b[8];
    for (r = 0; r < 16; r++) {
        u16 a = (u16)(((u16)(256 + (u8)(cam + y + r)) << 7) + (x >> 1));
        u16 bits = (u16)(((u16)pat[r] << 8) | pat[16 + r]);
        u8 fg = rc ? rc[r] : c1;
        if (!bits) continue;
        vdp_read_addr(a);
        for (i = 0; i < 8; i++) b[i] = PB_DAT;
        vdp_write_addr(a);
        for (i = 0; i < 8; i++, bits <<= 2) {
            u8 v = b[i];
            if (bits & 0x8000) v = (u8)((v & 0x0F) | (fg << 4));
            if (bits & 0x4000) v = (u8)((v & 0xF0) | fg);
            PB_DAT = v;
        }
    }
}
/* ★HUD と同じ絵・同じ色(hud.c の crush_pattern / hud_colors の crush_col)。ボム棒: 幅4の棒を6おきに bars 本、行 1..14 */
static const u8 crush_col[16] = { 15,15,15, 12,12,12, 11,11,11,11,11, 12,12,12, 15,15 };
static void crush_bg(u8 bars) {
    u8 pat[32], r, l = 0xF0, rt = 0x00;
    if (bars >= 2) { l |= 0x03; rt |= 0xC0; }
    if (bars >= 3) { rt |= 0x0F; }
    for (r = 0; r < 16; r++) { u8 on = (u8)(r >= 1 && r <= 14); pat[r] = on ? l : 0; pat[16 + r] = on ? rt : 0; }
    bgspr(8, 190, pat, crush_col, 0);
}
static void hud_bg(void) {
    if (!hud_sc_dirty) {                             /* 最初の1回: 残機のアイコン(零戦のシルエット, 緑)。絵は表Aの SPR_ZERO をそのまま */
        u8 pat[32], i;
        hud_sc_dirty = 1;
        vdp_cmd_wait();
        vdp_read_addr((u16)(0x7800 + SPR_ZERO * 8));
        for (i = 0; i < 32; i++) pat[i] = PB_DAT;
        bgspr(212, 1, pat, (const u8 *)0, 3);
    }
    if (g_score != hud_last) {
        u16 v = g_score; u8 i, d[5];
        hud_last = v;
        for (i = 5; i; ) { d[--i] = (u8)(v % 10); v /= 10; }
        sea_box(8, 2, 40, 8);
        vdp_cmd_wait();
        for (i = 0; i < 5; i++) glyph((u8)(8 + i * 8), 2, (u8)('0' + d[i]), 15);
    }
    if (g_lives != hud_lv) { hud_lv = g_lives; sea_box(234, 2, 8, 8); vdp_cmd_wait(); glyph(234, 2, (u8)('0' + (g_lives % 10)), 11); }
    if (g_crush != hud_cr) {                         /* ボム残数(画面下): 1〜3 は棒の本数、4以上は棒1本＋数字(HUD と同じ) */
        hud_cr = g_crush;
        sea_box(8, 190, 32, 16);
        vdp_cmd_wait();
        if (g_crush) crush_bg((u8)(g_crush <= 3 ? g_crush : 1));
        if (g_crush > 3) glyph(28, 192, (u8)('0' + (g_crush % 10)), 15);
    }
}

/* ---- 背景の弾 ---- */
static void pb_spawn(s16 x, s16 y, u8 ang, u8 spd) {
    u8 i;
    PB *b = pbv;
    for (i = 0; i < PB_N; i++, b++) {
        if (b->on) continue;
        b->on = 1;
        b->qx = (s16)(x << 3); b->qy = (s16)(y << 3);
        b->vx = (s8)(((s16)sin64[ang & 63] * spd) >> 7);
        b->vy = (s8)(((s16)sin64[(u8)(ang + 16) & 63] * spd) >> 7);
        pb_nx = pb_ox = b->sx = (u8)(x >> 1); pb_ny = pb_oy = b->ry = (u8)(y + (u8)cam);
        pb_w = 2; pb_h = 4; pb_oh = 0; pb_col = 0xCC;
        pb_r14 = 0xFF;
        pb_blit();
        return;
    }
}
static void pb_erase(PB *b) { pb_w = 2; pb_h = 0; pb_oh = 4; pb_ox = pb_nx = b->sx; pb_oy = pb_ny = b->ry; pb_blit(); b->on = 0; }
static void pb_update(void) {
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
        if (dx < 5 && dy < 5) { pb_erase(b); ent_player_hit(g_player_x, g_player_y); continue; }
        pb_w = 2; pb_h = 4; pb_oh = 4;
        pb_ox = b->sx; pb_oy = b->ry;
        pb_nx = b->sx = (u8)(x >> 1); pb_ny = b->ry = (u8)(y + cl);
        pb_blit();
    }
}

/* ---- 中ボスのスプライト(拡大で 1 マス 32 ドット) ---- */
static void put_sprites(void) {
    u8 c, j = 0, pass, sl, s0 = (u8)(32 - g_mb_n);
    s16 bx = bxx(), by = byy();
    const u8 *col = (const u8 *)(MB_BUF + 708);
    sl = s0;
    for (pass = 0; pass < 3; pass++) {
        u16 m = (pass == 0) ? g_mb_om : (pass == 1) ? g_mb_bm : 0x0660;   /* 影は 2x2(マス 5,6,9,10) */
        for (c = 0; c < 16; c++) {
            s16 x, y;
            u8 o;
            if (!(m & (1u << c))) continue;
            x = (s16)(bx + ((c & 3) << 5) + ((pass == 2) ? P61_SH_OFF : 0));
            y = (s16)(by + ((c >> 2) << 5) + ((pass == 2) ? P61_SH_OFF : 0));
            if (coldirty) {
                if (pass == 2) vdp_sprite_color(sl, 13);
                else if (flash) vdp_sprite_color(sl, 15);
                else vdp_sprite_color_tab(sl, col);
            }
            if (pass < 2) col += 16;
            o = (u8)(x < 0 || x > 255 || y < -32 || y > 212);
            vdp_sprite_pos(sl, o ? 0 : (u8)x, o ? 220 : (u8)y,
                           (pass == 0) ? MB_OV_PAT(j) : (pass == 1) ? MB_CELL_PAT(c) : MB_OV_PAT(j));
            j++; sl++;
        }
        if (pass == 1) j = 2;
    }
    for (; sl < 32; sl++) vdp_sprite_pos(sl, 0, 220, MB_CELL_PAT(0));
    coldirty = 0;
}

static void hit_test(void) {
    u8 i;
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        dx = (s16)(e->x + 8 - cx); if (dx < 0) dx = -dx;
        dy = (s16)(e->y + 8 - cy); if (dy < 0) dy = -dy;
        if (!((dx < 56 && dy < 14) || (dx < 16 && dy < 44))) continue;   /* 主翼 / 胴体と双胴 */
        if (e->ax == (s16)P61_PIERCE) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)P61_PIERCE;
        hp = (hp > e->hp) ? (u16)(hp - e->hp) : 0;
        if (!fcool) { flash = 1; fcool = 4; coldirty = 1; }
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
}

static void finish(void) {
    u8 i;
    for (i = 0; i < PB_N; i++) if (pbv[i].on) pb_erase(&pbv[i]);
    sea_rows(1, 16); sea_rows(190, 16);
    g_sea_skip = 0;
    vdp_wreg(1, (u8)(RG1SAV & 0xFE));               /* 拡大を切る */
    vdp_wreg(6, 0x0F);                              /* 絵の表Aへ */
    ent_spr_cache_inval(0);
    hud_colors();
    if (kpts) scorepop_add((s16)(cx - 8), (s16)(cy - 8), kpts);   /* ★撃墜の点数: 拡大中に出すと数字が2倍に見えた(実機で指摘) */
    mb_finish();
}

void ovl_mb_frame(void) {
    u8 tgt = fr;
    if (st == ST_DONE) return;
    t++;
    if (fcool) fcool--;
    if (flash && !--flash) coldirty = 1;
    vdp_wreg(6, 0x04);                              /* 絵の表B(半分の絵)と拡大 */
    vdp_wreg(1, (u8)(RG1SAV | 0x01));
    vdp_cmd_wait();
    switch (st) {
    case ST_ENTER:                                  /* 上から降りてくる */
        cy += 2;
        if (cy >= 60) { st = ST_FIGHT; st_t = 0; }
        break;
    case ST_FIGHT:
        if ((++st_t & (hurt ? 1 : 3)) == 0) {       /* 機首から2本の腕の渦巻き */
            pb_spawn(cx - 2, cy + 44, sang, 10); pb_spawn(cx - 2, cy + 44, (u8)(sang + 32), 10); sang += 5; }
        if (st_t >= 150) { st = ST_WARN; st_t = 0; }
        if (t >= P61_TIMEOUT) st = ST_LEAVE;
        hit_test();
        break;
    case ST_WARN:                                   /* 予告: 白く明滅 → 自機へ扇 */
        if ((++st_t & 3) == 1) { flash = 1; coldirty = 1; }
        if (st_t >= P61_WARN_T) {
            u8 a = (u8)(aim_dir((s16)(cx - 8), (s16)(cy + 36), g_player_x, g_player_y) << 1), k;
            for (k = 0; k < 11; k++) pb_spawn(cx - 2, cy + 44, (u8)(a - 15 + k * 3), hurt ? 16 : 13);
            sfx(2, SFX_BOOM);
            st = ST_FIGHT; st_t = 0;
        }
        hit_test();
        break;
    case ST_LEAVE:
        cy -= 2;
        if (cy < -70) st = ST_DONE;
        break;
    case ST_DIE:                                    /* 燃えながら落ちる */
        cy++;
        if ((t & 3) == 0) { ent_spawn_explosion((s16)(cx - 48 + (rnd() & 95)), (s16)(cy - 24 + (rnd() & 47))); if ((t & 7) == 0) sfx(2, SFX_BOOM); }
        if (++st_t >= 60) st = ST_DONE;
        break;
    }
    if (st == ST_DONE) { finish(); return; }
    if (st == ST_FIGHT || st == ST_WARN) {          /* 左右へゆっくり動き、機首を自機へ向ける */
        u8 d;
        ph++;
        cx = (s16)(128 + (((s16)sin64[(ph >> 2) & 63] * 64) >> 7));
        d = aim_dir(cx, cy, (s16)(g_player_x + 8), (s16)(g_player_y + 8));
        tgt = (u8)((d < 12) ? 0 : (d > 20) ? 8 : d - 12);
        if (hp && hp < P61_HP / 2) {                /* 手負い: エンジンが燃え、弾幕が濃く・速く */
            if (!hurt) { hurt = 1; g_shake = 8; sfx(2, SFX_BOOM); }
            if ((t & 7) == 0) ent_spawn_explosion((s16)(cx - 30 + ((t & 8) ? 44 : 0)), (s16)(cy - 8));
        }
        if (!hp) {                                  /* 撃墜 */
            u16 pts = (u16)(500 + (P61_TIMEOUT - t) / 3);
            st = ST_DIE; st_t = 0;
            g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
            kpts = pts;                             /* 点数の表示は拡大を切ってから(finish) */
            if (g_crush < CRUSH_MAX) g_crush++;
            g_hitstop = 4; g_shake = 8;
            shock_at(cy);
            sfx(2, SFX_BOOM);
        }
    }
    if (fr != tgt && (t & 3) == 0) fr = (u8)((fr < tgt) ? fr + 1 : fr - 1);
    if (fr != flast && g_mb_req == 0xFF && !g_mb_new) { flast = fr; g_mb_req = p61_req[fr]; }
    if (g_mb_new) {                                 /* 絵の表B へ(本体・重ね・影をまとめて) */
        g_mb_pat_off = (u16)(MB_PATB - 0x7800);
        mb_upload(448, 0);
        g_mb_n = 22;
        coldirty = 1;
    }
    pb_update();
    hud_bg();
    put_sprites();
}
