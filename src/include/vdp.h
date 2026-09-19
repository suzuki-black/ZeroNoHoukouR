/* vdp.h — VDP(V9958)アクセスの常駐API。
   実装(vdp.c)は前作 BattleshipProto で実機確定した MSXgl 流イディオム
   (0x99 の2バイト書込を各自 di/ei で原子化)を踏襲。
   ※ホットパス(毎フレーム描画)から呼ぶ想定。常駐(bank0-2)に置く。 */
#ifndef VDP_H
#define VDP_H

#include "types.h"

/* VDP レジスタ R#r へ v を書く(di/ei 原子化)。 */
void vdp_wreg(u8 r, u8 v);

/* パレット registers: 色 idx を (r,g,b) 各0-7 に設定。 */
void vdp_set_pal(u8 idx, u8 r, u8 g, u8 b);

/* VRAM 書込アドレスを a に設定(以降 vdp_data() で連続書込)。 */
void vdp_write_addr(u16 a);

/* VRAM データポートへ1バイト(vdp_write_addr 後に使う)。 */
void vdp_data(u8 v);
u8   vdp_read_data(void);   /* vdp_read_addr の後に1バイト読む */

/* SCREEN5(GRAPHIC4, 256x212 16色)へ切替(BIOS CHGMOD)。 */
void vdp_screen5(void);

/* ゲーム標準パレット(SCREEN5)を張る。SCREEN5 へ入る度(CHGMOD後)に呼ぶ。 */
void vdp_palette_game(void);

/* 文字 c の自前8x8フォント・グリフ(8B)を返す(未収録/空白は空グリフ)。HUD数字パターン生成等が使う。 */
const u8 *vdp_glyph(u8 c);

/* 拡大文字描画(自前フォント, scale 倍角。px 偶数前提)。見出し(STAGE/TARGET/艦名)用。 */
void vdp_text_s(u8 px, u8 py, u8 fg, u8 bg, u8 scale, const char *s);

/* SCREEN12(GRAPHIC7 + YJK 自然画, 256x212)へ切替。タイトルYJK画の表示に使う。 */
void vdp_screen12(void);

/* first_bank から連続バンクの生データ total バイトを VRAM (hi<<16|lo) 番地から流す(常駐のみ)。 */
void vdp_blit_bank_vram(u8 first_bank, u16 total, u8 hi, u16 lo);

/* VDPコマンド完了待ち(CE ポーリング)。 */
void vdp_cmd_wait(void);

/* 矩形塗り(HMMV): (dx,dy) から (nx,ny) を色 color で塗る。 */
void vdp_fill(u16 dx, u16 dy, u16 nx, u16 ny, u8 color);

/* VRAM→VRAM 矩形コピー(LMMM)。(sx,sy)→(dx,dy) を (nx,ny)。Yは0-1023(全4ページ)。
   縦スクロールの行流し込み(バッファB→表示ページ)に使う。 */
/* ★コマンドレジスタ列(R#32..46)と発行。稲妻(ovl_crush)が LINE を自前で組むために公開する。 */
extern volatile u8 vdpcbuf[16];
void vdp_cmd_flush(void);
void vdp_copy(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny);
/* VRAM→VRAM 透過コピー(色0は転送しない)。炎/煙を艦BGへ重ねる用。 */
void vdp_copy_t(u16 sx, u16 sy, u16 dx, u16 dy, u16 nx, u16 ny);

/* 文字列描画(BIOS 8x8フォント使用)。px は偶数、page0 のみ。fg=文字色/bg=地色。 */
void vdp_text(u8 px, u8 py, u8 fg, u8 bg, const char *s);

/* 表示同期: VBLANK(JIFFY 更新)を1回待つ。 */
void vdp_wait_frame(void);

/* ===== スクロール =====
   縦スクロール R#23 は VRAM 全体を縦シフトし「スプライトにも効く」。
   よって縦スクロール中もスプライトを画面固定に見せるため、vdp_sprite_pos は
   現在の縦スクロール量を Y に加算して補正する(この値は vdp_set_vscroll が保持)。 */
void vdp_set_vscroll(u8 v);                 /* R#23 = v(縦スクロール)。0で無効化。 */
extern u8 g_vscroll;                        /* 現在の縦スクロール量(vdp_set_vscroll が保持)。
                                               ★entity.c の asm と raster.c(分割行の VRAM 行補正)が参照する。 */
