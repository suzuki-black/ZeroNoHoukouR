/* ovl_part.c — 1面の轟沈: 火の粉と破片(パーティクル)。撃沈オーバレイ(OVL7)に同居する。

   ★画面は SCREEN5 のまま(精緻)。粒だけを背景へ差分描きする。
     SCREEN3(1ブロック 4x4 ドット)で 400 粒出す版も作ったが、火の粉に画面を粗くする理由は無い。
   ★最初の版は「2x1 ドットの粒を 260 個」だったが、細かすぎて**艦が消えただけに見えた**
     (ユーザー指摘)。粒を 2x2 ドットに太らせ、噴出を山なりの塊にし、白フラッシュと
     ぎざぎざの崩れ方を足して「爆ぜて散る」絵にする。

   ★速度の勘所: 粒 1 個につき VRAM の番地設定(ポート4回=約20µs)が要る。2x2 は 2 行なので
     消去 2 回＋描画 2 回 = 4 回。turboR 実測で **2x2 は約 200 粒 / 2x1 は約 320 粒が 30fps**。
   ★消去は「海テンプレートの同じ位置の色」で塗り戻す(VRAM を読み戻すと番地設定が倍になる)。
     テンプレート 16 行(2KB)を RAM へ写して引く。
   ★RAM は hot_ram(演出中だけ借りる。終わったら常駐が hot_load() で戻す):
     0xC600 海テンプレート 2,048B(-0xCDFF) / 0xCE00 粒 8B×200(-0xD43F) / 0xD500 内側ループ(331B)
   ★粒を 6B から 8B(前回描いた位置 px,py を持つ)に広げたとき、粒の置き場が 0xD43F まで伸びて
     **内側ループのコード(当時 0xD400)を上書きしていた**。演出が数フレーム進んで粒が 192 個を
     超えた瞬間に RAM のコードが壊れ、機械がリセットする(1面クリアでタイトルに戻る)。
     置き場を動かすときは「粒の終端 = PRT + PRT_N*PRT_SZ」と CODE の間隔を必ず数えること。
   ★粒 1 個 8B: x(8.8) y(8.8) vx(s8) vy(s8) px py。y_hi=0xFF は死んだ粒。
   ★y は「画面Y + 48」で持つ。画面の上へ飛び出した粒が 0 を下回って回り込み、
     「即死して撒いた場所に少し残るだけ」になっていたため(実際に踏んだ)。48 ドットぶん上に
     居られるようにして、重力で戻ってくるまで生かす。 */
#define YBIAS 48
#include "types.h"
#include "vdp.h"
#include "raster.h"
#include "overlay.h"
#include "scroll.h"
#include "aa_hot.h"      /* curstage(4面=双子艦だけ噴き口が 2 つ) */

#define TMPL_ADDR 0xC600
#define PRT_ADDR  0xCE00
#define CODE_ADDR 0xD500           /* ★粒の終端より後ろ。0xD7FF まで 768B 使える */
#define TMPL    ((u8 *)TMPL_ADDR)  /* 海テンプレート 16 行(1 行 128B)。256 境界に置くこと */
#define PRT     ((u8 *)PRT_ADDR)
#define PRT_N   200                /* 2x2 ドットで 30fps に収まる数(実測) */
#define PRT_SZ  8                  /* x(2) y(2) vx vy px py。px=0xFF は「前回描いていない」 */
#define CODE    ((u8 *)CODE_ADDR)
#if (TMPL_ADDR + 16 * 128) > PRT_ADDR
#error "海テンプレートが粒の置き場に食い込んでいる"
#endif
#if (PRT_ADDR + PRT_N * PRT_SZ) > CODE_ADDR
#error "粒の置き場が RAM 実行コードを上書きする(これで機械がリセットした)"
#endif
#if CODE_ADDR + 768 > 0xD800
#error "RAM 実行コードが hot_ram の借用範囲(0xD7FF)をはみ出す"
#endif

