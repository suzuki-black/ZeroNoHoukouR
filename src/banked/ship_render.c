/* banked/ship_render.c — 艦の重い事前描画レンダラ「本体」(冷たいバンク SHIP_RENDER_BANK)。
   常駐 ship_render ラッパが g_shipargs を埋めて bcall → ここの banked_entry が描画する。
   バッファB(=SC_SHIPBUF_Y)へ上面視の艦を一度だけ描く。艦高496px。中心x=128。
   ★このバンク内からは data窓を差替えない(vdp_copy は常駐/窓非差替なので呼んでよい)。
   対空砲座標表(aag_*)と g_shipargs は常駐(ship_aag.c)にあり extern 参照。 */
#include "ship.h"
#include "scroll.h"  /* SC_SHIPBUF_Y / SC_SEATMPL_Y / SC_SHIP_ROWS / scroll_to */
#include "vdp.h"     /* vdp_copy(常駐。海テンプレのタイル) */
#include "entity.h"  /* 沈没演出: ent_reset/ent_update_all/ent_draw_all/ent_spawn_explosion(常駐) */
#include "sound.h"   /* sfx/bgm_stop/play_sink/SFX_* */
#include "player.h"  /* g_player_x/y */
#include "input.h"   /* input_poll/g_input_edge/INP_* */
#include "gamestate.h" /* g_score/g_hiscore/g_continue */
#include "sprites.h"  /* SPR_* / vdp_sprite_pattern(mode4=パターン投入を移設) */

__sfr __at(0x98) SH_DAT;     /* VRAM データ */
__sfr __at(0x99) SH_CTRL;    /* VDP アドレス/レジスタ */
__sfr __at(0x9B) SH_IDAT;    /* R#17 間接オートインクリメント */

#define B         SC_SHIPBUF_Y   /* 528 */
#define SHADOWC   7              /* 船体ドロップシャドウ(暗青) */
#define NAAG      SHIP_NAAG

static u8 g_hull;   /* hull_w プロファイル選択 */

/* ---- 高速VDPコマンド完了待ち(S#2 タイトポール) ---- */
static void swait(void) {
    __asm
        di
        ld   a, #2
        out  (0x99), a
        ld   a, #0x8F
        out  (0x99), a
    00011$:
        in   a, (0x99)
        rra
        jr   c, 00011$
        ld   a, #0
        out  (0x99), a
        ld   a, #0x8F
        out  (0x99), a
        ei
    __endasm;
}

/* ---- 高速矩形塗り(LMMV, R#17間接)。dy は絶対VRAM Y。 ---- */
static void sfill(u16 dx, u16 dy, u16 nx, u16 ny, u8 c) {
    swait();
    __asm di __endasm;
    SH_CTRL = 36; SH_CTRL = 0x80 | 17;
    SH_IDAT = (u8)(dx & 0xFF); SH_IDAT = (u8)(dx >> 8);
    SH_IDAT = (u8)(dy & 0xFF); SH_IDAT = (u8)(dy >> 8);
    SH_IDAT = (u8)(nx & 0xFF); SH_IDAT = (u8)(nx >> 8);
    SH_IDAT = (u8)(ny & 0xFF); SH_IDAT = (u8)(ny >> 8);
    SH_IDAT = c; SH_IDAT = 0; SH_IDAT = 0x80;
    __asm ei __endasm;
}

/* ---- 直接1画素(read-modify-write)。dy=絶対VRAM Y(17bit)。 ---- */
static void vpset(u16 dy, u16 x, u8 c) {
    u8 r14  = (u8)((dy >> 7) & 7);
    u8 alo  = (u8)(((dy & 1) << 7) | (x >> 1));
    u8 amid = (u8)((dy >> 1) & 0x3F);
    u8 b;
    __asm di __endasm;
    SH_CTRL = r14;  SH_CTRL = 0x80 | 14;
    SH_CTRL = alo;  SH_CTRL = amid;
    b = SH_DAT;
    if (x & 1) b = (u8)((b & 0xF0) | c); else b = (u8)((b & 0x0F) | (u8)(c << 4));
    SH_CTRL = r14;  SH_CTRL = 0x80 | 14;
    SH_CTRL = alo;  SH_CTRL = (u8)(amid | 0x40);
    SH_DAT = b;
    __asm ei __endasm;
}
static void spset(u16 x, u16 dy, u8 c) { vpset(dy, x, c); }

