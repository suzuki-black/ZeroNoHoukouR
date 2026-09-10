/* scroll.c — 連続縦スクロール地形＋疑似多重スクロール海(旧版 SEA13 移植)。scroll.h 参照。 */
#include "scroll.h"
#include "vdp.h"

#define PAGE1_Y   256           /* 表示リング(page1)の基準Y */

u16 g_cam;
s16 g_scroll_dy;   /* このフレームのスクロール量(new-old, px)。敵弾の艦追従に使う */
static s16 drawn_top, drawn_bot;

/* 海テンプレート用の乱数(旧版と同LCG。斑点の見た目のみ) */
static u16 srng = 12345;
static u8 srnd(void) { srng = (u16)(srng * 25173 + 13849); return (u8)(srng >> 8); }

/* 海テンプレート(512, 256x16)を構築: 色1地＋色2を420点＋色7を320点(継目を隠す粗ノイズ)。 */
void scroll_build_sea(void) {
    u16 k;
    vdp_fill(0, SC_SEATMPL_Y, 256, 16, 1);
    for (k = 0; k < 420; k++) vdp_fill(srnd(), (u16)(SC_SEATMPL_Y + (srnd() & 15)), 1, 1, 2);
    for (k = 0; k < 320; k++) vdp_fill(srnd(), (u16)(SC_SEATMPL_Y + (srnd() & 15)), 1, 1, 7);
}

/* 世界行 r を page1 リングの該当16pxスロットへ。艦行=バッファB, それ以外=海テンプレ。 */
static void draw_row(s16 r) {
    u16 dy = (u16)(PAGE1_Y + (u8)((u16)r << 4));
    if (r >= SC_SHIP_R0 && r < SC_SHIP_R1)
        vdp_copy(0, (u16)(SC_SHIPBUF_Y + (u16)(r - SC_SHIP_R0) * 16), 0, dy, 256, 16);
    else
        vdp_copy(0, SC_SEATMPL_Y, 0, dy, 256, 16);
}

/* 世界行 r0..r1 を表示リングへ再描画(可視域 drawn_top..drawn_bot にクランプ)。
   ★炎をバッファBへ焼込んだ後、その行だけをリングへ正しい位置で反映する用。
     炎を表示リングへ直接 blit すると、リング256px<艦496px のため 256px 離れた砲台に
     エイリアスして炎が乗る不具合が出るため、必ず「B→draw_row(行ごとに正位置)」を経由する。 */
/* ★炎専用の「部分幅」行反映: 世界行 r0..r1 の [x0,x0+w) 帯だけを 艦バッファB→リングへ写す。
   炎は必ず艦上(x≈60..190, 海帯に掛からない)なので艦行のみB経由でコピー=海テンプレ不要。
   従来の scroll_repaint_rows は全幅256pxを塗り直していたが、炎は幅≤32pxなのでコピー面積を約1/8へ。
   ★HMMM(G4=2px/byte)は X/幅の低位1bitを無視するので、x0を偶数へ丸め・幅を右端まで覆う偶数へ拡張。 */
void scroll_repaint_cols(s16 r0, s16 r1, u8 x0, u8 w) {
    u8 x  = (u8)(x0 & 0xFE);                          /* 偶数境界へ */
    u8 ww = (u8)(((u8)(x0 & 1) + w + 1) & 0xFE);      /* [x0,x0+w) を確実に覆う偶数幅 */
    s16 r;
    if (r0 < drawn_top) r0 = drawn_top;
    if (r1 > drawn_bot) r1 = drawn_bot;
    for (r = r0; r <= r1; r++) {
        if (r < SC_SHIP_R0 || r >= SC_SHIP_R1) continue;   /* 炎は艦行のみ(海行に炎は付かない) */
        { u16 dy = (u16)(PAGE1_Y + (u8)((u16)r << 4));
          vdp_copy(x, (u16)(SC_SHIPBUF_Y + (u16)(r - SC_SHIP_R0) * 16), x, dy, ww, 16); }
    }
}

/* ===== SEA13: 海コラムだけ位相流し(艦とその影は不可侵=帯で避ける) ===== */
static u16 sea_phase;
static u8  sea_acc, sea_strip;
/* {startX,width,...} 艦＋影を避けた海コラム(艦種別に手調整)。順=BB/Iowa/Carrier/Hood/Twins。 */
static const u8 sea_bb[4] = { 0, 72, 196, 60 };            /* ビスマルク/アイオワ(半幅54): x0..72 と x196.. */
static const u8 sea_cv[4] = { 0, 66, 190, 66 };            /* 空母(半幅58,対称影) */
static const u8 sea_hd[4] = { 0, 80, 190, 66 };            /* フッド(半幅46) */
static const u8 sea_tw[6] = { 0, 50, 116, 38, 220, 36 };   /* 双子: 左/船間/右の3帯 */
static const u8 sea_full[4] = { 0, 128, 128, 128 };        /* イントロ=全幅(艦がまだ無い→中央も流す) */
static const u8 *sea_ranges = sea_full;
static u8 sea_nranges = 2;

/* イントロ(艦未出現)は全幅アニメで初期化。艦が出たら sea_set_ship で艦回避帯へ切替える。 */
void sea_init(u8 stage) {
    (void)stage;
    sea_ranges = sea_full; sea_nranges = 2;
    sea_phase = 0; sea_acc = 0; sea_strip = 0;
}