void vdp_set_hscroll(u8 coarse, u8 fine);   /* R#26=coarse(8px単位)/R#27=fine(0-7)。蛇行はスプライト非影響。 */

/* 表示ページ(SCREEN5: 0/1)。R#2。スプライトテーブルは page0末尾に居るので、縦スクロールする
   背景は page1 に描いて page1 を表示 → スクロールでスプライトテーブルが可視域に出るゴミを防ぐ。
   VDPコマンドで page1 を描くには Y に +256 する(DYは10bit)。 */
void vdp_set_display_page(u8 page);

/* ===== スプライト(V9938 mode2, 16x16) =====
   SCREEN5 の BIOS 既定テーブルを使用: 属性0x7600 / 色0x7400 / パターン0x7800(vdp.c参照)。
   色はmode2では行ごと(色表16B/枚)。単色運用は vdp_sprite_color で全16行を塗る。 */
void vdp_sprite_init(void);                          /* 16x16化＋テーブル基底設定＋全消し */
void vdp_sprites(u8 on);                             /* スプライト機能ON/OFF(R#8 SPD)。重いblit中はoff=VDP帯域回復 */
void vdp_sprite_pattern(u8 patnum, const u8 *d32);
extern u8 g_spr_patb;   /* 1=vdp_sprite_pattern が絵の表B(0x2000)にも書く(4面の中ボスの間。HUD のボム棒が下の帯でも変わるように) */   /* 16x16=32B をパターン patnum へ(patnumは4の倍数) */
void vdp_sprite_color(u8 slot, u8 color);            /* slot の色表16行を単色 color に */
void vdp_sprite_color_tab(u8 slot, const u8 *tab16); /* slot の色表を行別に(陰影)。row0=上 */
/* ★スプライト拡大(R#1 bit0=MAG)。16x16 と併せて 1枚32x32ドットになる=8枚で画面幅を覆える。
   全スプライトに効くので、ONにする区間では他のスプライトを出さないこと(津波演出でのみ使用)。 */
void vdp_sprite_mag(u8 on);
/* ★VRAM 読み出し。アフィン回転の元絵をスプライトパターン表から取り出すのに使う。 */
void vdp_read_addr(u16 a);
void vdp_sprite_pattern_read(u8 patnum, u8 *d32);
void vdp_sprite_pos(u8 slot, u8 x, u8 y, u8 patnum); /* slot の属性(Y=y-1,X,pattern)を更新 */
void vdp_sprite_hide_from(u8 slot);                  /* slot に停止マーカ(Y=208)=以降非表示 */
/* ★A6: SAT属性をRAM鏡へ溜め→一括バースト(ent_draw_all専用=ポートアクセス削減)。色表は別テーブルで従来通り。 */
void vdp_sat_pos(u8 slot, u8 x, u8 y, u8 patnum);    /* 属性をシャドウへ(VRAM直書きせず) */
void vdp_sat_flush(u8 from, u8 live);
extern u8 g_spr_hide_to;   /* 0 以外: vdp_sat_flush は停止マーカを書かず、live..これ-1 を画面外へ(後ろの枠に中ボス) */

/* ---- スプライト表の2セット目(ラスタ分割で32枚の総数制限を破る。詳細は vdp.c) ----
   R#5 = (色表>>7)|0x07 / 属性表 = 色表+0x200。A=0xEF(既定) / B=0xE7(色0x7000,属性0x7200)。 */
#define SPR_R5_A 0xEF
#define SPR_R5_B 0xE7
extern u8 g_spr_dual;                      /* 1=属性/色をセットBへもミラー(分割しても見た目不変の土台) */
void vdp_sprite_setbase(u8 r5);            /* R#5 を切替(分割行では RasSplit の reg=5 で直接書く) */
void vdp_sprite_setb_init(u8 color);       /* セットBを初期化(色表を単色で埋め全枚を画面外へ)。1回だけ */
void vdp_sprite_pos_a(u8 slot, u8 x, u8 y, u8 patnum);   /* セットA限定(ミラーしない) */
void vdp_sprite_hide_from_a(u8 slot);
void vdp_sprite_color_a(u8 slot, u8 color);
void vdp_sprite_color_b(u8 slot, u8 color);
void vdp_sprite_pos_b(u8 slot, u8 x, u8 y, u8 patnum);
void vdp_sprite_hide_from_b(u8 slot);                /* シャドウの from..live-1 をSATへ一括バースト＋停止マーカ */

#endif /* VDP_H */
