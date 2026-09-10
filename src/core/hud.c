/* hud.c — スプライトHUD実装。数字パターンは自前8x8フォント('0'..'9')を
   16x16 スプライトの左上 8x8 へ写して生成(vdp_text と同じ vdp_glyph 経由=書体を統一)。
   V9938 sprite mode2 は 1走査線 8枚まで表示できるので、上端に数字を6枚並べても欠けない。 */
#include "hud.h"
#include "vdp.h"
#include "sprites.h"
#include "entity.h"   /* g_spr_base(エンティティ描画の開始スロット) */

void hud_init(void) {
    u8 pat[32];
    u8 d, r;
    for (d = 0; d < 10; d++) {
        const u8 *g = vdp_glyph((u8)('0' + d));   /* 自前フォントの数字グリフ */
        for (r = 0; r < 8;  r++) pat[r] = g[r];   /* 左列 rows0-7 = 8x8 グリフ */
        for (r = 8; r < 32; r++) pat[r] = 0;      /* 左列下半分＋右列は空 */
        vdp_sprite_pattern(SPR_DIGIT0 + d * 4, pat);
    }
    /* 色は固定(位置だけ毎フレーム更新): スコア=白 / 残機アイコン=零戦の緑 / 残機数=黄 */
    for (d = 0; d < 5; d++) vdp_sprite_color(d, 15);
    vdp_sprite_color(5, 3);    /* 残機アイコン=緑(零戦シルエット) */
    vdp_sprite_color(6, 11);   /* 残機数=黄 */
#ifdef DEBUG_FPS
    /* "MASK" の文字スプライトを空きパターン144,148,152,156へ生成(数字と同じくグリフを16x16左上8x8へ) */
    { static const char msk[4] = { 'M', 'A', 'S', 'K' }; u8 c, rr, p[32];
      for (c = 0; c < 4; c++) {
          const u8 *g = vdp_glyph((u8)msk[c]);
          for (rr = 0; rr < 8;  rr++) p[rr] = g[rr];
          for (rr = 8; rr < 32; rr++) p[rr] = 0;
          vdp_sprite_pattern((u8)(144 + c * 4), p);
      } }
    { u8 s; for (s = 7; s < 15; s++) vdp_sprite_color(s, 13); }   /* FPS2桁＋"MASK"4字＋mask値2桁=ほぼ黒(視認性) */
#endif
    g_spr_base = HUD_SLOTS;   /* 以降エンティティは slot(HUD_SLOTS) から詰める */
}

/* スコア5桁ゼロ詰め(slot0-4)＋残機=零戦アイコン(slot5)＋予備機数1桁(slot6, 右上)。
   HUDは低slot=高優先なので、8枚/走査線を超えても敵機(slot7+)が先に間引かれHUDは残る。 */
void hud_draw(u16 score, u8 lives) {
    /* ★桁分解(除算×10)はスコア/残機が変化した時だけ=毎フレームの高価な除算を回避(Z80は除算がライブラリ呼び)。
       スプライト位置(vscroll補正で毎フレーム変わる)は毎回書く。 */
    static u16 last_score = 0xFFFF; static u8 dig[5] = { 0,0,0,0,0 };
    static u8  last_lives = 0xFF;   static u8 ldig = 0;
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
#ifdef DEBUG_FPS
    /* ★デバッグROMのみ。左2桁=g_fps(JIFFY基準の参考値)、右4桁=フレームカウンタ(ストップウォッチ実測用の真値)。
       使い方: 右4桁を読む→スマホで正確に10秒→もう一度読む→(差)/10=実FPS。JIFFYの進み方に依存しない。 */
    /* ★1走査線8枚制限を守るため2行に分割: y=24にFPS2桁(slot7,8) / y=40に"MASK n"(slot9-13)。
       いずれも低slot=高優先なのでゲームスプライト(slot14+)より必ず表示される。 */
    { u8 f = (g_fps > 99) ? 99 : g_fps;
      vdp_sprite_pos(7,  96, 24, (u8)(SPR_DIGIT0 + (f / 10) * 4));   /* FPS十の位 */
      vdp_sprite_pos(8, 104, 24, (u8)(SPR_DIGIT0 + (f % 10) * 4));   /* FPS一の位 */
      vdp_sprite_pos(9,   72, 40, 144);   /* M */
      vdp_sprite_pos(10,  80, 40, 148);   /* A */
      vdp_sprite_pos(11,  88, 40, 152);   /* S */
      vdp_sprite_pos(12,  96, 40, 156);   /* K */
      vdp_sprite_pos(13, 112, 40, (u8)(SPR_DIGIT0 + (u8)((g_dbgmask / 10) % 10) * 4));   /* マスク十の位 */
      vdp_sprite_pos(14, 120, 40, (u8)(SPR_DIGIT0 + (u8)(g_dbgmask % 10) * 4)); }        /* マスク一の位 */
#endif
}
