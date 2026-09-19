/* gen_planes.c — 敵戦闘機/艦載機の8方向スプライトを手続き生成する冷たいコード(bank19)。
   ★元は常駐(sprites.c)に居たが、面開始で1回しか走らないのに約900B 常駐窓を食っていた。
     演出(ラスタ分割/CPU弾幕/パレットエンジン)を常時オンにしたところ DEBUG_PROF ビルドが
     常駐24KB を超過したため、documented な「常駐リクレイム」(性能と高速化 §3-C)でここへ追い出した。
   ★bank16(ship_render)へ相乗りさせようとしたが 8KB を超えたので専用バンクにした。
   ★このバンク内から常駐関数(vdp_sprite_pattern)は呼んでよい。データ窓(0xA000)は差し替えない。
   引数は g_shipargs.hull に面番号を入れて渡す(bcall は引数を取れないため)。 */
#include "sprites.h"
#include "ship.h"    /* g_shipargs */
#include "vdp.h"     /* vdp_sprite_pattern / vdp_sprite_pattern_read / vdp_write_addr */
#include "sound.h"   /* play_fanfare / bgm_stop */
#include "input.h"
#include "raster.h"
#include "gamestate.h"   /* g_score / g_hiscore */
#include "assets_data.h" /* PANEL_W / PANEL_WB / PANEL_H */

/* ===== 敵戦闘機/艦載機の8方向スプライトを手続き生成(旧版 build_plane_pattern 移植・常駐) =====
   胴体＋主翼＋尾翼＋エンジンを線分で描く。8方向×3サイズ(小/中/大)を面別に生成しVRAMへ。setup時=cartで実行。
   ★縞々対策(旧版で踏んだ罠): 斜め方向は線が1pxの対角線になり隣接ピクセルが角接触で市松に途切れる。
     斜め時だけ進行X方向へ+1した補填ピクセルを足して隙間を埋める(pl_plot呼びの ★印)。 */
static const s8 pl_ddx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };   /* 0=上,時計回り(dir8/dirdx8と一致) */
static const s8 pl_ddy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
static const u8 pl_sz[3][3] = { {2,2,3}, {4,3,5}, {6,5,7} };  /* {noseL,tailL,wing} 小/中/大 */
static const s8 pl_wingd[5] = { -1, +1, 0, +1, +1 };         /* 面別の翼幅デルタ(独細/米幅広/英中/独幅広/米幅広) */

static void pl_plot(u16 *bar, s8 x, s8 y) {   /* 関数化=マクロ多重展開の肥大を回避(常駐節約) */
    if (x >= 0 && x < 16 && y >= 0 && y < 16) bar[y] |= (u16)(0x8000u >> x);
}
static void build_plane(u8 dir, s8 noseL, s8 tailL, s8 wing, u8 patnum) {
    u16 bar[16]; u8 i, buf[32], k = 0;
    s8 ddx = pl_ddx[dir], ddy = pl_ddy[dir], px = (s8)-ddy, py = ddx, t;
    for (i = 0; i < 16; i++) bar[i] = 0;
    for (t = (s8)-tailL; t <= noseL; t++) {                    /* 胴体(2px幅) */
        s8 fx = (s8)(8 + ddx*t), fy = (s8)(8 + ddy*t);
        pl_plot(bar, fx, fy); pl_plot(bar, (s8)(fx+px), (s8)(fy+py));
        if (ddx && ddy) { pl_plot(bar, (s8)(fx+ddx), fy); pl_plot(bar, (s8)(fx+ddx+px), (s8)(fy+py)); }  /* ★斜め隙間埋め */
    }
    { s8 wx = (s8)(8 + ddx), wy = (s8)(8 + ddy);               /* 主翼 */
      for (t = (s8)-wing; t <= wing; t++) {
          pl_plot(bar, (s8)(wx+px*t), (s8)(wy+py*t)); pl_plot(bar, (s8)(wx+ddx+px*t), (s8)(wy+ddy+py*t));
          if (ddx && ddy) pl_plot(bar, (s8)(wx+px*t+ddx), (s8)(wy+py*t));                                /* ★斜め隙間埋め */
      } }
    { s8 tx = (s8)(8 - ddx*tailL), ty = (s8)(8 - ddy*tailL), w2 = (s8)((wing>>1)+1);  /* 水平尾翼 */
      for (t = (s8)-w2; t <= w2; t++) pl_plot(bar, (s8)(tx+px*t), (s8)(ty+py*t)); }
    { s8 nx = (s8)(8 + ddx*noseL), ny = (s8)(8 + ddy*noseL);   /* エンジン(機首先端3px) */
      pl_plot(bar, nx, ny); pl_plot(bar, (s8)(nx+px), (s8)(ny+py)); pl_plot(bar, (s8)(nx-px), (s8)(ny-py)); }
    for (i = 0; i < 8; i++)  buf[k++] = (u8)(bar[i] >> 8);      /* TL / BL / TR / BR */
    for (i = 8; i < 16; i++) buf[k++] = (u8)(bar[i] >> 8);
    for (i = 0; i < 8; i++)  buf[k++] = (u8)(bar[i] & 0xFF);
    for (i = 8; i < 16; i++) buf[k++] = (u8)(bar[i] & 0xFF);
    vdp_sprite_pattern(patnum, buf);
}
static void load_planes(u8 stage) {
    static const u8 base[3] = { SPR_PLANE_S, SPR_PLANE_M, SPR_PLANE_L };
    s8 wd = pl_wingd[(stage < 5) ? stage : 0];
    u8 sz, d;
    for (sz = 0; sz < 3; sz++)
        for (d = 0; d < 8; d++)
            build_plane(d, (s8)pl_sz[sz][0], (s8)pl_sz[sz][1], (s8)((s8)pl_sz[sz][2] + wd), (u8)(base[sz] + d * 4));
}