/* ---- 疑似乱数(旧版と同一LCG。呼び順も一致=斑点再現)。初期化は ship_render_impl 冒頭で行う
   (宣言時 =12345 の初期化子は .ihx にデータレコードを生む=バンク配置不可なので付けない)。 ---- */
static u16 rng;
static u8 rnd(void) { rng = (u16)(rng * 25173 + 13849); return (u8)(rng >> 8); }

/* ---- 整数sqrt ---- */
static u8 isqrt(u16 n) { u8 r = 0; u16 sq = 1; while (sq <= n) { r++; sq += (u16)(2 * r + 1); } return r; }

/* ---- 塗り円(cy はship-local) ---- */
static void disk(s16 cx, s16 cy, s16 rr, u8 c) {
    s16 dy, w;
    for (dy = -rr; dy <= rr; dy++) {
        w = (s16)isqrt((u16)(rr * rr - dy * dy)) * 2;
        if (w > 0) sfill((u16)(cx - w / 2), (u16)(B + cy + dy), (u16)w, 1, c);
    }
}

/* ---- 陰影ドーム(左上光源) ---- */
static void dome(s16 cx, s16 cy, s16 rr) {
    s16 t;
    disk(cx, cy, rr, 13);
    disk(cx, cy, rr - 1, 5);
    disk(cx - 1, cy - 1, rr - 2, 4);
    disk(cx - 1, cy - 1, rr - 4, 14);
    t = rr - 6; if (t < 1) t = 1;
    disk(cx - 2, cy - 2, t, 15);
}

static void mainGun(s16 cx, s16 cy, s16 rr) {
    disk(cx, cy, rr + 2, 13);
    disk(cx, cy, rr + 1, 5);
    dome(cx, cy, rr);
}
static void aaGun(s16 x, s16 cy, s16 rr) { disk(x, cy, rr + 1, 13); dome(x, cy, rr); }
static void ground(s16 cx, s16 cy, s16 rr) { disk(cx + 4, cy + 6, rr, 13); }

/* ---- 円筒陰影の砲身 ---- */
static void barrel(s16 cx, s16 y, s16 L, s16 w) {
    s16 i, dd, t; u8 c;
    for (i = 0; i < w; i++) {
        dd = i - (w - 1) / 2; if (dd < 0) dd = -dd;
        t = (s16)(dd * 100 / ((w + 1) / 2));
        c = (t < 30) ? 15 : (t < 60) ? 14 : (t < 85) ? 4 : 5;
        sfill((u16)(cx - w / 2 + i), (u16)(B + y), 1, (u16)L, c);
    }
}

/* ---- 金属斑点 ---- */
static void metalNoise(s16 x, s16 y, s16 w, s16 h) {
    s16 nx, ny; u8 r;
    for (ny = y; ny < y + h; ny += 2)
        for (nx = x; nx < x + w; nx += 2) { r = rnd(); if (r < 55) spset((u16)nx, (u16)(B + ny), (u8)((r & 1) ? 4 : 13)); }
}

/* ---- 3D金属ボックス ---- */
static void deckBox(s16 x, s16 y, s16 w, s16 h, u8 fill) {
    sfill((u16)(x + 3), (u16)(B + y + 4), (u16)w, (u16)h, 13);
    sfill((u16)x, (u16)(B + y), (u16)w, (u16)h, fill);
    metalNoise(x + 1, y + 1, w - 2, h - 2);
    sfill((u16)x, (u16)(B + y), (u16)w, 1, 15);
    sfill((u16)x, (u16)(B + y), 1, (u16)h, 14);
    sfill((u16)x, (u16)(B + y + h - 1), (u16)w, 1, 13);
    sfill((u16)(x + w - 1), (u16)(B + y), 1, (u16)h, 13);
}

