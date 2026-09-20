/* hud.c — スプライトHUD実装。数字パターンは自前8x8フォント('0'..'9')を
   16x16 スプライトの左上 8x8 へ写して生成(vdp_text と同じ vdp_glyph 経由=書体を統一)。
   V9938 sprite mode2 は 1走査線 8枚まで表示できるので、上端に数字を6枚並べても欠けない。 */
#include "hud.h"
#include "vdp.h"
#include "sprites.h"
#include "entity.h"   /* g_spr_base(エンティティ描画の開始スロット) */
#include "gamestate.h" /* g_crush: メガクラッシュ残数 */

static void crush_pattern(u8 bars);

/* HUD が VRAM に持っているもの(色表＋ボム棒のパターン)を置き直す。
   ★hud_init だけでなく、**他の用途に奪われた後の復旧**にも呼ぶ: 津波は32枚すべての色表と
     ボム棒のパターン枠(SPR_CRUSH=SPR_WAVE4)を自前の水で上書きする。 */
void hud_colors(void) {
    u8 d;
    if (g_crush) crush_pattern((u8)(g_crush <= 3 ? g_crush : 1));   /* 棒のパターンを描き直す */
    for (d = 0; d < 5; d++) vdp_sprite_color(d, 15);   /* スコア=白 */
    vdp_sprite_color(5, 3);    /* 残機アイコン=緑(零戦シルエット) */
    vdp_sprite_color(6, 11);   /* 残機数=黄 */
    /* ★ボム(メガクラッシュ)残数(画面下)。棒は**行別カラー**で「熱い棒」に見せる
       (白い縁→橙→赤い芯→橙→白い縁)。単色の白い棒だと HUD の数字と区別がつかず、
       ボムのストックに見えない。数字(4本目以降)は白。 */
    { static const u8 crush_col[16] = { 15,15,15, 12,12,12, 11,11,11,11,11, 12,12,12, 15,15 };
      vdp_sprite_color_tab(7, crush_col); }
    vdp_sprite_color(8, 15);
#ifdef DEBUG_FPS
    { u8 sl; for (sl = 9; sl < HUD_SLOTS; sl++) vdp_sprite_color(sl, 13); }   /* FPS2桁＋mask値2桁=ほぼ黒(視認性) */
#endif
}

void hud_init(void) {
    u8 pat[32];
    u8 d, r;
    for (d = 0; d < 10; d++) {
        const u8 *g = vdp_glyph((u8)('0' + d));   /* 自前フォントの数字グリフ */
        for (r = 0; r < 8;  r++) pat[r] = g[r];   /* 左列 rows0-7 = 8x8 グリフ */
        for (r = 8; r < 32; r++) pat[r] = 0;      /* 左列下半分＋右列は空 */
        vdp_sprite_pattern(SPR_DIGIT0 + d * 4, pat);
    }
    hud_colors();
    g_spr_base = HUD_SLOTS;   /* 以降エンティティは slot(HUD_SLOTS) から詰める */
}

/* スコア5桁ゼロ詰め(slot0-4)＋残機=零戦アイコン(slot5)＋予備機数1桁(slot6, 右上)。
   HUDは低slot=高優先なので、8枚/走査線を超えても敵機(slot7+)が先に間引かれHUDは残る。 */
/* ★ボム残数の「棒」パターンを描き直す(bars=1..3)。16x16 の左上から
     幅4px の棒を 6px 間隔で bars 本 …… |  / | |  / | | |
   3本が並ぶ最大の太さが 4px(4+2+4+2+4=16)。1本=小さすぎて見えない、を避けるため
   高さは 14px(row1..14)まで伸ばす。
   ★パターン番号を3つ確保する案は採れなかった(16x16の空き枠は SPR_CRUSH の1つだけ。
     144-156 は津波 SPR_WAVE0 が使う)。残数が変わったときだけ 32B を書き換える
     ＝1面で数回しか走らないので、VRAM 帯域から見れば無に等しい。 */
static void crush_pattern(u8 bars) {
    u8 pat[32], r, l = 0xF0, rt = 0x00;   /* 1本目: x0-3 */
    if (bars >= 2) { l |= 0x03; rt |= 0xC0; }   /* 2本目: x6-9 (左2bit＋右2bit) */
    if (bars >= 3) { rt |= 0x0F; }              /* 3本目: x12-15 */
    for (r = 0; r < 16; r++) {
        u8 on = (u8)(r >= 1 && r <= 14);
        pat[r]      = on ? l  : 0;   /* 左半分(x0-7)  */
        pat[16 + r] = on ? rt : 0;   /* 右半分(x8-15) */
    }
    vdp_sprite_pattern(SPR_CRUSH, pat);
}

