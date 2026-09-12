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
#include "sprites.h"  /* SPR_WAVE0: 津波のコマ */
#include "gamestate.h" /* CRUSH_WAVE_*: 津波の台本 */
#include "gamestate.h" /* CRUSH_WAVE_*: 津波の台本 */

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
        jag(x0, 0, x1, SCR_BOT, 12, 34, 1);            /* 本線(太い芯＋光芒)。★分割数は描画コスト直結:
                                                          18 だと1枚 0.45秒かかり、3発で演出が冗長になる */
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

/* ============================ 津波 ============================
   ★稲妻のあとに「海そのものが立ち上がる」一撃。画面下(＝自機のいる側)から
     **画面幅いっぱいの水の壁**が、地鳴りとともに加速しながら駆け上がる。

   ── 1. なぜ拡大スプライトか(枚数制限の突破) ──
   V9938 は SI(16x16)＋MAG(R#1 bit0)で1枚 **32x32ドット**になる(MSX2 Technical Handbook ch4)。
   1走査線8枚という制限は「枚数」なので、32px 幅なら 8枚×32 = 256px = 画面幅ちょうどを
   1本の走査線で覆える。段を **32px ちょうどの間隔** で積めば段どうしが重ならず、どの走査線も
   常にきっちり8枚のまま。8列×4段=32枚(全スプライト)で 256×128px の水の壁になる。
   拡大なし(16px)だと8枚で128px＝画面の半分しか覆えない。
   ★MAG は全スプライトに効く(画面途中で R#1 を差し替える技は Grauw の split guide にも無く
     実機依存の地雷)。よって津波の間は HUD も敵も出さない＝32枚すべてを波が使う。

   ── 2. 絵作り(「のっぺりした MSX っぽさ」を消す) ──
   最初はべた塗りの帯を4段積んだだけで、単色の面が大きく「しょぼい/MSXっぽい」となった。
   レトロなドット絵の水の描き方を調べて、次の3つを効かせている:
     (a) **波の解剖に沿う**: peak / lip / trough / foam zone。とくに **lip(波頭の唇)の
         真下に濃い影**を置くと、ただの帯が一気に立体的な波になる
         (skyryedesign "Waves Drawing Guide": 「a heavier shadow under the lip」
          「darker hues in wave troughs, inside barrels, and under wave crests」)。
     (b) **泡は場所で描き分ける**: 上端は細かい飛沫(mist)、波頭は割れた白い塊
         (broken white shapes)、面は流れに沿った筋(streaky lines)、裾は粗い粒。
     (c) **色は段でなくディザで繋ぐ**: 限られた色数で階調を作る定石(drububu / Pixnote)。
         mode2 のスプライトは **行ごとに1色** なので、色表の隣り合う行を交互に置くと
         縦方向の 1D ディザになり、拡大で1行=2px なので目には中間色として溶ける。
         ★べた塗りの幅を広く取らないこと。それが「のっぺり」の正体。
   ★色表が使えるのは波が **水平な帯** だから(斜めに置くと行と波頭がずれて使えない)。

   ── 3. 継ぎ目 ──
   どのコマも**左端列と右端列の波頭高さを row4 に揃えて**あるので、A/B をどの順に並べても
   段差が出ない。泡の粒/筋の周期は 16 の約数(4/8)にしてあるので横に繋げても柄が段にならない。

   ── 4. 動き ──
   一定速度で上がると「板が平行移動している」ようにしか見えない。実際の津波と同じく
   **加速**させる: 進んだ距離を step の二乗に比例させる(dist = step*step/6)。
   出だしはほとんど動かず地鳴りだけ(ゴゴゴゴ)、後半で一気に駆け上がる(ゴーーー)。
   画面揺れ(R#23 ジッタ)と低いノイズ(SFX_RUMBLE)は呼び側(scene_stage)が重ねる。 */
#define WAVE_COLS 8    /* 8枚 × 32px = 256px = 画面幅(1走査線ちょうど8枚) */
#define WAVE_ROWS 4    /* 4段 × 32px = 128px の厚み(段は重ならないので枚数制限は増えない) */

/* コマ5種。★SPR_WAVE4 は HUD のボム棒(SPR_CRUSH)と枠を共有する。津波の間は HUD を出さないので
   奪ってよいが、終わったら hud_colors() が棒のパターンを描き直す。 */
