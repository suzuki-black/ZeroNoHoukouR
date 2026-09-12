/* ovl_crush.c — メガクラッシュの稲妻を手続き生成して画面へ描く(RAMオーバレイ)。

   ★アルゴリズムは 2D 稲妻の定番「中点変位(midpoint displacement)＋分岐」に従う。
     出典: "How to Generate Shockingly Good 2D Lightning Effects"(Tuts+ / Drilian 法)。
     https://code.tutsplus.com/how-to-generate-shockingly-good-2d-lightning-effects--gamedev-2681t
     骨子は3つで、どれを欠いても稲妻に見えない:
       1. 始点→終点の直線に沿って点を取り、**垂線方向**へランダム変位させる(Sway)
       2. **隣接点との変化量を制限**して角を鋭くしすぎない(平滑化)。これが無いとただのノイズになる
       3. **終点近くは変位を絞る**(envelope)＝線がちゃんと目標へ収束する
     分岐は 30° 前後で本線から生やす(原典は主稲妻あたり3〜6本)。
   ★本作の稲妻はほぼ縦に走るので、垂線方向＝ほぼ水平。よって変位を x のオフセットとして扱う
     (三角関数を持たずに済む。Z80/R800 に浮動小数は無い)。

   ★クラッシュ中はスクロールもAIも止まっている＝30fps のフレーム予算に縛られない。
     だから普段は選べない「実際に線を引く」という重い手が使える(仕様で止めてよいと決めた恩恵)。
   ★線は V9938 の LINE コマンド(1本=コマンド1発)。矩形を積み上げる方式は実測で1セット360msかかり
     遅すぎた(しかも描画中ずっと前フレームのパレットが出たまま＝真っ白で何も見えなかった)。
   ★消去は scroll_repaint_all()(常駐)が世界の正本から引き直す。ここでは描くだけ。 */
#include "types.h"
#include "vdp.h"
#include "scroll.h"
#include "entity.h"   /* ent_at / ENT_MAX / g_ebul: 敵弾の一括消去 */

#define PAGE1_Y   256    /* 表示リングの基準Y(scroll.c と同じ) */
#define BOLT_CORE 15     /* 芯=白 */
#define BOLT_GLOW 14     /* 光芒=淡灰(芯の隣に薄く重ねて太く見せる) */
#define SCR_BOT   208    /* 稲妻を走らせる下端 */

/* ★V9938 LINE(0x70): (dx,dy)→(x2,y2) に直線を1コマンドで引く。本ファイル(稲妻)専用。
   ★常駐に置くと DEBUG_PROF ビルドが 24KB を超えるのでオーバレイ側に置く。
   長辺=NX / 短辺=NY / ARG bit0=MAJ(1=Yが長辺) bit2=DIX(左向き) bit3=DIY(上向き)。
   ★矩形を積み上げて線を描くより桁違いに速い(1本=コマンド1発)。稲妻は数百本引くので効く。 */
static void vdp_line(u16 dx, u16 dy, u16 x2, u16 y2, u8 color) {
    u16 adx, ady, nx, ny;
    u8 arg = 0;
    if (x2 >= dx) adx = (u16)(x2 - dx); else { adx = (u16)(dx - x2); arg |= 0x04; }
    if (y2 >= dy) ady = (u16)(y2 - dy); else { ady = (u16)(dy - y2); arg |= 0x08; }
    /* ★短辺(NY)が 0 になる純粋な垂直/水平線は LINE で描いてはいけない。VDP はサイズレジスタの 0 を
       「最大(512/1024)」と解釈するため、線が画面幅いっぱいに暴走して下地を白い点で撒き散らす
       (実機ではなく openMSX で再現。稲妻の上端にノイズ帯が出るという形で踏んだ)。矩形塗りで正確に描く。 */
    if (adx == 0 && ady == 0) return;
    if (adx == 0) { vdp_fill(dx, (y2 >= dy) ? dy : y2, 1, (u16)(ady + 1), color); return; }
    if (ady == 0) { vdp_fill((x2 >= dx) ? dx : x2, dy, (u16)(adx + 1), 1, color); return; }
    if (ady >= adx) { nx = ady; ny = adx; arg |= 0x01; } else { nx = adx; ny = ady; }
    vdp_cmd_wait();
    vdpcbuf[4]  = dx & 0xFF; vdpcbuf[5]  = (dx >> 8) & 0x01;
    vdpcbuf[6]  = dy & 0xFF; vdpcbuf[7]  = (dy >> 8) & 0x03;
    vdpcbuf[8]  = nx & 0xFF; vdpcbuf[9]  = (nx >> 8) & 0x01;
    vdpcbuf[10] = ny & 0xFF; vdpcbuf[11] = (ny >> 8) & 0x03;
    vdpcbuf[12] = color;
    vdpcbuf[13] = arg;
    vdpcbuf[14] = 0x70;   /* CMD = LINE(論理IMP) */
    vdp_cmd_flush();
}


