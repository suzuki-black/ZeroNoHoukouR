/* ovl_crack.c — 2面(米空母 ESSEX)の轟沈: 飛行甲板が縦に裂け、左右へ開いて海に呑まれる。
   撃沈オーバレイ(OVL7)に同居し、撃破演出の最後に一度だけ呼ばれて数秒ブロックする。

   ★面ごとに絵を変える方針。1面は「上から下へ消えながら火の粉と破片」(ovl_part)、3面は
     「きりもみ急上昇」(ovl_spin)なので、2面は **縦に裂けて左右へ開く**=消え方の向きが直交する。
     空母の飛行甲板には中心線が引いてあるので、そこが裂けるのは絵として分かりやすい。
   ★台本(t は開始からのフレーム):
       0..T_ZIP      中心線に沿って黒い亀裂が艦首から艦尾へ走る(LMMV の塗りだけ)
       ..T_OPEN      亀裂が左右へ広がる=甲板が海に呑まれる(海テンプレートからの HMMM)
       ..SEQ_END     取りこぼしを端まで掃いて、炎と煙だけが残る
   ★帯域: 消去は「縦の帯」を 16 行ずつの塊で写す。海テンプレートは 16 行周期なので、
     塊の先頭を周期に合わせれば 1 コマンドで 16 行ぶん写せる(1 行ずつ 212 回やると重い)。
   ★オーバレイの static は 0xEE00-0xEEFF の 256B しか無く、既に埋まりかけている。
     炎と煙の状態は演出中だけ借りる hot_ram(0xC600〜)に置く。 */
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "overlay.h"
#include "scroll.h"
#include "aa_hot.h"       /* curstage */
#include "stage_grade.h"  /* 面ごとの時間帯・天候の基準パレット(2面=夕焼け) */
#include "spinfx.h"       /* 同じバンクの 3面・5面の演出と共有する */
#include "debart.h"       /* 破片の絵と色(4面の ovl_part.c と共有) */

#define FX      ((u8 *)0xC600)     /* 炎と煙 16 枚 × 4B: x, y, 残り寿命, ポーズ */
#define FX_N    16
#define FX_FIRE 8                  /* 前半 8 枚が炎、後半 8 枚が煙(色表がスロット固定なので分ける) */
#define FX_SZ   4
#define SMK_RIM 16                 /* 煙の「縁」のパターン番号(16,20)。本体は 8,12 */
#define DB_PAT  24                 /* 破片のパターン番号の起点(炎 0..7 / 煙 8..23) */

#define CX       128               /* 艦の中心線(撃破時に蛇行の横スクロールは 0 に戻されている) */
#define OPEN_MAX 68                /* 片側の開き。艦の半幅は最大 58(空母) */
#define T_ZIP    16
#define T_OPEN   (T_ZIP + 44)
#define SEQ_END  (T_OPEN + 20)

