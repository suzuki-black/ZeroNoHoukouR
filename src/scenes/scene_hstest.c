/* scene_hstest.c — ★実機検証用: 走査線の途中で横スクロール(R#26/R#27)を書き換えられるか。
   `make clean && make HSTEST=1` のときだけ起動シーンとしてリンクされる(通常 ROM には入らない)。

   ★目的: 中ボス案(3面の「うねる海」/4面の霧の揺らぎ/2面の夕焼けグラデーション)の前提を実機で確かめる。
     Grauw の screensplit guide は R#23(縦)しか扱っておらず、**横スクロールの途中書き換えは資料が無い**。
   ★レジスタ(V9958 データブック/Portar tech doc で確認):
     R#25 bit0 SP2 = 1画面/2画面, bit1 MSK = 左端8ドットを隠す, bit2 WTE, bit3 YJK, bit4 YAE
     R#26 = 画面を左へ「8ドット単位」で送る / R#27 = 画面を右へ「1ドット単位」で送る
     つまり 見かけの横位置 = -(R#26*8) + R#27。本作の蛇行(apply_weave)と同じ式をここでも使う。
   ★方式: 既存のラスタ分割(raster.c: R#19＋S#1 の FH)にそのまま乗せる。1本の分割で R#26 と R#27 の
     2本を書けるので、帯ごとに横位置を変えられるはず。分割は 12 本・間隔 12 行以上(実測の制約)。
   ★見るところ:
     1. 帯ごとに背景が横へずれて sin 波にうねるか。継ぎ目が1〜2行ずれる/ちらつく/化けることは無いか。
     2. 左端8ドット(MSK)。MSK=0 だと横に送った分の左端に何が出るか。MSK=1 で隠れるか。
     3. スプライト(下段の4枚)が揺れないか＝横スクロールはスプライトに効かない、の確認。
     4. 粗(R#26)だけ / 微(R#27)だけ / 両方、で継ぎ目の出方が変わるか。
   操作: SPACE=モード切替  上下=振幅±2  左右=分割の本数±1 */
#include "vdp.h"
#include "input.h"
#include "scene.h"
#include "raster.h"

#define BANDS_MIN 2
#define BANDS_MAX 11      /* 先頭(line0)＋分割11本=12本(RAS_MAX) */

static u8 mode;      /* 0=粗+微 / 1=微だけ(±7) / 2=粗だけ(8ドット単位) / 3=分割なし(全画面を同じ量で) */
static u8 bands;     /* 分割の本数(帯の数=bands+1) */
static u8 amp;       /* 振幅(ドット) */
static u8 msk;       /* R#25 の MSK */
static u8 t;

/* sin 1周期を 16 段で(±64 を 1/64 で正規化)。帯ごとの横位置＝amp * sin16[i] / 64 */
static const s8 sin16[16] = { 0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24 };
static const char *const mode_name[6] = { "COARSE+FINE", "FINE ONLY  ", "COARSE ONLY", "NO SPLIT   ", "FROZEN WAVE", "2BAND STATIC" };

/* 市松と縦縞。横にずれたことが一目で分かる絵。 */
static const u8 pat_box[32] = {
    0xFF,0xFF, 0x81,0x81, 0xBD,0xBD, 0xA5,0xA5, 0xA5,0xA5, 0xBD,0xBD, 0x81,0x81, 0xFF,0xFF,
    0xFF,0xFF, 0x81,0x81, 0xBD,0xBD, 0xA5,0xA5, 0xA5,0xA5, 0xBD,0xBD, 0x81,0x81, 0xFF,0xFF,
};

static void num3(u8 x, u8 y, u8 v) {
    char s[4];
    s[0] = (char)('0' + v / 100); s[1] = (char)('0' + (v / 10) % 10); s[2] = (char)('0' + v % 10); s[3] = 0;
    vdp_text(x, y, 15, 1, s);
}

/* 背景: 8 ドットごとの縦線＋16 行ごとの横線＋斜めの線。横のずれも継ぎ目も読める絵にする。 */
static void draw_bg(void) {
    u8 x, y;
    vdp_fill(0, 0, 256, 212, 1);                                            /* 地=暗い青 */
    for (x = 0; x < 16; x++) {                                              /* 16 ドットごとの白い縦線。64 ごとは橙 */
        vdp_fill((u16)(x * 16), 0, 2, 180, (u8)((x & 3) ? 15 : 12));
    }
    for (y = 0; y < 180; y = (u8)(y + 16)) vdp_fill(0, y, 256, 1, 7);       /* 16 行ごとの横線(帯の目安) */
    for (y = 0; y < 180; y++) vdp_fill((u16)((y + 20) & 255), y, 2, 1, 11); /* 斜めの赤線(継ぎ目の段差が読める) */
    vdp_text(2, 2, 15, 1, "R#26/27 SPLIT TEST");
    vdp_text(2, 182, 14, 1, "SPACE:MODE UD:AMP LR:BANDS M:MSK");
    if (mode == 5) vdp_text(2, 170, 11, 1, "LINE106 -> RIGHT 32DOT");
    vdp_text(2, 192, 15, 1, "MODE");
    vdp_text(42, 192, 12, 1, mode_name[mode]);
    vdp_text(140, 192, 15, 1, "AMP");   num3(172, 192, amp);
    vdp_text(200, 192, 15, 1, "N");     num3(212, 192, (u8)(bands + 1));
    vdp_text(2, 202, 15, 1, msk ? "MSK=1 HIDE LEFT 8" : "MSK=0 SHOW LEFT 8");
}