/* ---- 船体半幅プロファイル ---- */
static s16 hull_w(s16 y) {
    if (g_hull == 1) {
        if (y < 44)  return (s16)(5 + (41 * y) / 44);
        if (y < 436) return 46;
        if (y < 474) return (s16)(46 - (16 * (y - 436)) / 38);
        return (s16)(30 - (5 * (y - 474)) / 22);
    }
    if (g_hull == 2) {
        if (y < 28)  return (s16)(6 + (18 * y) / 28);
        if (y < 440) return 24;
        if (y < 476) return (s16)(24 - (10 * (y - 440)) / 36);
        return (s16)(14 - (3 * (y - 476)) / 20);
    }
    if (g_hull == HULL_IOWA) {
        if (y < 50)  return (s16)(5 + (45 * y) / 50);
        if (y < 448) return 50;
        if (y < 486) return (s16)(50 - (20 * (y - 448)) / 38);
        return (s16)(30 - (4 * (y - 486)) / 9);
    }
    if (y < 36)  return (s16)(9 + (45 * y) / 36);
    if (y < 400) return 54;
    if (y < 462) return (s16)(54 - (21 * (y - 400)) / 62);
    return (s16)(33 - (3 * (y - 462)) / 34);
}

/* ---- 空母の飛行甲板プロファイル ---- */
static s16 carrier_w(s16 y) {
    if (y < 28)  return (s16)(20 + (38 * y) / 28);
    if (y < 452) return 58;
    if (y < 490) return (s16)(58 - (26 * (y - 452)) / 38);
    return 32;
}

/* ---- 船体本体 ---- */
static void paint_hull_at(s16 cx) {
    s16 y, y2, h, w, i, ys, ws, nx, ny; u8 r;
    y = 0;
    while (y < 496) {
        ys = y - 10; ws = (ys < 0) ? -1 : hull_w(ys);
        y2 = y + 1;
        while (y2 < 496) { s16 w2 = ((y2 - 10) < 0) ? -1 : hull_w(y2 - 10); if (w2 != ws) break; y2++; }
        if (ws >= 0) sfill((u16)(cx - ws + 14), (u16)(B + y), (u16)(ws * 2), (u16)(y2 - y), SHADOWC);
        y = y2;
    }
    y = 0;
    while (y < 496) {
        w = hull_w(y);
        y2 = y + 1;
        while (y2 < 496 && hull_w(y2) == w) y2++;
        h = y2 - y;
        sfill((u16)(cx - w), (u16)(B + y), (u16)(w * 2), (u16)h, 6);
        sfill((u16)(cx - w), (u16)(B + y), 1, (u16)h, 14);
        sfill((u16)(cx - w + 1), (u16)(B + y), 2, (u16)h, 12);
        sfill((u16)(cx + w - 3), (u16)(B + y), 2, (u16)h, 9);
        sfill((u16)(cx + w - 1), (u16)(B + y), 1, (u16)h, 13);
        y = y2;
    }
    for (i = -8; i <= 8; i++) {
        w = i * 6; if (w < 0) w = -w;
        ys = -1; ws = 0;
        for (y = 6; y < 490; y++) if (hull_w(y) - 3 > w) { if (ys < 0) ys = y; ws = y; }
        if (ys >= 0) sfill((u16)(cx + i * 6), (u16)(B + ys), 1, (u16)(ws - ys + 1), 9);
    }
    for (ny = 8; ny < 488; ny += 2) {
        w = hull_w(ny);
        if (w < 14) continue;
        for (nx = (s16)(cx - w + 4); nx < (s16)(cx + w - 4); nx += 2) {
            r = rnd();
            if (r < 80) spset((u16)nx, (u16)(B + ny), (u8)((r & 1) ? 12 : 9));
        }
    }
    for (y = 44; y < 466; y += 5) {
        ws = hull_w(y);
        sfill((u16)(cx - ws + 3), (u16)(B + y), 2, 1, 13);
        sfill((u16)(cx + ws - 5), (u16)(B + y), 2, 1, 13);
    }
}

