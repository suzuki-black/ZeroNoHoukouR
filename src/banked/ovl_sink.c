/* ovl_sink.c — 戦艦の撃沈シーン(ROADMAP P2-B / B1)。RAM オーバレイ OVL7_BANK で動く。
   撃破の瞬間に通常面のオーバレイと入れ替えて読み込み、次の面の準備で通常面のものへ戻る。
   ★見下ろし視点の沈没は「**船尾から水に沈み、見えている艦が船首へ向かって短くなって消える**」。艦は動かず、
     画面は前へ進み続ける。(★1943 を見本と書いていたが誤り: 1943 の戦艦は沈まず破片が飛び散るだけ=ユーザー指摘。本作独自の演出)
   ★台本(t は撃沈開始からのフレーム):
       誘爆      0..BLAST_T-1 : 画面を揺らし、艦の全幅に爆発を降らせる
       水没      BLAST_T..    : 水面の線(世界 Y = wl)が船尾から船首へ上がってくる。
                                 線より下の行は海の模様で上書き、すぐ上の数行は海の色を網目で重ねて「濡れた暗さ」、
                                 線には白波、線に沿って爆発。画面はゆっくり前へ進む。
       静まる    船首が沈んだら(または水面が画面の上へ抜けたら) : 泡の名残を見せて結果画面へ
   ★出力は 1 行ずつの HMMM(と網目の透明コピー)だけ。水面が上がった行ぶん＝数行/フレーム。
   ★書くのは「表示リング」と「艦バッファ B」の両方。B も書かないと、炎の描き直し(B→リング)や
     スクロールで露出した行が艦の絵に戻ってしまう。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "sound.h"
#include "scroll.h"
#include "aa_hot.h"     /* cam / rnd */

#define PAGE1_Y   256
#define WET_Y     100    /* 網目の元絵(page0 の非表示行。100=海の色が偶数列 / 101=奇数列) */
#define WET_ROWS  8      /* 水面のすぐ上で網目を重ねる行数 */
#define WAKE_ROWS 6      /* 白波を残す行数(それより下は海に戻す) */
#define BLAST_T   30     /* 誘爆の長さ */
#define CALM_T    16     /* 船首が沈んでから結果画面まで */
#define BOW_Y     (SC_SHIP_R0 * 16)
#define STERN_Y   (SC_SHIP_R1 * 16)

extern u8  burn_left[], burn_sz[], nburn;   /* scene_stage.c の炎上サイト表 */
extern u16 burn_wtop[];
extern u8  burn_coma[];
extern const u8 fb_box[];
extern u8  sink_flash;                      /* ovl_palette.c(OVL_SINK): >0 の間、全画面を白へ寄せる */
void ovl_pal_update(void);
void ovl_pal_reset(void);

static u8  t, calm, sub;   /* sub: 水面の位置の端数(1/4 px) */
static u16 wl;        /* 水面の線の世界 Y(これより下は沈んだ) */
static u8  spd;       /* 水面が上がる速さ(1/4 px/フレーム) */

/* 世界 Y の行 [wy, wy+n) がすべて表示リングに今あるか(リングは 256 行周期。範囲外に書くと 256 上の行を壊す)。
   ★スクロール側が実際に描いた範囲 [drawn_top, drawn_bot] で判定する。以前は cam から「cam&0xFFF0 から 256 行」と
     推定していたが、カメラが上下したあとは描いた範囲がそれより 1〜2 段(16行)上に残っている。すると水面の下の
     「沈んだ海」が 256 上の**まだ画面に出ていない船首寄りの行**のスロットへ書かれ、その行が降りてきたとき
     艦の途中に横長の帯(12行ほど)が抜けた(実機で指摘。openMSX で全5面再現: 帯の行はリングだけ海で B は艦のまま)。
   ★画面の上端より少し上の行もリングに描いてある(16 行単位)ので、そこを書き漏らさないのは従来どおり。 */
static u8 in_ring(u16 wy, u8 n) { return (u8)((s16)(wy >> 4) >= drawn_top && (s16)((u16)(wy + n - 1) >> 4) <= drawn_bot); }

/* 世界 Y の行 [wy, wy+n) へ src の行を 1 回のコマンドで写す(リングと B)。
   ★リングは 256 行で折り返すので、折り返しを跨ぐときだけ 1 行ずつに分ける。 */