static const u8 wave_pat[5][32] = {
    /* CRESTA: 波頭の山 */
    { 0x00, 0x1C, 0x3E, 0x77, 0xFF, 0xFD, 0xEF, 0xEF, 0xFF, 0x5F, 0xFF, 0xF0, 0xDF, 0xFF, 0xFF, 0xE7,
      0x00, 0x88, 0x34, 0x7A, 0xFE, 0xFF, 0xDF, 0xFF, 0x9F, 0xBC, 0xFF, 0x25, 0xFF, 0xFF, 0xFF, 0xFE },
    /* CRESTB: 波頭の谷 */
    { 0x00, 0x00, 0x00, 0x00, 0x80, 0xE4, 0x74, 0xBD, 0xFF, 0xA8, 0xFF, 0x79, 0xFF, 0xFF, 0x7F, 0x7E,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x43, 0x4F, 0x0F, 0xFB, 0x7B, 0x99, 0xFF, 0xFD, 0xFF, 0xFF },
    /* FACE: 波の面(泡の線＋粒) */
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x40, 0xBF, 0xFF, 0xFE, 0xEF, 0x22, 0xFF, 0xFF, 0xFB, 0xCF,
      0xFF, 0xFF, 0xFF, 0xEF, 0x55, 0x7F, 0x54, 0xFE, 0x53, 0xFF, 0xFF, 0x00, 0xBB, 0x9F, 0xFE, 0xF7 },
    /* BODY: 胴(粗い粒) */
    { 0xFF, 0xFF, 0xFF, 0xCF, 0x18, 0xED, 0xFF, 0xDF, 0xB3, 0xC9, 0xE7, 0x7D, 0xE6, 0xA7, 0x84, 0x4E,
      0xBF, 0xFD, 0xDF, 0xFF, 0x84, 0x7B, 0xF6, 0xFF, 0x2F, 0x1B, 0xEF, 0x7E, 0xDE, 0x7A, 0x00, 0xB1 },
    /* FOOT: 裾(引き波の乱れ。疎らに散って海へ溶ける) */
    { 0x47, 0xE7, 0x52, 0x39, 0x20, 0x83, 0x98, 0x21, 0x22, 0x03, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
      0x43, 0x2A, 0x1A, 0xEC, 0x90, 0x24, 0x00, 0x10, 0x08, 0x10, 0x00, 0x11, 0x04, 0x80, 0x01, 0x00 },
};
static const u8 wave_pnum[5] = { SPR_WAVE0, SPR_WAVE0 + 4, SPR_WAVE0 + 8, SPR_WAVE0 + 12, SPR_WAVE4 };

/* 段ごとの行別カラー(mode2 は色が行単位＝拡大時は1エントリが画面2ライン)。
   ★色は**塊で**置く。1行おきに替えると拡大で2px の横縞になり、水ではなく鎧戸に見える(実際そうなった)。
     替えるのは波の解剖上の意味があるところだけ: lip の下の濃い影 / 面の泡の線 / 粒のアクセント。
   ★質感は「穴(透明)」が作る。下地は**ノイズ入りの海**(scroll_build_sea が色2/7を撒いている)なので、
     穴を散らすと海の粒が透けて自然な濁りになる。とくに胴から下は**地の色と同じ色(1)**を使い、
     穴が目立たないので低い塗り率でも破綻せず、粒だけが効く。
   ★逆に濃紺で厚く塗ると画面下半分が黒い塊になり艦も海も飲み込む(これも一度やって失敗した)。
     15=白 / 14=淡青灰 / 2=明るい水 / 1=海の地色 / 7=濃紺(影)。 */
static const u8 wcol0[16] = { 15,15,15,15,15,15,15,15, 15,14,14, 7,  7, 7, 7,14 };  /* 白い泡→淡→**lipの下の濃い影**→下面の淡 */
static const u8 wcol1[16] = {  2,15, 2, 2, 2, 2,15, 2,  2, 2, 2,14,  2, 2, 2, 2 };  /* 面: 明るい水＋泡の線＋粒 */
static const u8 wcol2[16] = {  2, 2, 2, 2,14, 2, 2, 1,  1, 7, 1, 1,  1, 1, 7, 1 };  /* 胴: 明るい水→海の地色＋濃紺の粒 */
static const u8 wcol3[16] = {  1, 1, 1, 7, 1, 1, 7, 1,  7, 1, 7, 1,  7, 1, 7, 7 };  /* 裾: 引き波の乱れ */