/* ---- 波切り艦首シェブロン ---- */
static void draw_bow(s16 cx, u8 cnt, u16 yb) {
    u8 i;
    for (i = 0; i <= cnt; i++) {
        sfill((u16)(cx - i), (u16)(B + yb + i / 2), 1, 2, 13);
        sfill((u16)(cx + i), (u16)(B + yb + i / 2), 1, 2, 13);
    }
}

/* ---- 対空砲23基(座標表は常駐 ship_aag.c を extern 参照) ---- */

static void draw_aag(u8 tbl, u8 gb, u8 gs, u8 ab, u8 as) {
    const u8 *ax; const u16 *ay; u8 i;
    switch (tbl) {
        case 1:  ax = aag_x_cv; ay = aag_y_cv; break;
        case 2:  ax = aag_x_hd; ay = aag_y_hd; break;
        case 3:  ax = aag_x_nl; ay = aag_y_nl; break;
        default: ax = aag_x_bb; ay = aag_y_bb; break;
    }
    for (i = 0; i < NAAG; i++) {
        ground((s16)ax[i], (s16)ay[i], (i < 14) ? gb : gs);
        aaGun ((s16)ax[i], (s16)ay[i], (i < 14) ? ab : as);
    }
}

/* ---- 艦OPS(7B/レコード)を解釈 ---- */
static void run_ship_ops(const u8 *d) {
    for (;;) {
        u8 op = d[0], p1, p2, p3; s16 x; u16 y;
        if (op == SOP_END) break;
        x = (s16)d[1]; y = (u16)(d[2] | (d[3] << 8)); p1 = d[4]; p2 = d[5]; p3 = d[6]; d += 7;
        switch (op) {
            case SOP_GROUND:  ground(x, (s16)y, p1);                 break;
            case SOP_MAINGUN: mainGun(x, (s16)y, p1);                break;   /* ドーム土台のみ(砲身は回転スプライト) */
            case SOP_DOME:    dome(x, (s16)y, p1);                   break;
            case SOP_DISK:    disk(x, (s16)y, p1, p2);               break;
            case SOP_DECKBOX: deckBox(x, (s16)y, p1, p2, p3);        break;
            case SOP_AAGUN:   aaGun(x, (s16)y, p1);                  break;
            case SOP_LMMV:    sfill((u16)x, (u16)(B + y), p1, p2, p3); break;
            case SOP_BARREL:  barrel(x, (s16)y, p1, p2);             break;
        }
    }
}

/* ---- 空母の飛行甲板 ---- */
static void carrier_deck(void) {
    s16 y, y2, w, i, nx, ny; u16 h; u8 r;
    y = 4;
    while (y < 494) {
        w = carrier_w(y - 4);
        y2 = y + 1; while (y2 < 494 && carrier_w(y2 - 4) == w) y2++;
        sfill((u16)(128 - w - 2), (u16)(B + y), (u16)((w + 2) * 2), (u16)(y2 - y), SHADOWC);
        y = y2;
    }
    y = 0;
    while (y < 496) {
        w = carrier_w(y);
        y2 = y + 1; while (y2 < 496 && carrier_w(y2) == w) y2++;
        h = (u16)(y2 - y);
        sfill((u16)(128 - w), (u16)(B + y), (u16)(w * 2), h, 6);
        sfill((u16)(128 - w), (u16)(B + y), 1, h, 14);
        sfill((u16)(128 - w + 1), (u16)(B + y), 1, h, 15);
        sfill((u16)(128 + w - 1), (u16)(B + y), 1, h, 13);
        sfill((u16)(128 + w - 2), (u16)(B + y), 1, h, 9);
        y = y2;
    }
    for (i = -3; i <= 3; i++) if (i) sfill((u16)(128 + i * 15), (u16)(B + 28), 1, 424, 9);
    for (ny = 8; ny < 488; ny += 2) {
        w = carrier_w(ny); if (w < 14) continue;
        for (nx = (s16)(128 - w + 3); nx < (s16)(128 + w - 3); nx += 2)
            { r = rnd(); if (r < 70) spset((u16)nx, (u16)(B + ny), (u8)((r & 1) ? 9 : 14)); }
    }
    for (y = 34; y < 452; y += 14) sfill(127, (u16)(B + y), 2, 8, 15);
}