#define SEQ_FALL 64                /* 噴き口が上から下まで下りるフレーム数 */
#define SEQ_END  104               /* 全体の尺(約 3.5 秒) */

static u16 seed;
static u8  emit_y;                 /* 噴き口の画面Y(ドット) */
static u16 cnt16;
static u8  vscroll_now;            /* asm から見る縦スクロール(g_vscroll の写し) */
static u8  pcol;                   /* いま描く粒の色(2 ドット分) */
static u8  wind;                   /* 爆風の横風(s8)。時間でゆっくり向きが変わる(軌道を反らせる) */
/* 噴き口の中心 -16(ここに 0..31 の散らしを足す)。★4面の双子艦は艦が 2 隻あるので 2 か所から噴く。
   艦の中心は ship_render の paint_hull_at と同じ 76 / 180(単艦は 128)。 */
static u8  cx0, cx1;

__sfr __at(0x98) PDAT;
__sfr __at(0x99) PCTL;

static u16 rnd16(void) { seed = (u16)(seed * 25173 + 13849); return seed; }

/* ★VRAM の 行512 以降は 16bit 番地を超える。R#14 は A14-A16(16KB 単位)なので row>>7。 */
static void read_row(u16 row) {
    u8  a16 = (u8)(row >> 7);
    u16 lo  = (u16)((row & 127) << 7);
    __asm di __endasm;
    PCTL = (u8)(a16 & 7);   PCTL = 0x80 | 14;
    PCTL = (u8)(lo & 0xFF); PCTL = (u8)((lo >> 8) & 0x3F);
    __asm ei __endasm;
}

/* 海テンプレート(VRAM 512行〜の 16 行)を RAM へ。粒の消去に使う。 */
static void grab_tmpl(void) {
    u8 r;
    u16 i;
    for (r = 0; r < 16; r++) {
        u8 *d = TMPL + ((u16)r << 7);
        read_row((u16)(SC_SEATMPL_Y + r));
        for (i = 0; i < 128; i++) d[i] = PDAT;
    }
}

/* ───────── 粒を 1 フレーム進めて SCREEN5 の背景へ差分描き(asm・RAM実行) ─────────
   ★番地: リング行 = 縦スクロール + 画面Y + 行オフセット。表示リングは page1(VRAM 行 256〜)
     なので 上位 = 0x80 + (row>>1) / 下位 = ((row&1)<<7) | (x>>1)。桁上がりしない。
   ★__naked で IX を使うので push/pop。相対ジャンプだけ(RAM 実行のため)。 */