static void put_rows(u16 wy, u8 n, u16 src, u8 trans) {
    u8 ry = (u8)wy;
    if (in_ring(wy, n)) {
        if ((u16)ry + n <= 256) {
            if (trans) vdp_copy_t(0, src, 0, (u16)(PAGE1_Y + ry), 256, n);
            else       vdp_copy(0, src, 0, (u16)(PAGE1_Y + ry), 256, n);
        } else {
            u8 i;
            for (i = 0; i < n; i++) put_rows((u16)(wy + i), 1, (u16)(src + (trans ? 0 : i)), trans);
            return;   /* B は 1 行ずつの呼び出しの中で書いた */
        }
    }
    if (wy >= BOW_Y && (u16)(wy + n) <= STERN_Y) {
        if (trans) vdp_copy_t(0, src, 0, (u16)(SC_SHIPBUF_Y + wy - BOW_Y), 256, n);
        else       vdp_copy(0, src, 0, (u16)(SC_SHIPBUF_Y + wy - BOW_Y), 256, n);
    }
}

/* 世界 Y の行 [wy, wy+n) を海の模様に。★海テンプレは 16 行周期なので、折り返さない範囲に分けて写す */
static void sea_rows(u16 wy, u8 n) {
    while (n) {
        u8 k = (u8)(16 - (wy & 15));
        if (k > n) k = n;
        put_rows(wy, k, (u16)(SC_SEATMPL_Y + (wy & 15)), 0);
        wy += k; n -= k;
    }
}

/* 世界 Y の 1 行に海の色を網目で重ねる(色 0 は透明＝半分だけ海の色になる。行ごとに網目をずらす) */
static void wet_row(u16 wy) {
    if (wy < BOW_Y || wy >= STERN_Y) return;
    put_rows(wy, 1, (u16)(WET_Y + (wy & 1)), 1);
}

/* 白波: リングの 1 行に短い白の切れ端を n 本(艦の幅のあたり) */
static void foam(u16 wy, u8 n) {
    if (!in_ring(wy, 1)) return;
    while (n--) {
        u8 x = (u8)(48 + (rnd() % 160));
        vdp_fill(x, (u16)(PAGE1_Y + (u8)wy), (u16)(2 + (rnd() & 7)), 1, (rnd() & 1) ? 15 : 14);
    }
}

/* 水面より下に掛かる炎上サイトを表から外す(外さないと炎の描き直しが海の上に炎を焼く) */
static void cut_burn(u16 lo) {
    u8 i = 0;
    while (i < nburn) {
        if ((u16)(burn_wtop[i] + fb_box[burn_sz[i]]) > lo) {
            nburn--;
            burn_left[i] = burn_left[nburn]; burn_wtop[i] = burn_wtop[nburn];
            burn_sz[i] = burn_sz[nburn];     burn_coma[i] = burn_coma[nburn];
        } else i++;
    }
}

/* 水面より下になった艦上のスプライト(砲台の砲身/空母の停泊機)を消す。★これらは艦の世界座標に貼り付いて
   いるので、放っておくと沈んだ海の上に砲身だけが浮いて残る(実機で報告) */
static void cut_sprites(u16 lo) {
    u8 i;
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        if (e->active && (e->type == ET_TURRET || e->type == ET_PARKED) && (u16)(e->ay + 8) >= lo) e->active = 0;
    }
}

void ovl_sink_init(void) {
    u8 i;
    t = 0; calm = 0; sub = 0; spd = 10;
    ovl_pal_reset();       /* ★static は前のオーバレイの残り物なので、ここで全部決め直す */
    sink_flash = 0;
    /* 網目の元絵を page0 の非表示行へ(1 バイト = 左右 2 ドット。0x10 = 左だけ海の色 1) */
    vdp_write_addr((u16)(WET_Y * 128));
    for (i = 0; i < 128; i++) vdp_data(0x10);
    vdp_write_addr((u16)((WET_Y + 1) * 128));
    for (i = 0; i < 128; i++) vdp_data(0x01);
    /* 水面は画面の下端のすぐ外から始める(それより下は二度と画面に出ない＝沈める必要が無い) */
    wl = (u16)(cam + 212 + WET_ROWS);
    if (wl > STERN_Y) wl = STERN_Y;
}