/* ---- 描画本体(旧 ship_render)。banked_entry から g_shipargs 経由で呼ぶ。 ---- */
static void ship_render_impl(u8 kind, u8 hull, u8 bow_cnt, u16 bow_yb, u8 aag_tbl,
                             const u8 *aagp, const u8 *ops, const u8 *ops2) {
    u8 r;
    g_hull = hull;
    rng = 12345;
    for (r = 0; r < SC_SHIP_ROWS; r++)
        vdp_copy(0, SC_SEATMPL_Y, 0, (u16)(B + (u16)r * 16), 256, 16);
    if (kind == 2) {
        carrier_deck();
        run_ship_ops(ops);
        metalNoise(140, 150, 36, 42);
        run_ship_ops(ops2);
        /* 甲板の停泊F6F は scene 側でエンティティ(ET_PARKED)として配置=破壊/発艦できる(BGベイクは廃止) */
    } else if (kind == 1) {
        paint_hull_at(76);  draw_bow(76, bow_cnt, bow_yb);
        paint_hull_at(180); draw_bow(180, bow_cnt, bow_yb);
        run_ship_ops(ops);
    } else {
        paint_hull_at(128); draw_bow(128, bow_cnt, bow_yb);
        run_ship_ops(ops);
    }
    draw_aag(aag_tbl, aagp[0], aagp[1], aagp[2], aagp[3]);
}

/* ---- バンク単一エントリ。常駐 ship_render ラッパが埋めた g_shipargs を読んで描画。 ---- */
/* 開始カード艦画像(g_card_ram, 64x48)を 縦横2倍(128x96)して page0 中央窓へ展開。
   常駐 vdp_write_addr/vdp_data(窓非差替)を使う=バンク内で data窓を触らない。 */
static u8 card_outrow[64];
static void draw_card_impl(void) {
    u8 r, sb, sv, b;
    for (r = 0; r < 48; r++) {
        const u8 *src = &g_card_ram[(u16)r * 32];
        for (sb = 0; sb < 32; sb++) {
            u8 sbyte = src[sb], a = (u8)(sbyte >> 4), lo = (u8)(sbyte & 0x0F);
            card_outrow[(u16)sb * 2]     = (u8)((a << 4) | a);
            card_outrow[(u16)sb * 2 + 1] = (u8)((lo << 4) | lo);
        }
        for (sv = 0; sv < 2; sv++) {
            vdp_write_addr((u16)((u16)(70 + (u16)r * 2 + sv) * 128 + 32));   /* 窓 x64,y70(128x96) */
            for (b = 0; b < 64; b++) vdp_data(card_outrow[b]);
        }
    }
}