static void step_plot(void) __naked {
    __asm
        push ix
        ld   ix, #0xCE00
        ld   hl, #200
        ld   (_cnt16), hl
        jr   pq_first
    pq_loop:
        ld   bc, #8
        add  ix, bc
    pq_first:
        ld   hl, (_cnt16)
        dec  hl
        ld   (_cnt16), hl
        ld   a, h
        or   l
        jr   nz, pq_body
        pop  ix
        ret
    pq_body:
        ld   a, 3(ix)             ; y_hi。0xFF は死んだ粒
        inc  a
        jr   z, pq_loop
        ;; ---- 前回描いた場所を消す(描いていなければ px=0xFF) ----
        ld   a, 6(ix)
        inc  a
        jr   z, pq_move
        jr   pq_er
    pq_back0:                     ; ★中継(jr の飛距離 ±127)
        jr   pq_loop
    pq_er:
        ld   c, 6(ix)             ; px
        ld   a, 7(ix)             ; py(画面行)
        call pq_erase
        ld   c, 6(ix)
        ld   a, 7(ix)
        inc  a
        call pq_erase
    pq_move:
        ;; ---- x += vx*16 / y += vy*16 / vy += 2(重力) ----
        ld   e, 4(ix)
        ld   a, e
        rla
        sbc  a, a
        ld   d, a
        sla  e
        rl   d
        sla  e
        rl   d
        sla  e
        rl   d
        sla  e
        rl   d
        ld   l, 0(ix)
        ld   h, 1(ix)
        add  hl, de
        ld   0(ix), l
        ld   1(ix), h
        ld   e, 5(ix)
        ld   a, e
        rla
        sbc  a, a
        ld   d, a
        sla  e
        rl   d
        sla  e
        rl   d
        sla  e
        rl   d
        sla  e
        rl   d
        ld   l, 2(ix)
        ld   h, 3(ix)
        add  hl, de
        ld   2(ix), l
        ld   3(ix), h
        jr   pq_gtyp
    pq_backm:                     ; ★中継(jr は ±127。種類分けを足して pq_back0 が遠くなった)
        jr   pq_back0
    pq_gtyp:
        ;; ---- 加速度は粒ごとに 4 種類(下 2bit)。全部「初速＋一定の重力」にすると
        ;;      どれも同じ放物線＝直線的に見えるため(ユーザー指摘)。 ----
        ld   a, (_cnt16)
        and  #3
        jr   z, pq_g0
        dec  a
        jr   z, pq_g1
        dec  a
        jr   z, pq_g2
        ;; --- 3: 渦。上っている間は内へ、落ち始めたら外へ曲がる(S字) ---
        ld   a, 5(ix)
        add  a, #2
        ld   5(ix), a
        bit  7, a
        ld   a, 4(ix)
        jr   nz, pq_g3u
        inc  a
        jr   pq_g3s
    pq_g3u:
        dec  a
    pq_g3s:
        ld   4(ix), a
        jr   pq_chk
    pq_g0:
        ;; --- 0: 重い燃えかす。急な放物線 ---
        ld   a, 5(ix)
        add  a, #3
        ld   5(ix), a
        jr   pq_chk
    pq_g1:
        ;; --- 1: 軽い火の粉。ゆっくり落ちて、時間で向きの変わる風に流される ---
        ld   a, 5(ix)
        inc  a
        ld   5(ix), a
        ld   a, (_wind)
        add  a, 4(ix)
        ld   4(ix), a
        jr   pq_chk
    pq_g2:
        ;; --- 2: 爆風。中心(x=128)から外へ加速する＝外向きに反る ---
        ld   a, 5(ix)
        add  a, #2
        ld   5(ix), a
        ld   a, 1(ix)
        cp   #128
        ld   a, 4(ix)
        jr   nc, pq_g2r
        dec  a
        jr   pq_g2s
    pq_g2r:
        inc  a
    pq_g2s:
        ld   4(ix), a
        jr   pq_chk
    pq_back:
        jr   pq_backm
    pq_chk:
        ;; ---- 海面(画面の下)・左右の外は消滅 ----
        ld   a, 3(ix)
        cp   #246                 ; YBIAS(48) + 198
        jr   nc, pq_kill
        ld   a, 1(ix)
        cp   #250
        jr   nc, pq_kill
        ;; ---- まだ画面の上なら「描かなかった」印を付けて次へ ----
        ld   a, 3(ix)
        cp   #48
        jr   nc, pq_color
        ld   6(ix), #0xFF
        jr   pq_back
    pq_kill:
        ld   3(ix), #0xFF
        ld   6(ix), #0xFF
        jr   pq_back
    pq_color:
        ;; ---- 色: 破片=中灰 / 火の粉=橙・赤・白 ----
        ld   a, 4(ix)
        and  #3
        jr   nz, pq_spark
        ld   a, #0x44
        jr   pq_have
    pq_spark:
        dec  a
        jr   z, pq_s1
        dec  a
        jr   z, pq_s2
        ld   a, #0xFF
        jr   pq_have
    pq_s1:
        ld   a, #0xCC
        jr   pq_have
    pq_s2:
        ld   a, #0xBB
    pq_have:
        ld   (_pcol), a
        ;; ---- 描く(2 行)。描いた場所を覚える ----
        ld   a, 3(ix)
        sub  #48
        ld   7(ix), a             ; py = 画面行
        ld   b, a                 ; 行を退避
        ld   a, 1(ix)
        ld   6(ix), a             ; px = x(★ld 6(ix),1(ix) は不可。A を介す)
        ld   c, a
        ld   a, b
        call pq_draw
        ld   c, 6(ix)
        ld   a, 7(ix)
        inc  a
        call pq_draw
        jr   pq_back

        ;; --- C=x(ドット) A=画面行: その位置を海の色で塗り戻す ---
    pq_erase:
        push af
        call pq_addr
        pop  af                   ; A = 画面行
        ld   b, a
        ld   a, (_vscroll_now)
        add  a, b
        and  #15                  ; テンプレートの行 r(0..15)
        ld   b, a
        ;; ★TMPL は 1 行 128B。番地 = 0xC600 + r*128 + (x>>1) なので
        ;;   上位 = 0xC6 + (r>>1) / 下位 = ((r&1)<<7) | (x>>1)。
        ;;   ここを「上位 = 0xC6 + r」(1 行 256B のつもり)にしていたため、奇数行は
        ;;   よその行、r>=2 では **2KB の外(粒の置き場やコード)** を海の色として
        ;;   塗っていた。海一面に色とりどりのゴミが残るのはこれ(実際に踏んだ)。
        srl  a
        add  a, #0xC6
        ld   h, a
        ld   a, b
        and  #1
        rrca                      ; (r&1)<<7
        ld   l, a
        ld   a, c                 ; x(ドット)
        srl  a
        add  a, l
        ld   l, a
        ld   a, (hl)
        out  (0x98), a
        ret

        ;; --- C=x(ドット) A=画面行: 粒の色を打つ ---
    pq_draw:
        call pq_addr
        ld   a, (_pcol)
        out  (0x98), a
        ret

        ;; --- C=x(ドット) A=画面行 → VRAM 書込み番地を設定 ---
        ;;   番地 = 0x8000 + row*128 + (x>>1)。ラッチは 14bit なので A14-A16 は R#14 へ。
        ;;   page1(0x8000) ゆえ R#14 = 2 + (row>>7)。ここを 0 にしていて粒が page0(非表示)に
        ;;   描かれていた(実際に踏んだ)。
    pq_addr:
        ld   b, a
        ld   a, (_vscroll_now)
        add  a, b                 ; リング行(0..255)
        ld   b, a
        rlca
        and  #1
        add  a, #2
        ld   d, a                 ; R#14
        ld   a, b
        and  #0x7F
        srl  a
        ld   h, a                 ; ラッチ上位
        ld   a, b
        and  #1
        rrca                      ; (row&1)<<7
        ld   l, a
        ld   a, c
        srl  a
        add  a, l
        ld   l, a                 ; ラッチ下位
        di
        ld   a, d
        out  (0x99), a
        ld   a, #0x80 + 14
        out  (0x99), a
        ld   a, l
        out  (0x99), a
        ld   a, h
        or   #0x40
        out  (0x99), a
        ei
        ret
    __endasm;
}
static void step_plot_end(void) __naked { __asm ret __endasm; }
static void (*stepr)(void);