/* 1 フレームぶん。戻り 1=演出が終わった(結果画面へ) */
u8 ovl_sink_frame(void) {
    ovl_pal_update();

    if (t < BLAST_T) {                                   /* ---- 誘爆 ---- */
        vdp_set_vscroll((u8)((s16)cam + (s16)(rnd() % 7) - 3));
        if ((t % 3) == 0) ent_spawn_explosion((s16)(80 + (rnd() % 96)), (s16)(20 + (rnd() % 172)));
        if ((t % 12) == 0) sfx(2, SFX_BOOM);
        if (++t == BLAST_T) { sink_flash = 3; sfx(2, SFX_THUNDER); g_rumble_lv = 10; }
        return 0;
    }

    /* 画面はゆっくり前へ(艦は止まったまま下へ流れていく)。静まってからは止める */
    if (!calm) { if (cam) cam--; scroll_to(cam); }

    if (!calm) {
        u16 nw;
        /* 水面が上がる: 最初はゆっくり、だんだん速く(2.5→5px/フレーム。画面が 1px 進むぶんを引いた速さで画面上を上がる)。
           ★撃沈中は炎と爆発が多く 15〜20fps まで落ちる(openMSX 実測)ので、フレームあたりの量は大きめに取る */
        if ((t & 7) == 0 && spd < 20) spd++;
        sub = (u8)(sub + spd);
        nw = (u16)(wl - (sub >> 2)); sub &= 3;
        if (nw < BOW_Y) nw = BOW_Y;
        if (nw < wl) {
            u8 k = (u8)(wl - nw);
            wl = nw;
            sea_rows(wl, k);                            /* 沈んだ行 */
            sea_rows((u16)(wl + WAKE_ROWS), k);         /* 白波の名残を消す */
            while (k--) wet_row((u16)(wl - WET_ROWS + k));   /* 網目の帯に入った行 */
        }
        foam(wl, 3);
        cut_burn(wl);
        cut_sprites(wl);

        /* 水面の線に沿って爆発と水しぶき(同じ横一列に並ぶとスプライト 8 枚制限に掛かるので控えめに) */
        if (wl > cam && wl < (u16)(cam + 212)) {
            u8 sy = (u8)(wl - cam);
            if ((t & 3) == 0) ent_spawn_explosion((s16)(64 + (rnd() % 128)), (s16)(sy - 12 - (rnd() & 7)));
            if ((t % 7) == 0) ent_spawn_spark((s16)(56 + (rnd() % 144)), (s16)(sy - 4));
        }
        if ((t % 20) == 0) sfx(2, SFX_BOOM);
        else if ((t % 16) == 8) sfx(2, SFX_RUMBLE);
        if (g_rumble_lv > 3 && (t & 15) == 0) g_rumble_lv--;

        /* 船首まで沈んだ、または水面が画面の上端に着いた(それより上は画面に出てこない＝待つ意味が無い)
           ★以前は「上端の 12 ドット手前」で切り上げていたが、そのとき**まだ見えている 12 ドットぶんの艦**が
             次の 1 フレームで下の一括処理にまとめて消され、「船首の少し下が縦 10〜20 ドット丸ごと欠ける」
             ように見えた(実機で指摘)。見えている間は沈め続け、上端に着いてから片付ける。 */
        if (wl <= BOW_Y || wl <= cam) {
            /* ★水面より上の網目の帯と、画面上端より上に描いてある行もまとめて海へ。残すと、網目で甲板だけが
               海の色に紛れ、暗い砲塔のドームがゴミのように浮いて見えた(実機で報告) */
            u16 top = (u16)(cam & 0xFFF0);
            if (top > (u16)(wl - WET_ROWS)) top = (u16)(wl - WET_ROWS);
            if (top < BOW_Y) top = BOW_Y;
            if (wl > top) sea_rows(top, (u8)(wl - top));
            wl = top;
            sfx(2, SFX_BOOM);
            g_rumble_lv = 0;
            calm = 1;
        }
        t++;
        return 0;
    }

    /* ---- 静まる: 沈んだ所に泡がしばらく残る ---- */
    if (calm < 8) foam((u16)(wl + (rnd() & 7)), 2);
    /* ★泡は海の上に描いた点なので、消さないと結果画面へ移るまで船首のあった所にゴミとして残る(実機で報告)。
       静まりの後半で、白波が残りうる範囲(水面〜白波の行＋泡の散る 8 行)を海に戻す */
    else if (calm == 8) sea_rows(wl, (u8)(WAKE_ROWS + 8));
    if (++calm >= CALM_T) return 1;
    return 0;
}
