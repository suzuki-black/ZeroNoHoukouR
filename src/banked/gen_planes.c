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
#include "entity.h"      /* 決戦前の投棄(ent_spawn) */
#include "player.h"      /* g_player_x/y */
#include "hud.h"         /* hud_draw / HUD_SLOTS(投棄でアイコンを1つずつ減らす) */
#include "assets_data.h" /* PANEL_W / PANEL_WB / PANEL_H */
#include "scroll.h"      /* g_cam(警報パネルを表示リングの行へ直す) */
#include "panel_alert.h" /* 敵艦発見 / 敵大将発見(gen_alert.py) */

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

/* ---- 警報(敵艦発見 / 敵大将発見)。ゲーム画面(表示リング page1)へ透過で描く ----
   画面の行 sy は、縦スクロール(R#23 = g_cam の下位)を足してリングの行へ直す(リングの継ぎ目で折り返す)。
   消すのは常駐の scroll_repaint_all(海の正本から引き直す)。描いている間は海の塗り直しと縦スクロールを止める。 */
#define ALERT_Y 70      /* 漢字の上端(画面の行)。英語はその 50 行下 */
/* ★1行ぶんの作業場は固定番地 0xE700(ship_ram / 中ボスの MB_BUF。どちらも警報の間は使っていない)。
   このバンクの static は 0xE000〜で、0xE100 からは曲データ(bgm_ram)。128B の static を置いたら曲を踏み潰し、
   サイレンの代わりに変な音が鳴った(openMSX の録音で確認) */
#define alert_line ((u8 *)0xE700)
static void alert_bits(const u8 *row, u8 wb, u16 x, u8 sy, u8 col) {
    /* 1行ぶん(左から 8*wb ドット)を「VRAM から読む→立っているドットだけ色を差す→書き戻す」で重ねる。
       ★ランごとに塗り命令を出す方式(blit_panel_t)は、この大きさだと 2 秒以上かかり画面が止まって見えた */
    u8 k, n = (u8)(wb << 2);                                   /* 1バイト=2ドット */
    u16 a = (u16)((u16)(256 + (u8)((u8)g_cam + sy)) * 128 + (x >> 1));
    vdp_read_addr(a);
    for (k = 0; k < n; k++) alert_line[k] = vdp_read_data();
    {   /* ビット列1バイト(8ドット)= VRAM 4バイト。2ドットずつ「どちらのドットに差すか」のマスクで混ぜる。空のバイトは飛ばす */
        static const u8 m2[4] = { 0x00, 0x0F, 0xF0, 0xFF };   /* 2ビット(左,右) → VRAM 1バイト内のマスク */
        u8 cc = (u8)(col * 0x11), *b = alert_line;
        for (k = 0; k < wb; k++, b += 4) {
            u8 v = row[k];
            if (!v) continue;
            { u8 m = m2[v >> 6];       b[0] = (u8)((b[0] & (u8)~m) | (cc & m)); }
            { u8 m = m2[(v >> 4) & 3]; b[1] = (u8)((b[1] & (u8)~m) | (cc & m)); }
            { u8 m = m2[(v >> 2) & 3]; b[2] = (u8)((b[2] & (u8)~m) | (cc & m)); }
            { u8 m = m2[v & 3];        b[3] = (u8)((b[3] & (u8)~m) | (cc & m)); }
        }
    }
    vdp_write_addr(a);
    for (k = 0; k < n; k++) vdp_data(alert_line[k]);
}
static void alert_panel(const u8 *p, u16 x, u8 sy, u8 col) {
    u8 y, wb = p[0], h = p[1];
    const u8 *row = p + 2;
    for (y = 0; y < h; y++, row += wb) alert_bits(row, wb, x, (u8)(sy + y), col);
}
static void alert_text(const char *m, u16 x, u8 sy, u8 col) {
    for (; *m; m++, x += 8) {
        const u8 *g = vdp_glyph((u8)*m);
        u8 y;
        for (y = 0; y < 8; y++) alert_bits(&g[y], 1, x, (u8)(sy + y), col);
    }
}
__sfr __at(0xA0) PSG_R;
__sfr __at(0xA1) PSG_V;
static void tone_wait(u8 lo, u8 n) {
    u8 v = 15, i;
    PSG_R = 0; PSG_V = lo;
    PSG_R = 1; PSG_V = 0;
    for (i = 0; i < n; i++) {
        PSG_R = 8; PSG_V = v;        /* ch A 音量 */
        v = (u8)((v > 3) ? v - 4 : 0);
        vdp_wait_frame();
    }
    PSG_R = 8; PSG_V = 0;
}
#define tick_wait() tone_wait(124, MSG_WAIT)   /* 打電音(周期124≒900Hz) */

/* ★決戦前の投棄(ユーザー案A): 「敵大将発見」と同時に、持っている増槽と爆弾を自機が捨てる。
   当たっても威力0の自機の弾として落とすだけ(画面の下へ抜けたら自動で消える)。捨てた瞬間に弾の段階とボムを 0 にする
   =「捨てたから弱くなった」という順番にする(黙って 0 にしていたのを改めた)。 */
static void drop1(s8 dx, u8 tank) {
    Entity *e = ent_spawn(ET_BULLET);
    if (e) {
        e->x = (s16)(g_player_x + dx); e->y = (s16)(g_player_y + 4);
        e->vx = (s8)(dx >> 3); e->vy = 3;
        e->team = TEAM_PLAYER; e->hp = 0;              /* 威力0=当たっても何も起きない */
        e->pat = tank ? SPR_TANK : SPR_EBSHELL;        /* 増槽 / 爆弾 */
        if (tank) e->coltab = barrel_col;              /* 増槽=金属の陰影(常駐の表。バンクの表は窓が戻ると消える) */
        else      e->color = 13;                       /* 爆弾=ほぼ黒 */
    }
    tone_wait(210, 25);                                /* 投下の音＋間(1つずつ落とす) */
}