/* 波のコマと色をVRAMへ置き、スプライトを拡大モードにする。津波の1フレーム目に1回だけ。 */
void ovl_crush_wave_init(void) {
    u8 i;
    for (i = 0; i < 5; i++) vdp_sprite_pattern(wave_pnum[i], wave_pat[i]);
    for (i = 0; i < WAVE_COLS; i++) {
        vdp_sprite_color_tab(i,                     wcol0);
        vdp_sprite_color_tab((u8)(WAVE_COLS + i),   wcol1);
        vdp_sprite_color_tab((u8)(WAVE_COLS*2 + i), wcol2);
        vdp_sprite_color_tab((u8)(WAVE_COLS*3 + i), wcol3);
    }
    vdp_sprite_mag(1);
}

/* step から波頭の画面Y を出す。★二乗＝等加速度。出だしはほとんど動かず(地鳴り)、後半で一気に来る。 */
s16 ovl_crush_wave_y(u8 step) {
    return (s16)((s16)CRUSH_WAVE_Y0 - (s16)(((u16)step * (u16)step) / CRUSH_WAVE_ACC));
}

/* 津波を1フレームぶん置く。
   ★上下へはみ出すコマは 32px ぶんまで負Yのままクリップさせる(退避すると端から食いちぎられて
     見える)。それより外は Y=220 へ退避する(負Yのまま置くと 216=表示停止マーカを踏んで
     以降のスプライトが全部消える)。 */
void ovl_crush_wave(u8 step) {
    s16 base = ovl_crush_wave_y(step);
    u8 r, c, ph = (u8)(step >> 2);   /* 泡の位相(4フレームに1回入替え=波頭がうねって見える) */
    for (r = 0; r < WAVE_ROWS; r++) {
        s16 y0 = (s16)(base + (s16)((u16)r << 5));
        for (c = 0; c < WAVE_COLS; c++) {
            /* ★斜め: 列ごとに CRUSH_WAVE_SHEAR px 下げる。まっすぐだと MSX 感が強い(実機で指摘)。
               ★★傾きには上限がある。段の間隔 32px = SHEAR × 列数(8) のとき、32枚の y が
                 SHEAR px 間隔で**均等**に並び、どの走査線にもちょうど8枚＝制限ぴったりになる。
                 SHEAR=16 にしたら y が偏って制限を超え、しかも段ごとの色帯が 16px の階段になった。
                 波頭のパターンも同じ傾き(2パターン行/コマ)で切ってあるので継ぎ目に段は出ない。 */
            s16 y = (s16)(y0 + (s16)((u16)c * CRUSH_WAVE_SHEAR));
            u8 sl = (u8)((r << 3) + c);
            u8 pt;
            /* ★同じコマを横に並べると 32px ごとに柄が繰り返して見える。2種を列ごとに
               市松で入れ替え、段どうしでも入れ替えて繰り返しを崩す。 */
            if (r == 0)                  pt = wave_pnum[(u8)((c + ph) & 1)];
            else if (r == WAVE_ROWS - 1) pt = wave_pnum[4];
            else                         pt = wave_pnum[(u8)(2 + ((c + r) & 1))];
            if (y < -32 || y > 211) vdp_sprite_pos(sl, 0, 220, pt);
            else                    vdp_sprite_pos(sl, (u8)(c << 5), (u8)y, pt);
        }
    }
}

/* 波を片付ける(クラッシュ終了時)。拡大を戻し、全枚を画面外へ。
   ★色表とボム棒のパターン枠は波が上書きしているので、呼び側が hud_colors() で張り直し、
     敵の色キャッシュ(entity.c)を捨てること。 */
void ovl_crush_wave_off(void) {
    u8 i;
    vdp_sprite_mag(0);
    for (i = 0; i < WAVE_COLS * WAVE_ROWS; i++) vdp_sprite_pos(i, 0, 220, wave_pnum[0]);
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