void hud_draw(u16 score, u8 lives) {
    /* ★桁分解(除算×10)はスコア/残機が変化した時だけ=毎フレームの高価な除算を回避(Z80は除算がライブラリ呼び)。
       スプライト位置(vscroll補正で毎フレーム変わる)は毎回書く。 */
    static u16 last_score = 0xFFFF; static u8 dig[5] = { 0,0,0,0,0 };
    static u8  last_lives = 0xFF;   static u8 ldig = 0;
    static u8  last_crush = 0xFF;   /* ボム残数: 変化時だけパターンを書き換える */
    static u8  last_pwr = 0xFF;     /* パワーアップ段階: 変化時だけ色を置く */
    u8 crush_n = g_crush;
    u8 i;
    if (score != last_score) {
        static const u16 place[5] = { 10000, 1000, 100, 10, 1 };
        for (i = 0; i < 5; i++) dig[i] = (u8)((score / place[i]) % 10);
        last_score = score;
    }
    if (lives != last_lives) { ldig = (u8)(lives % 10); last_lives = lives; }
    for (i = 0; i < 5; i++)
        vdp_sprite_pos(i, (u8)(8 + i * 8), 2, (u8)(SPR_DIGIT0 + dig[i] * 4));
    vdp_sprite_pos(5, 212, 1, SPR_ZERO);                          /* 残機=零戦シルエット */
    vdp_sprite_pos(6, 234, 2, (u8)(SPR_DIGIT0 + ldig * 4));       /* 予備機数(9頭打ち) */
    /* ★ボム残数を画面下(左)へ。1〜3 は棒の本数そのもの( | / | | / | | | )で数えずに読める。
       4以上(コンフィグや将来のパワーアップ)は棒1本＋数字にする(棒を4本以上並べても読めない)。
       残0のときは画面外へ退避して見せない。
       ★Y=216 は停止マーカなので vdp_sprite_pos 側の spr_y が回避する(ここでは気にしなくてよい)。 */
    if (crush_n != last_crush) {
        last_crush = crush_n;
        if (crush_n) crush_pattern((u8)(crush_n <= 3 ? crush_n : 1));
    }
    if (crush_n) {
        vdp_sprite_pos(7, 8, 190, SPR_CRUSH);
        if (crush_n > 3) vdp_sprite_pos(8, 27, 192, (u8)(SPR_DIGIT0 + (crush_n % 10) * 4));
        else             vdp_sprite_pos(8, 0, 220, SPR_DIGIT0);   /* 画面下端外へ */
    } else {
        vdp_sprite_pos(7, 0, 220, SPR_CRUSH);
        vdp_sprite_pos(8, 0, 220, SPR_DIGIT0);
    }
    /* ★パワーアップ段階(増槽の数)を画面下の中央へ。1段階=1本(最大3本)。取ると増え、ミス/最終面の投棄で減る。
       枠は HUD_SLOTS の後ろを段階ぶんだけ借りる(g_spr_base が scene_stage で段階ぶん下がる)。
       減ったぶんの枠は、次の ent_draw_all が停止マーカで隠す(前景の投棄では gen_planes が画面外へ退避する)。 */
    { static const u8 tx[PWR_MAX] = { 107, 121, 135 };   /* 3本並べたとき中央に来る位置 */
      for (i = 0; i < g_pwr; i++) vdp_sprite_pos((u8)(HUD_SLOTS + i), tx[i], 190, SPR_TANK);
      if (g_pwr != last_pwr) { for (i = 0; i < g_pwr; i++) vdp_sprite_color((u8)(HUD_SLOTS + i), 14); last_pwr = g_pwr; } }
#ifdef DEBUG_FPS
    /* ★デバッグROMのみ。左2桁=g_fps(JIFFY基準の参考値)、右4桁=フレームカウンタ(ストップウォッチ実測用の真値)。
       使い方: 右4桁を読む→スマホで正確に10秒→もう一度読む→(差)/10=実FPS。JIFFYの進み方に依存しない。 */
    /* ★y=24 に FPS2桁(slot9,10)＋mask値2桁(slot11,12)。低slot=高優先なのでゲームスプライトより必ず出る。
       ★"MASK" の文字は廃止(16x16 パターンの空き枠4つを津波 SPR_WAVE0 へ譲った)。 */
    { u8 f = (g_fps > 99) ? 99 : g_fps;
      vdp_sprite_pos(9,   96, 24, (u8)(SPR_DIGIT0 + (f / 10) * 4));   /* FPS十の位 */
      vdp_sprite_pos(10, 104, 24, (u8)(SPR_DIGIT0 + (f % 10) * 4));   /* FPS一の位 */
      vdp_sprite_pos(11, 120, 24, (u8)(SPR_DIGIT0 + (u8)((g_dbgmask / 10) % 10) * 4));   /* マスク十の位 */
      vdp_sprite_pos(12, 128, 24, (u8)(SPR_DIGIT0 + (u8)(g_dbgmask % 10) * 4)); }        /* マスク一の位 */
#endif
}