/* ---- 沈没演出(旧 play_death_anim)。cam=表示維持カメラ。自機位置は g_player_x/y。 ---- */
static void death_impl(u16 cam) {
    u8 t; s16 px = (s16)g_player_x, py = (s16)g_player_y;
    /* ★_bcall は banked 実行中ずっと di。しかし本関数は vdp_wait_frame(JIFFYを割込みで更新)で
       毎フレーム待つため、di のままだと JIFFY が進まず無限ループ=フリーズする。
       BGM再生ISRは曲データをRAM(bgm_ram)から読み 0xA000窓に触れないので、ここで ei しても
       窓(bank16)は壊れない。よって沈没演出/GO画面だけ割込みを許可する。 */
    __asm ei __endasm;
    bgm_stop();
    ent_reset();                        /* 自機/弾/敵を全消し(死んだ機を火球に置換) */
    sfx(2, SFX_BOOM);
    for (t = 0; t < 84; t++) {          /* ~1.4s */
        scroll_to(cam);
        vdp_set_vscroll((u8)((s16)cam + (s16)(rnd() % 9) - 4));   /* 沈没の揺れ: 縦±4px */
        if ((t & 7) == 0) ent_spawn_explosion(px + (s16)(rnd() % 14) - 7, py + (s16)(rnd() % 14) - 7);
        if ((t % 24) == 0) sfx(2, SFX_BOOM);
        ent_update_all();
        ent_draw_all();
        vdp_wait_frame();
    }
}

/* ---- ゲームオーバー画面(旧 game_over_screen)。戻り 1=CONTINUE / 0=TITLE を g_shipargs.ret へ。 ---- */
static char go_score[6];
static void go_fmt(u16 v) {
    u8 i; for (i = 5; i > 0; i--) { go_score[i - 1] = (char)('0' + (v % 10)); v /= 10; } go_score[5] = 0;
}
static u8 gameover_impl(void) {
    u8 sel = 0; s8 prev = -1;
    __asm ei __endasm;   /* ★同上: 入力待ち/フレーム待ちに割込みが要る(di のままだとフリーズ) */
    play_sink();
    vdp_set_vscroll(0); vdp_set_hscroll(0, 0);   /* ★縦横ともスクロール解除(蛇行weaveXの横ズレが残ると画面全体が右に寄る) */
    vdp_sprite_hide_from(0); vdp_set_display_page(0);
    vdp_fill(0, 0, 256, 212, 0);
    vdp_text_s(56, 44, 11, 0, 2, "GAME OVER");
    if (g_score > g_hiscore) g_hiscore = g_score;
    go_fmt(g_score); vdp_text(84, 96, 15, 0, "SCORE"); vdp_text(132, 96, 11, 0, go_score);
    go_fmt(g_hiscore); vdp_text(84, 116, 14, 0, "HI"); vdp_text(132, 116, 14, 0, go_score);
    if (!g_continue) {
        vdp_text(88, 160, 14, 0, "PUSH SPACE");
        for (;;) { input_poll(); if (g_input_edge & INP_TRIG) break; vdp_wait_frame(); }
        return 0;
    }
    for (;;) {
        input_poll();
        if ((g_input_edge & INP_UP) && sel)    sel = 0;
        if ((g_input_edge & INP_DOWN) && !sel) sel = 1;
        if (sel != (u8)prev) {
            vdp_text(96, 150, sel == 0 ? 11 : 4, 0, "CONTINUE");
            vdp_text(96, 170, sel == 1 ? 11 : 4, 0, "TITLE   ");
            prev = (s8)sel;
        }
        if (g_input_edge & INP_TRIG) break;
        vdp_wait_frame();
    }
    return (sel == 0) ? 1 : 0;
}

/* ===== スプライトのビットマップ(mode2 16x16, 前半16B=左列/後半16B=右列) =====
   ★常駐節約のため sprites.c から移設(面開始で1回VRAMへ流すだけの cold data)。
     hot な色表(zcol/barrel_col/barrel_flash)は常駐(sprites.c)のまま。 */