/* ★投棄: 増槽 → 爆弾 の順に**1つずつ**落とす。落とすたびに段階/残数を1つ減らし、HUD のアイコンも1つ減らす。 */
/* ★パワーアップ段階のアイコンを1枚置き直す。ふだんは ent_draw_all が**最後の枠**に描くが、投棄は前景
   (ゲームのフレームが進まない)なので自分で置く。空いている先頭(g_spr_used)へ置き、その後ろに停止マーカ。 */
static void pwr_icon_fg(void) {
    u8 sl = g_spr_used;
    vdp_sprite_color_tab(sl, pwr_col[g_pwr]);
    vdp_sprite_pos(sl, PWR_ICON_X, PWR_ICON_Y, SPR_PWRLV);
    vdp_sprite_hide_from((u8)(sl + 1));
}

static void jettison(void) {
    while (g_pwr) {
        g_pwr--;
        drop1((s8)((g_pwr & 1) ? 14 : -14), 1);
        hud_draw(g_score, g_lives);
        pwr_icon_fg();                       /* 段階が1つ減ったのを見せる */
    }
    ent_spr_cache_inval(g_spr_used);         /* ★色表を直書きしたのでキャッシュを捨てる */
    while (g_crush) {
        g_crush--;
        drop1((s8)((g_crush & 1) ? 5 : -5), 0);
        hud_draw(g_score, g_lives);
    }
}

/* ★電文(最終面): 「敵大将発見」の下に、一文字ずつ音を立てて出す(無音の中で)。出し終えたら投棄。
   前景で回す(results_impl と同じやり方)。この間はゲームのフレームが進まないので、スクロールも止まったまま。 */
#define MSG_Y   142     /* 1行目の上端(画面の行)。「敵大将発見」は 70〜128 */
#define MSG_ADV 20      /* 1文字の送り(18 ドット＋間 2) */
#define MSG_WAIT 15     /* 1文字あたりの間(フレーム)。緊急の呼びかけの後の決意なのでゆっくり(ユーザー指定) */
/* ★打電音: 曲も効果音も鳴っていない場面なので、PSG の tone A を直接鳴らす(sfx の短いノイズでは小さすぎると指摘)。
   最大音量の硬い「ツッ」から一気に減衰させる。割込み(ISR)は BGM 停止・効果音なしのとき ch A に触らない。 */
static void msg_line(const u8 *p, u8 sy) {
    u8 i, y;
    u16 x0 = (u16)((256 - (u16)MSG_N * MSG_ADV) >> 1);
    for (i = 0; i < MSG_N; i++, p += MSG_WB * MSG_H) {
        u16 x = (u16)(x0 + (u16)i * MSG_ADV);
        for (y = 0; y < MSG_H; y++) alert_bits(&p[y * MSG_WB], MSG_WB, x + 2, (u8)(sy + y + 2), 13);   /* 影 */
        for (y = 0; y < MSG_H; y++) alert_bits(&p[y * MSG_WB], MSG_WB, x, (u8)(sy + y), 15);           /* 本体=白 */
        tick_wait();                              /* 一文字ごとに打電音＋間 */
    }
}
static void msg_impl(void) {
    u8 f;
    bgm_stop();                                   /* 電文と投棄の間は無音 */
    msg_line(msg0, MSG_Y);
    for (f = 0; f < 30; f++) vdp_wait_frame();    /* 1行目のあとで一拍おく */
    msg_line(msg1, (u8)(MSG_Y + MSG_H + 6));
    for (f = 0; f < 60; f++) vdp_wait_frame();    /* 読ませる間 */
    jettison();                                   /* 増槽と爆弾を捨てる(落ちるのはこの後、ゲームが動き出してから) */
    for (f = 0; f < 40; f++) vdp_wait_frame();
}

static void alert_impl(u8 which) {
    const u8 *p = which ? alert_tai : alert_kan;
    vdp_cmd_wait();                                   /* 海の塗り直し(VDP コマンド)が済んでから読む */
    const char *m = which ? "ENEMY FLAGSHIP SIGHTED" : "ENEMY FLEET SIGHTED";
    u16 x = (u16)((256 - ((u16)p[0] << 3)) >> 1);
    u8 n = 0;
    while (m[n]) n++;
    alert_panel(p, x + 4, ALERT_Y + 2, 13);          /* 影(ほぼ黒)を右下へずらして先に(x は偶数=1バイト2ドット) */
    alert_panel(p, x, ALERT_Y, 11);                   /* 本体=赤 */
    x = (u16)((256 - (u16)n * 8) >> 1);
    alert_text(m, x + 2, ALERT_Y + 51, 13);
    alert_text(m, x, ALERT_Y + 50, 15);               /* 英語=白 */
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
    if (g_shipargs.mode == 8) { alert_impl(g_shipargs.hull); return; }   /* 警報パネル(hull=0 敵艦発見 / 1 敵大将発見) */
    if (g_shipargs.mode == 9) { msg_impl(); return; }                    /* 最終面の電文＋投棄 */
    if (g_shipargs.mode == 5) { results_impl((const char *)g_shipargs.ops); return; }
    if (g_shipargs.mode == 6) { card_text_impl(g_shipargs.hull, (const char *)g_shipargs.ops); return; }
    load_planes(g_shipargs.hull);
    loop_prerender();
}