/* ───────── 破片(スプライト 16x16) ─────────
   ★背景に描く粒は 2x2 ドットで目立ちにくい。大きく見える破片はスプライトで飛ばす。
     スプライト属性の書込みは 1 枚 4 バイト(番地設定 1 回)で、30 枚でも 1ms 弱。
     演出中は HUD も敵も出ないので 32 枚まるごと使える。 */
#define DEB_N 28
static u8  dbx[DEB_N], dby[DEB_N];      /* 画面座標(ドット) */
static s8  dvx[DEB_N], dvy[DEB_N];
static u8  dlive[DEB_N];
/* 軌道の種類(dlive の bit4-5)。deb_step のコメント参照。 */
#define DEB_ARC   0x00
#define DEB_LEAF  0x10
#define DEB_SPIN  0x20
#define DEB_FLOAT 0x30

/* 16x16 の破片(4 つの 8x8 パターン = 32 バイト)。左半分 16 行 → 右半分 16 行 の順。
   ★丸い塊を並べると「グラディウス(MSX)1面の溶岩」に見えて破片にならない(ユーザー指摘)。
     直線と角で出来た幾何学的なシルエット(楔・L字アングル・斜めの板・骨組み・砲身・箱枠)を
     6 種類そろえ、それぞれに「寝かせた面」を 1 枚ずつ足して 12 枚。ポーズを入れ替えることで
     回転(横転)しながら飛んでいるように見せる(スプライトは回転できないので絵を差し替える)。
   ★パターン番号は 16x16 なので 4 の倍数。種類 s・ポーズ p → s*8 + p*4。 */