/* ============ 宙返りのコマを事前に焼く(ROADMAP P2 項目9) ============
   ★見下ろし視点で上昇して宙返りするので、機体は**上がるほど大きく**(頂点=背面で2倍)、
     機首上げで**縦に縮み**(真横で一本線)、背面で上下反転する。32×32 を 2×2 の4スプライトで出す。
   ★★最初は宙返り中に毎フレーム描いていたが、**エミュの R800 で 16コマに 1.85秒**(頂点付近は
     1コマ 150〜215ms)かかった。原因は 32×32 を C で描くこと自体(ビットごとの可変シフトはループになる)。
     コマは k だけで決まる純関数なので、**面の準備で1回焼いておき、宙返り中は VDP にコピーさせる**。
   ★置き場: **page0 の 212〜220行**。ビットマップ(0〜211行)とスプライト色表(セットB=224行〜)の間の、
     **表示されず誰も使っていない VRAM**。page0 の 32〜211行もゲーム中は空いているが、ここは開始カード
     (draw_stage_card)が表示中に面の準備(stage_build)が走るので、そこへ書くとカードが崩れる。
   ★★宙返りは前後対称なので**描き分けが要るのは9コマ**(k と 16-k は同じ寸法・同じ反転)。9行=1,152B。
   ★出す側(オーバレイ)は **1行(128B)を HMMM で 249行へコピーするだけ**。249行＝パターン表
     0x7800+144*8＝SPR_WAVE0..+12 の4枚ぶん(32B×4)ちょうど。CPU の仕事はほぼゼロ、パターン枠も増えない。
   ★津波とパターン枠を共有する(津波はクラッシュの波が始まるたびに焼き直すので衝突しない)。 */
#define LOOP_VRAM_Y 212
static const u8  zu_w[9]  = { 16, 17, 18, 21, 24, 27, 30, 31, 32 };
static const u8  zu_h[9]  = { 16, 15, 13,  8,  1, 10, 21, 29, 32 };
static const u16 zu_xs[9] = { 256,241,228,195,171,152,137,132,128 };   /* 16*256/w */
static const u16 zu_ys[9] = { 256,273,315,512,4096,410,195,141,128 };  /* 16*256/h */
static const u8  bitm[8]  = { 0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01 };
static u8 zsrc[32], zflat[128], zcmap[32];

