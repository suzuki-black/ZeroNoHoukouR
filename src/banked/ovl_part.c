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
        ld   a, 5(ix)
        add  a, #2
        ld   5(ix), a
        jr   pq_chk
    pq_back:
        jr   pq_back0
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

/* 16x16 の破片(4 つの 8x8 パターン = 32 バイト)。左上/左下/右上/右下の順。
   ★スプライトは「行ごとに色を変えられる」(モード2の色表)。単色だと台無しなので、
     部品らしく見えるよう 3 種類 × 行ごとの色で作る(ユーザー指摘)。 */
static const u8 deb_pat[3][32] = {
    {   /* 砲塔らしい塊(丸い上部＋角ばった土台) */
        0x03,0x0F,0x1F,0x3F,0x3F,0x7F,0x7F,0x7F,   0x7F,0x7F,0x3F,0x3F,0x1F,0x1F,0x0F,0x06,
        0xC0,0xF0,0xF8,0xFC,0xFC,0xFE,0xFE,0xFE,   0xFE,0xFE,0xFC,0xFC,0xF8,0xF8,0xF0,0x60 },
    {   /* 甲板の板きれ(細長い) */
        0x00,0x00,0x3F,0x7F,0x7F,0x3F,0x00,0x00,   0x00,0x00,0x1F,0x3F,0x3F,0x1F,0x00,0x00,
        0x00,0x00,0xFC,0xFE,0xFE,0xFC,0x00,0x00,   0x00,0x00,0xF0,0xF8,0xF8,0xF0,0x00,0x00 },
    {   /* ぎざぎざの破片 */
        0x00,0x04,0x0E,0x1F,0x1E,0x0C,0x04,0x00,   0x00,0x06,0x0F,0x0E,0x04,0x00,0x00,0x00,
        0x00,0x20,0x70,0xF8,0x78,0x30,0x20,0x00,   0x00,0x60,0xF0,0x70,0x20,0x00,0x00,0x00 },
};

/* 行ごとの色(16 行)。上から下へ色が変わることで立体感と「燃えている感じ」を出す。 */
static const u8 deb_col[3][16] = {
    /* 砲塔: 上は淡灰のハイライト → 中灰 → 暗い土台 */
    { 14,14, 5, 5, 4, 4, 4, 5,  5, 4, 4,13,13,13,13,13 },
    /* 板きれ: 木甲板の色 → オリーブ(舷側) */
    {  6, 6, 6, 6, 9, 9, 9, 9,  6, 6, 6, 9, 9, 9,13,13 },
    /* 燃えている破片: 白 → 橙 → 赤 */
    { 15,15,12,12,12,12,11,11, 12,12,11,11,11,13,13,13 },
};

static void deb_init(void) {
    u8 i;
    vdp_sprite_init();
    vdp_sprite_pattern(0, deb_pat[0]);        /* 16x16 は 4 枚単位(0,4,8) */
    vdp_sprite_pattern(4, deb_pat[1]);
    vdp_sprite_pattern(8, deb_pat[2]);
    for (i = 0; i < DEB_N; i++) {
        dlive[i] = 0;
        vdp_sprite_color_tab(i, deb_col[i % 3]);   /* ★行ごとに色を変える(単色にしない) */
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
            dbx[i]  = (u8)(112 + ((r >> 8) & 31));
            dby[i]  = emit_y;
            dvx[i]  = (s8)((r & 15) - 8);
            dvy[i]  = (s8)(((r >> 4) & 15) - 11);
            dlive[i] = 1;
        }
        return;
    }
}

/* 破片を 1 フレーム進めて置き直す */
static void deb_step(void) {
    u8 i;
    for (i = 0; i < DEB_N; i++) {
        if (!dlive[i]) { vdp_sprite_pos(i, 0, 216, 0); continue; }
        {
            s16 nx = (s16)((s16)dbx[i] + dvx[i]);
            s16 ny = (s16)((s16)dby[i] + dvy[i]);
            if ((i & 3) == 0) dvy[i]++;                 /* 重力(ゆっくり) */
            else if ((i & 1) == 0) dvy[i]++;
            if (nx < 2 || nx > 248 || ny > 205) { dlive[i] = 0; vdp_sprite_pos(i, 0, 216, 0); continue; }
            if (ny < 0) ny = 0;
            dbx[i] = (u8)nx; dby[i] = (u8)ny;
            vdp_sprite_pos(i, dbx[i], dby[i], (u8)((i % 3) * 4));   /* 形は色表と対にする */
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
            p[1] = (u8)(112 + ((r >> 8) & 31));        /* x(ドット): 艦の中心付近 */
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