#define DEB_KIND 6
static const u8 deb_pat[DEB_KIND * 2][32] = {
    {   /* 装甲板の破片(鋭い楔) */
        0x00,0x01,0x07,0x1F,0x3F,0x7F,0xFF,0xFF,0xFF,0x7F,0x3E,0x1C,0x08,0x00,0x00,0x00,
        0x00,0xF0,0xF8,0xF8,0xF8,0xF0,0xE0,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* 同・横倒し(薄く見える面) */
        0x00,0x00,0x00,0x00,0x01,0x0F,0x3F,0x7C,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x7C,0xFC,0xF0,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* L字アングル材(梁の断片) */
        0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x70,0x7F,0x7F,0x7F,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x06,0x06,0xFE,0xFE,0xFE,0x00,0x00 },
    {   /* 同・斜め(への字に見える面) */
        0x00,0x01,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* 甲板の板きれ(斜めの帯・リベット穴) */
        0x00,0x00,0x00,0x01,0x03,0x07,0x0D,0x1F,0x3E,0x7C,0xF8,0x00,0x00,0x00,0x00,0x00,
        0x3E,0x7C,0xF8,0xB8,0xE0,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* 同・水平(折れ口はぎざぎざ) */
        0x00,0x00,0x00,0x7F,0x7F,0x66,0x7F,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0xF8,0xFC,0x6E,0xFC,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* マスト/デリックの骨組み(筋交いの枠) */
        0x00,0x7F,0x50,0x48,0x44,0x42,0x41,0x41,0x42,0x44,0x48,0x50,0x7F,0x00,0x00,0x00,
        0x00,0xFE,0x0A,0x12,0x22,0x42,0x82,0x82,0x42,0x22,0x12,0x0A,0xFE,0x00,0x00,0x00 },
    {   /* 同・横倒し(桟だけ見える) */
        0x00,0x00,0x00,0x00,0x7F,0x49,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0xFE,0x22,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* 折れた砲身(裂けた断面) */
        0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x1F,0x17,0x23,0x43,0x82,0x00,0x00,0x00,0x00,
        0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xC0,0xE0,0xA0,0x10,0x08,0x04,0x00,0x00,0x00,0x00 },
    {   /* 同・斜め */
        0x00,0x00,0x00,0x00,0x01,0x07,0x0F,0x3E,0x78,0x60,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x3C,0x7C,0xF0,0xC0,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    {   /* 構造物の箱(枠が残った角材) */
        0x00,0x3F,0x20,0x2F,0x28,0x28,0x2F,0x20,0x3F,0x3F,0x3F,0x00,0x00,0x00,0x00,0x00,
        0x00,0xF8,0x08,0xE8,0x28,0x28,0xE8,0x08,0xF0,0xE0,0x80,0x00,0x00,0x00,0x00,0x00 },
    {   /* 同・斜め(平行四辺形の枠) */
        0x00,0x07,0x08,0x10,0x20,0x40,0x80,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0xF8,0x04,0x08,0x10,0x20,0x40,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
};

/* 行ごとの色(16 行)。モード2の色表で「1 枚のスプライトの中で」色を変えられる。
   種類ごとに金属・木甲板・燃えている、と質感を変える。 */
#define DEB_COL_BURN (DEB_KIND)        /* 最後の 1 枚は「燃えている破片」。種類を問わず混ぜる */
static const u8 deb_col[DEB_KIND + 1][16] = {
    /* 楔: 淡灰のハイライト → 中灰 → 影 */
    { 14,14, 4, 4, 4, 5, 5, 5, 13,13,13,13,13,13,13,13 },
    /* L字アングル: 鋼の灰 */
    {  5, 5, 4, 4, 4, 4, 4, 5,  5, 4, 4,13,13,13,13,13 },
    /* 板きれ: 木甲板 → オリーブ(舷側) */
    {  6, 6, 6, 9, 9, 6, 6, 9,  9, 9, 9,13,13,13,13,13 },
    /* 骨組み: 細い鋼材のちらつき */
    { 14, 5,14, 5,14, 5, 4, 4,  5,14, 5,13,13,13,13,13 },
    /* 砲身: 金属 → 裂けた断面が焼けている */
    { 14,14, 5, 5, 4, 4, 4, 4,  5, 5,12,11,11,13,13,13 },
    /* 箱枠: 上面が明るく下へ落ちる */
    { 14, 4, 4, 5, 5, 4, 4,13, 13,13,13,13,13,13,13,13 },
    /* 燃えている破片: 白 → 橙 → 赤 → 焼け落ちた黒。★橙と赤を交互にすると縞模様に見えて
       「部品」ではなく旗のようになる。上から下へ一方向に落とすこと。 */
    { 15,15,12,12,12,12,11,11, 11,11,11,13,13,13,13,13 },
};

/* ★dlive[] は 1 バイトに詰める(オーバレイの静的領域は 256B しかない):
     0 = 出ていない / bit7 = 出ている / bit4-5 = 軌道の種類 / bit3 = ポーズ(寝かせた面) /
     bit0-2 = 次に横転するまでの数(軌道の位相も兼ねる)。
   種類は添字で決まる(i を 6 で割った余り)。色表はスプライトごとに固定なので、
   種類と色表は必ず対にすること(途中で種類を変えると木の色の骨組み等になる)。 */
static void deb_init(void) {
    u8 i, k;
    vdp_sprite_init();
    for (i = 0; i < DEB_KIND * 2; i++)
        vdp_sprite_pattern((u8)(i * 4), deb_pat[i]);   /* 16x16 はパターン番号 4 飛び */
    k = 0;
    for (i = 0; i < DEB_N; i++) {
        dlive[i] = 0;
        /* ★4 枚に 1 枚は「燃えている」色にして、金属一色の群れに見えないようにする */
        vdp_sprite_color_tab(i, ((i & 3) == 3) ? deb_col[DEB_COL_BURN] : deb_col[k]);
        if (++k >= DEB_KIND) k = 0;                    /* i % DEB_KIND を割り算せずに作る */
    }
    vdp_sprite_hide_from(0);
}

/* 破片を 1 つ噴き口から出す */
static void deb_spawn(void) {
    u8 i;
    for (i = 0; i < DEB_N; i++) {
        if (dlive[i]) continue;
        {
            u16 r = rnd16();
            u8  ty = (u8)((r >> 4) & 0x30);            /* 軌道の種類(bit4-5) */
            dbx[i]  = (u8)(((i & 1) ? cx1 : cx0) + ((r >> 8) & 31));
            dby[i]  = emit_y;
            dvx[i]  = (s8)((r & 15) - 8);
            dvy[i]  = (s8)(((r >> 4) & 15) - 11);
            /* ★種類ごとに出だしの勢いを変える。同じ初速だと、どの破片も同じ放物線を描いて
               「直線的」に見える(ユーザー指摘)。 */
            if (ty == DEB_LEAF)  { dvy[i] = (s8)(dvy[i] / 2 - 2); }          /* 板きれ: 高く上がらずひらひら */
            if (ty == DEB_SPIN)  { dvx[i] = (s8)(dvx[i] * 2); dvy[i] = (s8)(dvy[i] + 2); }  /* 螺旋: 横に強く */
            if (ty == DEB_FLOAT) { dvy[i] = (s8)(dvy[i] - 3); }              /* 軽い破片: 高く舞い上がる */
            dlive[i] = (u8)(0x80 | ty | (r & 0x0F));   /* ポーズと横転位相も散らす */
        }
        return;
    }
}

/* ★破片を 1 フレーム進めて置き直す。
   軌道は 4 種類。全部を同じ「初速＋重力」にすると、どれも同じ放物線＝直線的に見えて
   「破片が飛んでいる」感じにならない(ユーザー指摘)。種類は dlive の bit4-5 に持つ。
     DEB_ARC   放物線＋空気抵抗 … 素直に飛んで、横の勢いが鈍っていく
     DEB_LEAF  木の葉          … 落ちるのが遅く、左右へジグザグに翻る
     DEB_SPIN  螺旋            … 速度ベクトルを毎フレーム少し回す＝弧を描いて外へ逃げる
     DEB_FLOAT 高く舞う        … 重力が 1/3。高く上がってから、ゆっくり戻ってくる */
static void deb_step(void) {
    u8 i, base = 0;
    for (i = 0; i < DEB_N; i++) {
        u8 d = dlive[i];
        u8 pat = base;
        base += 8;                                      /* 次の種類の先頭パターン */
        if (base >= DEB_KIND * 8) base = 0;
        if (!d) { vdp_sprite_pos(i, 0, 216, 0); continue; }
        {
            u8  ty = (u8)(d & 0x30);
            u8  ph = (u8)(d & 7);                       /* 横転までの数。軌道の位相にも使う */
            s16 nx = (s16)((s16)dbx[i] + dvx[i]);
            s16 ny = (s16)((s16)dby[i] + dvy[i]);
            if (ty == DEB_ARC) {
                dvy[i]++;                                           /* 重力 */
                if (!ph) dvx[i] = (s8)(dvx[i] - (dvx[i] >> 2));     /* 空気抵抗(横だけ鈍る) */
            } else if (ty == DEB_LEAF) {
                if (ph & 1) dvy[i]++;                               /* 落ちるのが遅い */
                if (!ph) dvx[i] = (s8)(-dvx[i]);                    /* 翻って逆へ */
                if (dvy[i] > 3) dvy[i] = 3;                         /* 終端速度(ひらひら) */
            } else if (ty == DEB_SPIN) {
                s8 vx = dvx[i], vy = dvy[i];                        /* 速度を約15度ずつ回す */
                dvx[i] = (s8)(vx - (vy >> 2));
                dvy[i] = (s8)(vy + (vx >> 2) + ((ph & 1) ? 1 : 0)); /* 回しつつ重力 */
            } else {
                if (!(ph & 3)) dvy[i]++;                            /* 重力 1/3(軽い破片) */
                if (!ph) dvx[i] = (s8)(dvx[i] - (dvx[i] >> 3));
            }
            if (nx < 2 || nx > 248 || ny > 205) { dlive[i] = 0; vdp_sprite_pos(i, 0, 216, 0); continue; }
            if (ny < 0) ny = 0;
            dbx[i] = (u8)nx; dby[i] = (u8)ny;
            /* ★横転: 数フレームおきにポーズを入れ替える。速さは破片ごとに変える */
            { u8 tm = ph;
              if (tm) tm--;
              else { d ^= 0x08; tm = (u8)(3 + (i & 3)); }
              dlive[i] = (u8)((d & 0xF8) | tm); }
            if (d & 0x08) pat += 4;                     /* 寝かせた面 */
            vdp_sprite_pos(i, dbx[i], dby[i], pat);
        }
    }
}

/* 噴き口から粒を出す。★山なりに散らす(横に強く・上に強く)。 */
static void emit(u8 n) {
    u8 *p = PRT;
    u16 i;
    for (i = 0; i < PRT_N && n; i++, p += PRT_SZ) {
        if (p[3] != 0xFF) continue;
        {
            u16 r = rnd16();
            p[0] = (u8)(r & 0xFF);
            p[1] = (u8)(((i & 1) ? cx1 : cx0) + ((r >> 8) & 31));   /* x(ドット): 艦の中心付近 */
            p[2] = (u8)(r >> 5);
            p[3] = (u8)(emit_y + YBIAS);
            r = rnd16();
            p[4] = (u8)((s8)((r & 0x7F) - 64));        /* 横 ±64(×16 で ±4 ドット/フレーム) */
            p[5] = (u8)((s8)(((r >> 9) & 0x3F) - 32)); /* 縦 ±32(放射状) */
            p[6] = 0xFF;                               /* まだ描いていない */
        }
        n--;
    }
}

/* 噴き口が通ったところの艦を海に戻す。★端をぎざぎざにする(一直線に消えると
   「艦が消えただけ」に見えるため、行ごとに左右の食い込みを乱数で変える)。 */
static void erase_row(u8 y) {
    u16 ring = (u16)(256 + (u8)(vscroll_now + y));
    u16 r = rnd16();
    u16 x0 = (u16)(r & 31);                 /* 左の食い込み 0..31 */
    u16 w  = (u16)(256 - x0 - ((r >> 5) & 31));
    vdp_copy(x0, (u16)(SC_SEATMPL_Y + (u8)(ring & 15)), x0, ring, w, 1);
}

static void white_pal(void) { u8 i; for (i = 0; i < 16; i++) vdp_set_pal(i, 7, 7, 7); }

/* 撃沈の直後に常駐から呼ばれる(OVL_SLOT_PART)。戻るまで数秒ここに居る。 */
void ovl_part(void) {
    u16 k, t;
    const u8 *sp = (const u8 *)step_plot;
    u16 len = (u16)((const u8 *)step_plot_end - sp);
    if (len > 768) return;     /* 0xD500-0xD7FF に収まらないなら演出をあきらめる */
    seed = 0x2468;
    if (curstage == 3) { cx0 = 76 - 16; cx1 = 180 - 16; }   /* 双子艦: 2 隻それぞれから */
    else               { cx0 = 128 - 16; cx1 = 128 - 16; }
    vscroll_now = g_vscroll;
    grab_tmpl();
    for (k = 0; k < PRT_N; k++) { PRT[k * PRT_SZ + 3] = 0xFF; PRT[k * PRT_SZ + 6] = 0xFF; }
    for (k = 0; k < len; k++) CODE[k] = sp[k];
    stepr = (void (*)(void))CODE;
    emit_y = 0;
    raster_off();
    deb_init();
    /* ★白フラッシュ: 轟沈の瞬間を作る(パレットだけ=帯域ゼロ) */
    white_pal();
    vdp_wait_frame();
    vdp_wait_frame();
    vdp_palette_game();
    for (t = 0; t < SEQ_END; t++) {
        if (t < SEQ_FALL) {
            u8 ny = (u8)(((u16)t * 212) / SEQ_FALL);   /* ★表示は 212 行。196 で止めると下に艦が残る */
            while (emit_y < ny) { erase_row(emit_y); emit_y++; }
            /* ★風は時間で向きが変わる。一定の風だと全部が同じ方向へ流れて、やはり直線に見える。 */
        { static const s8 wtab[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };
          wind = (u8)wtab[(t >> 2) & 7]; }
        vdp_cmd_wait();   /* ★コピー(VDPコマンド)の完了を待つ。実行中に VRAM を直接叩くと
                                 書込みが化ける(粒が VRAM には在るのに画面に出ない状態になっていた) */
            emit((u8)((t < 12) ? 28 : 16));           /* 最初にどっと噴き、以降も絶やさない */
            deb_spawn();                              /* 破片(スプライト)も出す */
            if (t < 12) deb_spawn();
        }
        if (t == SEQ_FALL) {                       /* 取りこぼしが無いよう最後に全行を掃く */
            while (emit_y < 212) { erase_row(emit_y); emit_y++; }
            vdp_cmd_wait();
        }
        stepr();
        deb_step();
        vdp_wait_frame();
    }
    vdp_sprite_hide_from(0);
}