static void loop_prerender(void) {
    u8 u, i, dx, dy, sy, sc, L, R, ly, q, w, h, x0, y0, flip;
    u16 acc;
    vdp_sprite_pattern_read(SPR_ZERO, zsrc);           /* 元絵=VRAM が正本(bank16 が直前に載せた) */
    for (u = 0; u < 9; u++) {
        w = zu_w[u]; h = zu_h[u];
        x0 = (u8)((32 - w) >> 1); y0 = (u8)((32 - h) >> 1);
        flip = (u8)(u >= 5);                           /* cos<0 = 背面 → 上下反転 */
        for (i = 0; i < 128; i++) zflat[i] = 0;
        /* 標本点は各列/行の**中心**(累算器を半ステップから)。0 始まりだと真横のコマ(h=1)で
           元絵の0行目=プロペラの細い所だけを拾って素抜けになる。 */
        acc = (u16)(zu_xs[u] >> 1);
        for (dx = x0; dx < (u8)(x0 + w); dx++) { sc = (u8)(acc >> 8); zcmap[dx] = (sc > 15) ? 15 : sc; acc += zu_xs[u]; }
        acc = (u16)(zu_ys[u] >> 1);
        for (dy = y0; dy < (u8)(y0 + h); dy++) {
            sy = (u8)(acc >> 8); if (sy > 15) sy = 15; acc += zu_ys[u];
            if (flip) sy = (u8)(15 - sy);
            L = zsrc[sy]; R = zsrc[(u8)(16 + sy)];
            q  = (dy >= 16) ? 2 : 0;
            ly = (dy >= 16) ? (u8)(dy - 16) : dy;
            for (dx = x0; dx < (u8)(x0 + w); dx++) {
                sc = zcmap[dx];
                if ((sc < 8) ? (L & bitm[sc]) : (R & bitm[(u8)(sc - 8)])) {
                    u8 lx = (dx >= 16) ? (u8)(dx - 16) : dx;
                    u8 qq = (u8)(q + ((dx >= 16) ? 1 : 0));
                    zflat[(u8)((qq << 5) + ((lx < 8) ? ly : (u8)(16 + ly)))] |= bitm[lx & 7];
                }
            }
        }
        vdp_write_addr((u16)((u16)(LOOP_VRAM_Y + u) * 128));
        for (i = 0; i < 128; i++) vdp_data(zflat[i]);
    }
}

/* ===== 面の区切りの画面(常駐から移設。面の開始/撃破で1回しか走らない冷たいコード) =====
   ★data_read はここでは呼べない(窓を差し替えるとこのバンク自身が消える)。撃破!!パネルと艦名は
     常駐側が RAM(g_card_ram / cur_name)へ読んでから g_shipargs で渡す。 */

/* スコアを5桁ゼロ詰め文字列へ。 */
static char scorebuf[6];
static void fmt_score(u16 v) {
    u8 i;
    for (i = 5; i > 0; i--) { scorebuf[i - 1] = (char)('0' + (v % 10)); v /= 10; }
    scorebuf[5] = 0;
}

/* 撃破!! パネルを透過描画(1bit=1px正確)。★各行で立ちビットのラン(連続)を検出し 1 本を vdp_fill(LMMV)で塗る。
   1画素ずつ VRAM へ直接書くと、細切れの di/ei の隙間に割込みが刺さって横に潰れて化けた。 */
static void blit_panel_t(u16 dstx, u16 dsty, u8 oncol) {
    u8 y, c;
    for (y = 0; y < PANEL_H; y++) {
        const u8 *row = &g_card_ram[(u16)y * PANEL_WB];
        c = 0;
        while (c < PANEL_W) {
            if (row[c >> 3] & (u8)(0x80 >> (c & 7))) {
                u8 run = 1;
                while ((u8)(c + run) < PANEL_W &&
                       (row[(u8)(c + run) >> 3] & (u8)(0x80 >> ((c + run) & 7)))) run++;
                vdp_fill((u16)(dstx + c), (u16)(dsty + y), run, 1, oncol);
                c = (u8)(c + run);
            } else c++;
        }
    }
}