/* 見かけの横位置 sh(+で右へ)を R#26/R#27 へ。式は scene_stage.c の apply_weave と同じ。 */
/* ★R#26 は 1画面モード(SP2=0)では 0..31。bit5 を立てるとページが変わって画面が消える(実際に踏んだ) */
static u8 hs_coarse(s8 sh) { u8 s = (u8)(-sh); return (u8)(((s >> 3) + ((s & 7) ? 1 : 0)) & 0x1F); }
static u8 hs_fine(s8 sh)   { u8 s = (u8)(-sh); return (u8)((8 - (s & 7)) & 7); }

static void arm(void) {
    u8 i, n;
    if (mode == 3) {                       /* 分割なし: 全画面を同じ量で揺らす(基準。継ぎ目が出ないことの確認) */
        raster_off();
        return;
    }
    if (mode == 5) {
        /* ★切り分け用: 帯は2つだけ・動かさない。上=ずらさない / 画面中央(106行)から下=右へ 32 ドット。
           実機での見え方で原因が分かる:
             (a) 上下で段差 … 走査線の途中で効く(＝帯ごとの演出が作れる)
             (b) 画面全体が 32 ドットずれる … レジスタはフレーム単位で読まれる(最後に書いた値が次のフレーム全体に効く)
             (c) 何も動かない … 画面の途中の書き込みは無視される */
        g_ras[0].line = 0;   g_ras[0].reg = 26; g_ras[0].val = 0;               g_ras[0].reg2 = 27; g_ras[0].val2 = 0;
        g_ras[0].pidx = RAS_NOPAL;
        g_ras[1].line = 106; g_ras[1].reg = 26; g_ras[1].val = hs_coarse(32);   g_ras[1].reg2 = 27; g_ras[1].val2 = hs_fine(32);
        g_ras[1].pidx = RAS_NOPAL;
        raster_arm(2);
        return;
    }
    for (i = 0; i <= bands; i++) {
        s8 sh = (s8)(((s16)amp * sin16[(u8)((i * 2 + ((mode == 4) ? 0 : t / 4)) & 15)]) / 64);
        u8 c = hs_coarse(sh), f = hs_fine(sh);
        if (mode == 1) { c = 0; f = (u8)((8 - ((u8)(-sh) & 7)) & 7); }      /* 微だけ(0..7ドット) */
        if (mode == 2) { f = 0; c = (u8)(((u8)(-sh) >> 3) & 0x1F); }        /* 粗だけ(8ドット単位) */
        g_ras[i].line = (u8)(i * (u8)(176 / (bands + 1)));                   /* 等間隔(先頭は 0=VBLANK 適用)。下の文字帯(180〜)は揺らさない */
        g_ras[i].reg  = 26; g_ras[i].val  = c;
        g_ras[i].reg2 = 27; g_ras[i].val2 = f;
        g_ras[i].pidx = RAS_NOPAL;
    }
    n = (u8)(bands + 1);
    raster_arm(n);
}

static void hstest_init(void) {
    u8 i;
    vdp_set_vscroll(0);
    vdp_set_display_page(0);
    vdp_sprite_init();
    vdp_sprite_pattern(0, pat_box);
    for (i = 0; i < 4; i++) {            /* 下段のスプライト4枚: 横スクロールで動かないことの確認用 */
        vdp_sprite_color((u8)i, 15);
        vdp_sprite_pos((u8)i, (u8)(32 + i * 48), 160, 0);
    }
    vdp_sprite_hide_from(4);
    mode = 0; bands = 11; amp = 8; msk = 0; t = 0;
    vdp_wreg(25, 0x00);
    draw_bg();
    arm();
}

static u8 hstest_update(void) {
    t++;
    if (g_input_edge & INP_TRIG)  { mode = (u8)((mode + 1) % 6); draw_bg(); }
    if (g_input_edge & INP_TRIGB) { msk = (u8)(msk ^ 1); vdp_wreg(25, (u8)(msk ? 0x02 : 0x00)); draw_bg(); }  /* R#25 bit1=MSK */
    if ((g_input_edge & INP_UP)    && amp < 32) { amp = (u8)(amp + 2); draw_bg(); }
    if ((g_input_edge & INP_DOWN)  && amp > 2)  { amp = (u8)(amp - 2); draw_bg(); }
    if ((g_input_edge & INP_RIGHT) && bands < BANDS_MAX) { bands++; draw_bg(); }
    if ((g_input_edge & INP_LEFT)  && bands > BANDS_MIN) { bands--; draw_bg(); }
    if (mode == 3) {                     /* 分割なし: 全画面を1つの値で(基準) */
        s8 sh = (s8)(((s16)amp * sin16[(u8)((t / 4) & 15)]) / 64);
        vdp_set_hscroll(hs_coarse(sh), hs_fine(sh));
    } else arm();
    return SCENE_NONE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) hstest_init();
    else g_scene_ret = hstest_update();
}