/* 艦出現後: 艦とその影を避けた海コラム帯へ切替(艦のx範囲を塗り潰さない)。 */
void sea_set_ship(u8 stage) {
    switch (stage) {
        case 1:  sea_ranges = sea_cv; sea_nranges = 2; break;   /* 空母 */
        case 2:  sea_ranges = sea_hd; sea_nranges = 2; break;   /* フッド */
        case 3:  sea_ranges = sea_tw; sea_nranges = 3; break;   /* 双子(3帯) */
        default: sea_ranges = sea_bb; sea_nranges = 2; break;   /* 0=BB / 4=Iowa */
    }
}

/* ===== SEA13 海コラム位相流し: 「1コピーずつ」発行する分割APIが唯一の実装 =====
   状態(sea_vy/sea_p/sea_si/sea_total)を持ち、sea_begin()で1フレーム準備→sea_step()を必要回数呼ぶ。
   ★H-B対策(炎BG文字化けの構造的封じ): 海の状態進行(sea_strip/sea_phase/sea_acc)と座標計算(source/dest)は
     「この分割APIだけ」が実装する。一括版 sea_frame() も下でこのAPIの一括実行として定義する=状態機械を
     物理的に1本化。これで「一括版」と「分割版(§4-1オーバーラップ)」の間で状態が二重進行したり座標が
     食い違って dest がフォント/艦外へ逸れる(→文字化け)経路を、設計上あり得なくする。
   海コラム塗りは vdp_copy=HMMM(0xD0)。HMMM は LMMM の約2.4倍速(実測 CE待ち 155→64反復/コピー)。
   ★以前 端接地帯を YMMM(0xE0) 化したが残留VDP状態に敏感でソフトリセット後に崩れたため HMMM に戻した。 */
static u16 sea_vy;
static u8  sea_p, sea_si, sea_total;
void sea_begin(void) {
    sea_vy = (u16)(PAGE1_Y + (u16)sea_strip * 16);
    sea_p  = (u8)(sea_phase & 15);
    sea_si = 0;
    sea_total = (u8)(sea_nranges * 2);           /* 各range 2コピー(上+wrap。wrapはp=0で空発行) */
    sea_strip = (u8)((sea_strip + 7) & 15);       /* 歩幅7(16と互素)=掃引を散らす。進行はbeginで1回だけ */
    if (++sea_acc >= 8) { sea_acc = 0; sea_phase += 1; }   /* 0.125px/f */
}
/* 次の1コピーを発行(vdp_copy=HMMM)。上[p..16]＋wrap下[0..p](p=0なら下は空発行)。残1/終0。 */
u8 sea_step(void) {
    u8 ri, part, rx, rw, t;
    if (sea_si >= sea_total) return 0;
    ri = (u8)(sea_si >> 1); part = (u8)(sea_si & 1);
    rx = sea_ranges[2 * ri]; rw = sea_ranges[2 * ri + 1];
    t = (u8)(16 - sea_p);
    if (part == 0) {
        vdp_copy(rx, (u16)(SC_SEATMPL_Y + sea_p), rx, sea_vy, rw, t);      /* 上[p..16] */
    } else if (sea_p) {
        vdp_copy(rx, SC_SEATMPL_Y, rx, (u16)(sea_vy + t), rw, sea_p);      /* 下[0..p](wrap) */
    }
    sea_si++;
    return (u8)(sea_si < sea_total);
}

/* 一括版: 分割APIを1フレーム分まとめて実行するだけ(=状態機械の唯一の実装は sea_begin/sea_step)。
   ★呼ぶ頻度は呼び元(scene_stage)が制御: 戦闘中は毎フレーム、序盤(全幅=最重)は SEA0_DIV 間引き。 */
void sea_frame(void) {
    sea_begin();
    while (sea_step()) { }
}

void scroll_init(void) {
    s16 r;
    /* ★page1 を表示する"前"に、リング窓を描き切り・縦位置も確定させる。
       旧実装は先に display=page1 してから draw_row していたため、page1 に残る
       前ステージの最終フレーム(破壊/炎上画面)が上書きされ切るまでの数十μs、
       画面に露出していた(=ステージ遷移時に一瞬ゴミが見える。実機でも発生)。
       カード(page0)を出したまま page1 を完成させ、最後に一気に切替えることで解消。 */
    /* sea_init(stage) は呼び元(scene_stage)が艦種に合わせて呼ぶ */

    g_cam = SC_CAM_START;
    drawn_top = (s16)(g_cam >> 4);
    drawn_bot = (s16)((g_cam + 211) >> 4);
    for (r = drawn_top; r <= drawn_bot; r++) draw_row(r);   /* page1 リングを新面で満たす(まだ非表示) */
    vdp_set_vscroll((u8)g_cam);       /* 縦位置も新面へ。★flip前に確定=表示した瞬間に窓が正しい */
    /* 最後に page1 を出す。R#23→R#2 の順=両者の隙間(数μs)にフレーム境界が来ても、
       その間見えるのは page0(カード)側だけ。旧ステージの page1 は決して露出しない。 */
    vdp_set_display_page(1);          /* 完成済み page1 を表示=ここでゲーム画面が現れる */
}

void scroll_to(u16 cam) {
    s16 vt = (s16)(cam >> 4);
    s16 vb = (s16)((cam + 211) >> 4);
    while (drawn_bot < vb) { drawn_bot++; draw_row(drawn_bot); if (drawn_top < drawn_bot - 15) drawn_top = drawn_bot - 15; }
    while (drawn_top > vt) { drawn_top--; draw_row(drawn_top); if (drawn_bot > drawn_top + 15) drawn_bot = drawn_top + 15; }
    g_scroll_dy = (s16)cam - (s16)g_cam;   /* ★このフレームのスクロール量(new-old)。敵弾が艦と一緒に流れる為に使う */
    g_cam = cam;
    vdp_set_vscroll((u8)cam);
}