/* 16x16(4 つの 8x8 = 32B)。左半分 16 行 → 右半分 16 行。パターン番号は 4 の倍数。 */
static const u8 fx_pat[6][32] = {
    {   /* 炎の舌(割れ目から噴く) */
        0x02,0x07,0x07,0x0F,0x0F,0x1F,0x1F,0x3F,0x3F,0x7F,0x7F,0x7B,0x71,0x20,0x00,0x00,
        0x00,0x08,0x1C,0x9C,0xDC,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xDE,0x8E,0x84,0x00,0x00 },
    {   /* 同・揺らぎ */
        0x10,0x38,0x38,0x7C,0x7E,0x7F,0x3F,0x3F,0x7F,0x7F,0x7D,0x78,0x30,0x10,0x00,0x00,
        0x08,0x0C,0x1C,0x3C,0x7C,0xFC,0xFC,0xFC,0xFE,0xFE,0xDE,0x8E,0x44,0x00,0x00,0x00 },
    {   /* 黒煙の塊 */
        0x07,0x1F,0x3F,0x7F,0x7F,0xFF,0xFF,0x7F,0x7F,0x3F,0x1F,0x07,0x00,0x00,0x00,0x00,
        0x80,0xF0,0xF8,0xFC,0xFE,0xFF,0xFF,0xFE,0xFE,0xFC,0xF0,0xC0,0x00,0x00,0x00,0x00 },
    {   /* 同・散り際 */
        0x00,0x01,0x07,0x1F,0x3F,0x3F,0x1F,0x07,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0xC0,0xF0,0xF8,0xF8,0xF8,0xF0,0xE0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* 黒煙の塊: 縁(明るい側) */
        0x07,0x1C,0x30,0x60,0x40,0xC0,0x80,0x00,0x40,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x80,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* 同・散り際: 縁(明るい側) */
        0x00,0x01,0x07,0x1C,0x30,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0xC0,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
};

/* 行ごとの色。炎は白い芯 → 橙 → 赤、煙は淡灰 → 灰 → 黒、煙の縁は光が当たった側。
   ★煙も 2 枚重ねにした(1 枚だと平らな塊に見える=ユーザー指摘)。数は 8 → 4 個に減らし、
     枠(8〜15)はそのまま使う。 */
static const u8 fx_col[3][16] = {
    { 15,15,15,12,12,12,12,11, 11,11,11,11,11,13,13,13 },   /* 炎 */
    { 14,14, 5, 5, 4, 4, 5, 5,  4, 4,13,13,13,13,13,13 },   /* 煙(本体) */
    { 15,14,14,14, 5, 5, 5, 4,  4, 4,13,13,13,13,13,13 },   /* 煙(縁。上が明るい) */
};

static u8  vs;        /* 演出中の縦スクロール(画面行 → リング行 の変換に使う) */
static u16 seed;

static u16 rnd16(void) { seed = (u16)(seed * 25173 + 13849); return seed; }

/* 画面の縦帯 [x, x+w) を海に戻す。★テンプレートは 16 行周期なので、周期の切れ目で区切れば
   1 コマンドで最大 16 行ぶん写せる。リング(256 行)の折返しは跨がないように切る。
   ★HMMM(高速コピー)は **バイト単位**。SCREEN5 は 1 バイト 2 ドットなので、x や幅が奇数だと
     端の 1 ドットが写らず、海の上に細い縦縞が残る(実際に踏んだ)。偶数へ丸めてから出す
     (余分に消すのは海を海で塗るだけなので無害)。 */
static void sea_strip(u8 x, u8 w) {
    u8 y = 0;
    if (x & 1) { x--; w++; }
    if (w & 1) w++;
    while (y < 212) {
        u8 r  = (u8)(vs + y);
        u8 ph = (u8)(r & 15);
        u8 n  = (u8)(16 - ph);
        if ((u8)(y + n) > 212) n = (u8)(212 - y);
        if ((u16)r + n > 256)  n = (u8)(256 - r);
        vdp_copy(x, (u16)(SC_SEATMPL_Y + ph), x, (u16)(256 + r), w, n);
        y = (u8)(y + n);
    }
}

/* 画面行 [y, y+n) の帯を色 c で塗る(亀裂)。リングの折返しで 2 回に割る。 */
static void seam(u8 y, u8 n, u8 x, u8 w, u8 c) {
    u8 r = (u8)(vs + y), n1 = n;
    if ((u16)r + n > 256) n1 = (u8)(256 - r);
    vdp_fill(x, (u16)(256 + r), w, n1, c);
    if (n1 < n) vdp_fill(x, 256, w, (u8)(n - n1), c);
}

static void fx_init(void) {
    u8 i;
    u8 *p = FX;
    vdp_sprite_init();
    for (i = 0; i < 6; i++) vdp_sprite_pattern((u8)(i * 4), fx_pat[i]);   /* 炎2・煙本体2・煙の縁2 */
    for (i = 0; i < FX_N; i++, p += FX_SZ) {
        p[2] = 0;
        /* 炎は 1 枚(枠 0..7)。煙は 2 枚 1 組で、偶数枠が前(縁)・奇数枠が後(本体)。 */
        if (i < FX_FIRE)          vdp_sprite_color_tab(i, fx_col[0]);
        else if ((i & 1) == 0)    vdp_sprite_color_tab(i, fx_col[2]);
        else                      vdp_sprite_color_tab(i, fx_col[1]);
    }
    vdp_sprite_hide_from(0);
}

/* 炎の置き場所。裂け目の縁に貼り付くが、一列に並ぶと機械的に見えるので横にばらす。 */
static u8 flame_x(u8 i, u8 half, u8 jit) {
    u8 d = (u8)((jit >> 4) & 7);                                   /* 0..7 ドットのゆらぎ */
    return (u8)((i & 1) ? (CX + half - 4 + d) : (CX - half - 12 - d));
}

/* 割れ目から炎を噴かせる。煙は炎が消えた場所から立つ。 */
static void fx_spawn(u8 zip, u8 half) {
    u8 i;
    u8 *p = FX;
    u16 r = rnd16();
    for (i = 0; i < FX_FIRE; i++, p += FX_SZ) {
        if (p[2]) continue;
        p[3] = (u8)(r & 0xF0);          /* 上位 4bit = 縁からの横のゆらぎ(下位はポーズの数) */
        p[0] = flame_x(i, half, p[3]);
        p[1] = (u8)(((u8)(r >> 8)) & 0x7F);                          /* 亀裂が走ったところまで */
        if (p[1] > zip) p[1] = (u8)(zip >> 1);
        p[2] = (u8)(10 + (r & 7));
        return;
    }
}

/* ★煙は 2 枚 1 組(偶数枠=前の縁 / 奇数枠=後の本体)。状態は偶数枠にだけ持つ。 */
static void fx_smoke(u8 x, u8 y) {
    u8 i;
    u8 *p = FX + FX_FIRE * FX_SZ;
    for (i = FX_FIRE; i < FX_N; i += 2, p += FX_SZ * 2) {
        if (p[2]) continue;
        p[0] = x; p[1] = y; p[2] = (u8)(14 + (rnd16() & 7)); p[3] = 0;
        return;
    }
}

/* 1 フレーム進める。炎は裂け目の縁に貼り付いたまま揺らぎ、煙は外へ流れて散る。 */
static void fx_step(u8 half) {
    u8 i;
    u8 *p = FX;
    for (i = 0; i < FX_N; i++, p += FX_SZ) {
        if (i >= FX_FIRE && (i & 1)) continue;          /* 煙の後ろの枠は前の枠が面倒を見る */
        if (!p[2]) {
            vdp_sprite_pos(i, 0, 216, 0);
            if (i >= FX_FIRE) vdp_sprite_pos((u8)(i + 1), 0, 216, 0);
            continue;
        }
        p[3] = (u8)((p[3] & 0xF0) | ((p[3] + 1) & 0x0F));   /* 下位 4bit だけ回す(上位はゆらぎ) */
        if (--p[2] == 0) {
            if (i < FX_FIRE) fx_smoke(p[0], p[1]);      /* 炎が尽きたら黒煙になる */
            vdp_sprite_pos(i, 0, 216, 0);
            if (i >= FX_FIRE) vdp_sprite_pos((u8)(i + 1), 0, 216, 0);
            continue;
        }
        if (i < FX_FIRE) {
            /* ★炎は割れ目の縁に貼り付いたまま、舐めるように上下へ揺れる。
               同じ場所で絵を切り替えるだけだと「点滅している四角」に見える。 */
            p[0] = flame_x(i, half, p[3]);                               /* 開くのに合わせて外へ */
            if ((p[3] & 3) == 0 && p[1] > 2) p[1]--;                     /* じりじり上へ舐める */
            vdp_sprite_pos(i, p[0], p[1], (u8)((p[3] & 2) ? 4 : 0));
        } else {
            /* ★煙は直線に流さない(ユーザー指摘)。外へ流れる勢いは寿命とともに鈍り、
               左右に揺れながら、落ちる速さも半分になる＝弧を描いて散る。 */
            u8 ph = (u8)(p[3] & 7);
            s8 dx = (s8)(((ph == 1) || (ph == 2)) ? 1 : (((ph == 5) || (ph == 6)) ? -1 : 0));  /* 揺れ */
            if (p[2] > 8) dx = (s8)(dx + ((p[0] < CX) ? -1 : 1));        /* 若いうちは外へも流れる */
            p[0] = (u8)(p[0] + dx);
            if ((p[3] & 1) && p[1] < 200) p[1]++;                        /* 落ちるのは 2 フレームに 1 */
            {   u8 pat = (u8)((p[2] < 6) ? 12 : 8);                      /* 散り際は小さく */
                vdp_sprite_pos(i, p[0], p[1], (u8)(SMK_RIM + pat - 8));  /* 前(縁) */
                vdp_sprite_pos((u8)(i + 1), p[0], p[1], pat); }          /* 後(本体) */
        }
    }
}

/* ───────── 割れ目から飛ぶ破片(甲板の板きれ・鋼材) ─────────
   ★2面にも本物の破片を出す。1 個につきスプライト 2 枚(前=縁と焼け跡 / 後=本体)で、
     行ごとに色が変わる(絵と色表は debart.h。4面と同じもの)。
     枠は炎・煙の後ろ(16〜)を使う。16 + 6*2 = 28 枚で 32 枚に収まる。 */
#define DB_N    6                     /* 破片の数(スプライトは 2 倍) */
#define DB_SLOT FX_N                  /* 破片が使うスプライト枠の先頭 */
static u8 dbx[DB_N], dby[DB_N], dlive[DB_N];
static s8 dvx[DB_N], dvy[DB_N];

static void db_init(void) {
    u8 i, k = 0;
    for (i = 0; i < DEB_KIND * 2; i++) {
        vdp_sprite_pattern((u8)(DB_PAT + i * 4), deb_rim[i]);
        vdp_sprite_pattern((u8)(DB_PAT + DEB_PATB + i * 4), deb_body[i]);
    }
    for (i = 0; i < DB_N; i++) {
        dlive[i] = 0;
        vdp_sprite_color_tab((u8)(DB_SLOT + i * 2),     col_rim[k]);
        vdp_sprite_color_tab((u8)(DB_SLOT + i * 2 + 1), col_body[k]);
        if (++k >= DEB_KIND) k = 0;
    }
}

/* 割れ目の縁から 1 個。half=いまの開き。 */
static void db_spawn(u8 half, u8 zip) {
    u8 i;
    for (i = 0; i < DB_N; i++) {
        u16 r;
        if (dlive[i]) continue;
        r = rnd16();
        dbx[i] = (u8)((i & 1) ? (CX + half - 8) : (CX - half - 8));
        dby[i] = (u8)(((u8)(r >> 8)) % ((zip > 24) ? zip : 24));
        dvx[i] = (s8)((i & 1) ? (2 + (r & 3)) : (s8)(-2 - (s8)(r & 3)));   /* 割れ目から外へ */
        dvy[i] = (s8)(((r >> 4) & 7) - 6);                                  /* 上へ跳ね上がる */
        dlive[i] = (u8)(0x80 | (r & 0x0F));
        return;
    }
}

static void db_step(void) {
    u8 i, kind = 0;
    for (i = 0; i < DB_N; i++) {
        u8 d = dlive[i], k = kind;
        if (++kind >= DEB_KIND) kind = 0;
        if (!d) { vdp_sprite_pos((u8)(DB_SLOT + i * 2), 0, 216, 0); vdp_sprite_pos((u8)(DB_SLOT + i * 2 + 1), 0, 216, 0); continue; }
        {
            u8  ph = (u8)(d & 7);
            s16 nx = (s16)((s16)dbx[i] + dvx[i]);
            s16 ny = (s16)((s16)dby[i] + dvy[i]);
            if (ph & 1) dvy[i]++;                                  /* 重力(ゆっくり) */
            if (!ph) dvx[i] = (s8)(dvx[i] - (dvx[i] >> 2));        /* 横は鈍る */
            if (nx < 2 || nx > 248 || ny > 205) {
                dlive[i] = 0;
                vdp_sprite_pos((u8)(DB_SLOT + i * 2), 0, 216, 0); vdp_sprite_pos((u8)(DB_SLOT + i * 2 + 1), 0, 216, 0);
                continue;
            }
            if (ny < 0) ny = 0;
            dbx[i] = (u8)nx; dby[i] = (u8)ny;
            { u8 tm = ph;
              if (tm) tm--; else { d ^= 0x08; tm = (u8)(3 + (i & 3)); }
              dlive[i] = (u8)((d & 0xF8) | tm); }
            {   u8 pat = (u8)(DB_PAT + (((k << 1) + ((d & 0x08) ? 1 : 0)) << 2));
                vdp_sprite_pos((u8)(DB_SLOT + i * 2),     dbx[i], dby[i], pat);
                vdp_sprite_pos((u8)(DB_SLOT + i * 2 + 1), dbx[i], dby[i], (u8)(DEB_PATB + pat));
            }
        }
    }
}

static void white_pal(void) { u8 i; for (i = 0; i < 16; i++) vdp_set_pal(i, 7, 7, 7); }

/* ★閃光から戻すときは **その面の** パレットへ戻すこと。vdp_palette_game() は 1 面(昼)の色なので、
   2 面(夕焼け)でこれを呼ぶと海が昼の青に戻ってしまう(実際に踏んだ)。
   ★同じ理由で SCREEN3 の演出(3面・5面)も CHGMOD の後にこれを呼ぶ。バンク内で共有する。 */
void spinfx_stage_pal(void) {
    u8 i, s = (u8)((curstage < 5) ? curstage : 0);
    for (i = 0; i < 16; i++) {
        const u8 *c = pal_stage[s][i];
        vdp_set_pal(i, c[0], c[1], c[2]);
    }
}

/* 撃破演出の最後に常駐から呼ばれる(OVL_SLOT_CRACK)。戻るまで数秒ここに居る。 */
void ovl_crack(void) {
    u16 t;
    u8 zip = 0, half = 0, sweep = 0;
    vs   = g_vscroll;
    seed = 0x3179;
    raster_off();
    fx_init();
    db_init();
    white_pal();                 /* ★閃光はパレットだけ=帯域ゼロ */
    vdp_wait_frame();
    vdp_wait_frame();
    spinfx_stage_pal();
    for (t = 0; t < SEQ_END; t++) {
        if (t < T_ZIP) {                                  /* 亀裂が走る */
            u8 ny = (u8)(((u16)(t + 1) * 212) / T_ZIP);
            while (zip < ny) {
                u8 n = (u8)(((ny - zip) > 8) ? 8 : (ny - zip));
                u8 w = (u8)(2 + (rnd16() & 2));           /* 幅を揺らして一直線に見せない */
                seam(zip, n, (u8)(CX - w), (u8)(w * 2), 13);
                zip = (u8)(zip + n);
            }
        } else if (t < T_OPEN) {                          /* 左右へ開く */
            u8 nh = (u8)(half + 2);
            if (nh > OPEN_MAX) nh = OPEN_MAX;
            if (nh > half) {
                u8 jag = (u8)((rnd16() & 3) << 1);        /* 縁をぎざぎざに(偶数。余分に消すのは無害) */
                sea_strip((u8)(CX - nh - jag), (u8)((nh - half) + jag));
                jag = (u8)((rnd16() & 3) << 1);
                sea_strip((u8)(CX + half), (u8)((nh - half) + jag));
                half = nh;
            }
        } else if (sweep < 4) {                           /* ★端に艦が残らないよう最後に掃く */
            sea_strip((u8)(40 + sweep * 44), 44);
            sweep++;
        }
        vdp_cmd_wait();          /* ★コマンドの完了待ち。走っている間にスプライト表を叩かない */
        if (t < T_OPEN) { fx_spawn(zip, half); if ((t & 3) == 0) db_spawn(half, zip); }
        fx_step(half);
        db_step();
        vdp_wait_frame();
    }
    vdp_sprite_hide_from(0);
}