static u16 rng;
static u8 rnd(void) { rng = (u16)(rng * 25173 + 13849); return (u8)(rng >> 8); }
/* ★0..n-1 の一様乱数。**剰余(% n)を使わないこと**: rnd() は 0..255 なので n=176 なら
   256%176=80 ぶんだけ 0..79 が二倍出る＝稲妻が画面左へ偏る(実機で指摘された)。
   乗算シフトなら分布が崩れない。 */
static u8 rndn(u8 n) { return (u8)(((u16)rnd() * n) >> 8); }

/* 画面座標の線分を表示リングへ引く。★リング(256px周期)の継ぎ目をまたぐ場合は交点で2本に割る。
   割らずに引くと VRAM の別領域(海テンプレ 512〜)へ線が突き抜けて下地を壊す。 */
static void seg(s16 x0, u8 y0, s16 x1, u8 y1, u8 c) {
    u16 v0 = (u16)((u16)g_vscroll + y0);
    u16 v1 = (u16)((u16)g_vscroll + y1);
    if ((u8)(v0 >> 8) == (u8)(v1 >> 8)) {
        vdp_line((u16)x0, (u16)(PAGE1_Y + (v0 & 0xFF)), (u16)x1, (u16)(PAGE1_Y + (v1 & 0xFF)), c);
    } else {
        u8 yc = (u8)(((v0 & 0xFF00) + 256) - (u16)g_vscroll);   /* 継ぎ目にあたる画面Y */
        s16 dy = (s16)(y1 - y0);
        s16 xc = (dy > 0) ? (s16)(x0 + ((s16)(x1 - x0) * (s16)(yc - y0)) / dy) : x0;
        vdp_line((u16)x0, (u16)(PAGE1_Y + (v0 & 0xFF)), (u16)xc, (u16)(PAGE1_Y + 255), c);
        vdp_line((u16)xc, (u16)PAGE1_Y,                 (u16)x1, (u16)(PAGE1_Y + (v1 & 0xFF)), c);
    }
}

/* 中点変位で (x0,y0)→(x1,y1) をギザギザに引く。
   sway=変位の最大幅 / nseg=分割数 / glow=1 なら芯の隣に光芒も引く。
   ★平滑化(隣接点との変化量制限)と envelope(終点で変位0)が稲妻らしさの要。 */
static void jag(s16 x0, u8 y0, s16 x1, u8 y1, u8 nseg, u8 sway, u8 glow) {
    s16 px = x0, disp = 0;
    u8  py = y0, i;
    for (i = 1; i <= nseg; i++) {
        u8  y = (u8)(y0 + (u16)((u16)(y1 - y0) * i) / nseg);
        s16 bx = (s16)(x0 + ((s16)(x1 - x0) * (s16)i) / (s16)nseg);   /* 直線上の基準点 */
        s16 x;
        if (i < nseg) {
            s16 t = (s16)((s16)rndn((u8)(2 * sway + 1)) - (s16)sway);   /* 目標変位(剰余は使わない) */
            s16 d = (s16)(t - disp);
            if (d >  12) d =  12;          /* ★平滑化: 隣接点との差を制限(角を鋭くしすぎない) */
            else if (d < -12) d = -12;
            disp = (s16)(disp + d);
            if (i >= (u8)(nseg - 1)) disp >>= 1;   /* ★envelope: 終点手前で変位を絞る */
        } else {
            disp = 0;                      /* 終点は必ず目標へ */
        }
        x = (s16)(bx + disp);
        if (x < 3)   x = 3;
        else if (x > 251) x = 251;
        /* ★太さ: 芯を3本(x-1,x,x+1)の白で引き、その外側に光芒を2本。1本だけだと細すぎて
           16色の海の上では稲妻に見えない(実機で指摘された)。枝は細いまま(本物も枝は細い)。 */
        if (glow) {
            seg((s16)(px - 3), py, (s16)(x - 3), y, BOLT_GLOW);
            seg((s16)(px + 3), py, (s16)(x + 3), y, BOLT_GLOW);
            seg((s16)(px - 1), py, (s16)(x - 1), y, BOLT_CORE);
            seg((s16)(px + 1), py, (s16)(x + 1), y, BOLT_CORE);
        }
        seg(px, py, x, y, BOLT_CORE);
        px = x; py = y;
    }
}