static const u8 pat_block[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
static const u8 pat_bullet[32] = {
    0x00,0x00,0x00,0x00,0x00,0x07,0x07,0x07,0x07,0x07,0x07,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0x00,0x00,0x00,0x00,0x00
};
static const u8 pat_turret[32] = {
    0x0C,0x0C,0x0C,0x0C,0x0C, 0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F, 0x00,
    0x30,0x30,0x30,0x30,0x30, 0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC, 0x00
};
/* ★空戦戦闘機(bf109/corsair/spitfire/fw190)の手描きパターンは 8方向手続き生成へ移行したため削除。
   停泊機(空母)の hellcat のみ手描きを残す。 */
static const u8 pat_hellcat[32] = {
    0x00,0x00,0x0F,0x07,0x03,0x03,0x3F,0xFF,0xFF,0x03,0x03,0x03,0x07,0x03,0x01,0x00,
    0x00,0x00,0xF0,0xE0,0xC0,0xC0,0xFC,0xFF,0xFF,0xC0,0xC0,0xC0,0xE0,0xC0,0x80,0x00
};
static const u8 pat_zero_a[32] = {  /* コマA(プロペラ細) */
    0x02,0x02,0x03,0x01,0x01,0x03,0x3F,0x7F,0x3F,0x0F,0x01,0x01,0x01,0x0F,0x01,0x01,
    0x40,0x40,0xC0,0x80,0x80,0xC0,0xFC,0xFE,0xFC,0xF0,0x80,0x80,0x80,0xF0,0x80,0x80
};
static const u8 pat_zero_b[32] = {  /* コマB(プロペラ太=ブラー) */
    0x0F,0x0F,0x03,0x01,0x01,0x03,0x3F,0x7F,0x3F,0x0F,0x01,0x01,0x01,0x0F,0x01,0x01,
    0xF0,0xF0,0xC0,0x80,0x80,0xC0,0xFC,0xFE,0xFC,0xF0,0x80,0x80,0x80,0xF0,0x80,0x80
};
static const u8 pat_pbullet[32] = {
    0x00,0x00,0x00,0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x00,0x00,0x00,0x00
};
static const u8 pat_ebshell[32] = {
    0x00,0x00,0x01,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x01,0x00,0x00,
    0x00,0x00,0x80,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0x80,0x00,0x00
};
static const u8 pat_exp0[32] = {
    0x00,0x00,0x00,0x00,0x00,0x03,0x07,0x07,0x07,0x07,0x03,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xC0,0xE0,0xE0,0xE0,0xE0,0xC0,0x00,0x00,0x00,0x00,0x00
};
static const u8 pat_exp1[32] = {
    0x00,0x01,0x01,0x03,0x0F,0x0F,0x1F,0x7F,0x7F,0x1F,0x0F,0x0F,0x03,0x01,0x01,0x00,
    0x00,0x80,0x80,0xC0,0xF0,0xF0,0xF8,0xFE,0xFE,0xF8,0xF0,0xF0,0xC0,0x80,0x80,0x00
};
static const u8 pat_exp2[32] = {
    0x00,0x0F,0x3F,0x3F,0x7F,0x7C,0x7A,0x79,0x79,0x7A,0x7C,0x7F,0x3F,0x3F,0x0F,0x00,
    0x00,0xF0,0xFC,0xFC,0xFE,0x3E,0x5E,0x9E,0x9E,0x5E,0x3E,0xFE,0xFC,0xFC,0xF0,0x00
};
static const u8 pat_exp3[32] = {
    0x00,0x01,0x30,0x30,0x00,0x00,0x00,0x40,0x40,0x00,0x00,0x00,0x30,0x30,0x01,0x00,
    0x00,0x80,0x0C,0x0C,0x00,0x00,0x00,0x02,0x02,0x00,0x00,0x00,0x0C,0x0C,0x80,0x00
};
static const u8 pat_flash[32] = {
    0x00,0x00,0x00,0x00,0x01,0x01,0x03,0x1F,0x1F,0x03,0x01,0x01,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x80,0x80,0xC0,0xF8,0xF8,0xC0,0x80,0x80,0x00,0x00,0x00,0x00
};
static const u8 pat_barrel0[32] = {
    0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
    0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0
};
static const u8 pat_barrel1[32] = {
    0x00,0x00,0x00,0x00,0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF,0xFF,0xFE,0xFC,0xF8,
    0x1F,0x3F,0x7F,0xFF,0xFF,0xFE,0xFC,0xF8,0xF0,0xE0,0xC0,0x80,0x00,0x00,0x00,0x00
};
static const u8 pat_barrel2[32] = {
    0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00
};
static const u8 pat_barrel3[32] = {
    0xF8,0xFC,0xFE,0xFF,0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0xFF,0x7F,0x3F,0x1F
};
static const u8 pat_barrel4[32] = {
    0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,0x07,
    0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0
};
static const u8 pat_barrel5[32] = {
    0x00,0x00,0x00,0x00,0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0xFF,0xFF,0xFE,0xFC,0xF8,
    0x1F,0x3F,0x7F,0xFF,0xFF,0xFE,0xFC,0xF8,0xF0,0xE0,0xC0,0x80,0x00,0x00,0x00,0x00
};
static const u8 pat_barrel6[32] = {
    0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00
};
static const u8 pat_barrel7[32] = {
    0xF8,0xFC,0xFE,0xFF,0xFF,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x80,0xC0,0xE0,0xF0,0xF8,0xFC,0xFE,0xFF,0xFF,0x7F,0x3F,0x1F
};
/* パターン投入本体(常駐 sprites_load ラッパが mode4 で bcall)。VRAM書込 vdp_sprite_pattern は常駐。
   ★戦闘機8方向の手続き生成(load_planes)は常駐(sprites.c)へ置き、sprites_load が bcall 後に呼ぶ。 */
static void load_sprites_impl(void) {
    vdp_sprite_pattern(SPR_BLOCK,   pat_block);
    vdp_sprite_pattern(SPR_BULLET,  pat_bullet);
    vdp_sprite_pattern(SPR_TURRET,  pat_turret);
    vdp_sprite_pattern(SPR_HELLCAT,  pat_hellcat);   /* ★停泊機(空母)は今まで通り手描きF6F。空戦戦闘機は8方向生成へ移行 */
    vdp_sprite_pattern(SPR_ZERO,     pat_zero_a);
    vdp_sprite_pattern(SPR_ZERO2,    pat_zero_b);
    vdp_sprite_pattern(SPR_PBULLET,  pat_pbullet);
    vdp_sprite_pattern(SPR_EBSHELL,  pat_ebshell);
    vdp_sprite_pattern(SPR_EXP0,     pat_exp0);
    vdp_sprite_pattern(SPR_EXP1,     pat_exp1);
    vdp_sprite_pattern(SPR_EXP2,     pat_exp2);
    vdp_sprite_pattern(SPR_EXP3,     pat_exp3);
    vdp_sprite_pattern(SPR_FLASH,    pat_flash);
    vdp_sprite_pattern(SPR_BARREL0,      pat_barrel0);
    vdp_sprite_pattern(SPR_BARREL0 + 4,  pat_barrel1);
    vdp_sprite_pattern(SPR_BARREL0 + 8,  pat_barrel2);
    vdp_sprite_pattern(SPR_BARREL0 + 12, pat_barrel3);
    vdp_sprite_pattern(SPR_BARREL0 + 16, pat_barrel4);
    vdp_sprite_pattern(SPR_BARREL0 + 20, pat_barrel5);
    vdp_sprite_pattern(SPR_BARREL0 + 24, pat_barrel6);
    vdp_sprite_pattern(SPR_BARREL0 + 28, pat_barrel7);
}

void banked_entry(void) {
    switch (g_shipargs.mode) {
        case 1: draw_card_impl(); return;
        case 2: death_impl(g_shipargs.cam); return;
        case 3: g_shipargs.ret = gameover_impl(); return;
        case 4: load_sprites_impl(); return;   /* ★スプライトパターン投入(sprites.cから移設) */
        default:
            ship_render_impl(g_shipargs.kind, g_shipargs.hull, g_shipargs.bow_cnt, g_shipargs.bow_yb,
                             g_shipargs.aag_tbl, g_shipargs.aagp, g_shipargs.ops, g_shipargs.ops2);
    }
}