/* 撃破結果画面(page0)＋勝ちどきファンファーレ＋トリガ待ち。パネルは g_card_ram、撃沈文は ops。 */
static void results_impl(const char *m) {
    u8 f, n = 0;
    vdp_fill(0, 0, 256, 212, 1);        /* 背景=エンディング/開始カードと同じ青(色1) */
    vdp_cmd_wait();
    vdp_wait_frame();                   /* ★切替は VBLANK 直後にまとめて(途中で変えると1フレームだけ混ざる) */
    vdp_set_display_page(0);            /* ★塗り終えてから表示(page0 に残る開始カードを見せない) */
    vdp_set_vscroll(0);
    vdp_set_hscroll(0, 0);              /* ★横スクロール(蛇行weaveX)も解除=残ると画面全体が右に寄る */
    vdp_palette_game();                 /* ★面の色調(夕焼け/夜など)を昼の基準へ戻す(切替の後。先だと沈んだ海の色が変わって見える) */
    /* 撃破!! 。黒影を横+6/縦+2へずらす。透過blitで「青地 → 影(黒) → 本体(白)」の順に重ねる */
    blit_panel_t(78, 42, 0);
    blit_panel_t(72, 40, 15);
    while (m[n]) n++;
    vdp_text_s((u8)((256 - (u16)n * 16) / 2), 100, 15, 1, 2, m);   /* [艦名] SUNK を白・2倍角で中央 */
    if (g_score > g_hiscore) g_hiscore = g_score;
    fmt_score(g_score);
    vdp_text(72, 132, 15, 1, "SCORE");
    vdp_text(120, 132, 15, 1, scorebuf);
    fmt_score(g_hiscore);
    vdp_text(72, 152, 15, 1, "HI");
    vdp_text(120, 152, 15, 1, scorebuf);
    play_fanfare();                     /* 勝ちどき(BGM停止・前景同期) */
    vdp_text(88, 176, 15, 1, "PUSH SPACE");
    { u8 armed = 0;                     /* ★連射ホールドで一瞬で飛ばされないよう「一度離してから押す」 */
      for (f = 0; f < 240; f++) {
          input_poll();
          if (!(g_input & INP_TRIG)) armed = 1;
          if (armed && (g_input_edge & INP_TRIG)) break;
          vdp_wait_frame();
      }
    }
}

/* 開始カードの文字(STAGE n / - TARGET - / 艦名)。艦の絵は常駐側が続けて描く。 */
static void card_text_impl(u8 stage, const char *nm) {
    u8 n = 0;
    char num[2];
    while (nm[n]) n++;
    bgm_stop();                              /* カード中は無音 */
    vdp_palette_game();                      /* ★前の面の色調を昼の基準へ戻す */
    raster_off();                            /* ★page0 全面の画面。分割表を走らせない */
    vdp_set_vscroll(0);
    vdp_sprite_hide_from(0);
    vdp_set_display_page(0);
    vdp_fill(0, 0, 256, 212, 1);
    num[0] = (char)('1' + stage); num[1] = 0;
    vdp_text_s(72, 18, 15, 1, 2, "STAGE");
    vdp_text_s(168, 18, 15, 1, 2, num);
    vdp_text_s(48, 44, 11, 1, 2, "- TARGET -");
    vdp_text_s((u8)(128 - n * 8), 176, 15, 1, 2, nm);
}

/* ★5面の中ボス(P-61)の準備: スプライトを全部 2 倍に拡大(MAG)するので、絵の表A(0x7800)の64枚を半分(左上 8x8)に
   縮めて絵の表B(0x2000)へ置く(2x2 画素の OR=細い弾も消えない)。オーバレイの枠が足りないので、1回きりのこの処理はここ(ROM 実行)。
   宙返りのコマは ovl_rot(OVL_MAG) が毎回同じ縮め方で書く。 */
static void mag_table(void) {
    u8 p, r, i, src[32];
    vdp_cmd_wait();
    for (p = 0; p < 64; p++) {
        vdp_read_addr((u16)(0x7800 + ((u16)p << 5)));
        for (i = 0; i < 32; i++) src[i] = vdp_read_data();
        vdp_write_addr((u16)(0x2000 + ((u16)p << 5)));
        for (r = 0; r < 8; r++) {
            u16 s = (u16)(((u16)(src[r * 2] | src[r * 2 + 1]) << 8) | (u8)(src[16 + r * 2] | src[16 + r * 2 + 1]));
            u8 d = 0, m;
            for (m = 0x80; m; m >>= 1) { if (s & 0xC000) d |= m; s <<= 2; }   /* ★0x80>>i の形は、このバンクの版で i のずらしが効かず全行 0x80 になった */
            vdp_data(d);
        }
        for (r = 8; r < 32; r++) vdp_data(0);
    }
}

void banked_entry(void) {
    if (g_shipargs.mode == 7) { mag_table(); return; }
    if (g_shipargs.mode == 5) { results_impl((const char *)g_shipargs.ops); return; }
    if (g_shipargs.mode == 6) { card_text_impl(g_shipargs.hull, (const char *)g_shipargs.ops); return; }
    load_planes(g_shipargs.hull);
    loop_prerender();
}