/* seed ごとに違う稲妻群を描く。主稲妻 2〜3 本＋各 3〜4 本の枝。 */
void ovl_crush_bolts(u8 seed) {
    u8 b, nb, bw;
    rng = (u16)(seed * 2069u + 1013u);
    nb = (u8)(2 + (rnd() & 1));      /* 主稲妻 2〜3本 */
    bw = (u8)(232 / nb);             /* ★画面を本数で割り、1帯に1本ずつ置く。
                                        全部を乱数任せにすると固まって偏る(実機で左偏りを指摘された)。 */
    for (b = 0; b < nb; b++) {
        s16 x0 = (s16)(12 + (s16)b * bw + (s16)rndn((u8)(bw - 16)));
        s16 x1 = (s16)(x0 + (s16)rndn(97) - 48);       /* 下端は左右へ流す */
        u8  k, nbr = (u8)(3 + (rnd() & 1));
        if (x1 < 10) x1 = 10; else if (x1 > 246) x1 = 246;
        jag(x0, 0, x1, SCR_BOT, 18, 34, 1);            /* 本線(太い芯＋光芒) */
        /* ★枝: 本線上の点から 30°前後で分かれ、短く走って消える(原典は主稲妻あたり3〜6本)。 */
        for (k = 0; k < nbr; k++) {
            u8  by = (u8)(24 + rndn(150));             /* 分岐点の高さ */
            s16 bx = (s16)(x0 + ((s16)(x1 - x0) * (s16)by) / SCR_BOT);
            u8  len = (u8)(28 + rndn(44));
            u8  ey  = (u8)(((u16)by + len > SCR_BOT) ? SCR_BOT : (u8)(by + len));
            /* tan30°≒0.58≒4/7。下向きの接線を ±30° 回した先を終点にする。 */
            s16 ex  = (s16)(bx + ((rnd() & 1) ? (s16)((u16)len * 4 / 7) : -(s16)((u16)len * 4 / 7)));
            if (ex < 6) ex = 6; else if (ex > 250) ex = 250;
            jag(bx, by, ex, ey, 6, 12, 0);
        }
    }
}

/* ★メガクラッシュ用: 画面上の敵弾だけを消す(敵機・砲台・自機弾は残す)。
   対象は敵弾(ET_BULLET かつ TEAM_ENEMY)・対空砲の時限信管弾(ET_AABURST)・潜水艦ミサイル(ET_SMISSILE)。
   CPU弾幕(curtain)は別プールなので呼び側が curtain_reset() を併せて呼ぶこと。
   ★常駐(entity.c)ではなくここに置く理由: 呼ぶのはクラッシュの1フレームだけ＝完全に冷たいのに
     常駐窓を約70B食う。DEBUG_PROF ビルドが 24KB 天井に貼り付いているので追い出した。
     オーバレイはホット区間(page2=RAM)の中でしか呼べないので、呼び出しは ramx_use_ram() の内側。 */
void ovl_clear_enemy_bullets(void) {
    u8 i;
    Entity *e;
    for (i = 0; i < ENT_MAX; i++) {
        e = ent_at(i);
        if (!e->active) continue;
        if ((e->type == ET_BULLET && e->team == TEAM_ENEMY) ||
            e->type == ET_AABURST || e->type == ET_SMISSILE) e->active = 0;
    }
    g_ebul = 0;   /* 敵弾カウンタ(O(1)上限判定用)も畳む。次フレームの再計数で整合する */
}

