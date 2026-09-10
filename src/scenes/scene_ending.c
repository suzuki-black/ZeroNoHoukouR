/* scene_ending.c — ★エンディング(冷たいシーン=bank7)。静かなED曲(track2)＋
   なめらかな縦スクロールのスタッフロール(旧版 run_ending 移植)。
   技法: page0(0..255)を256pxリングにし、R#23(縦スクロール)を毎フレーム更新。スクロールで下端
   (可視域直下の非可視帯)に入ってくる行を、その場で **1フレーム1文字ずつ(end_putc)** 先読み描画する。
   ★旧実装は全行をバッファBへ事前描画(B=最大31行の固定上限)。1文字分散に変えて行数上限を撤廃
     (=物語/クレジットの分量に融通が利く)。★描画は必ず end_putc(cmd_wait無し)で行う=毎フレーム
     vdp_cmd_wait を呼ぶとVBLANK割込のタイミングを乱してスクロールがカクつく(実測で確定)。非可視帯なので tear なし。
   ※banked scene: 常駐 vdp_* を注入番地で呼ぶ。データ窓(bank_data)は触らない。 */
#include "vdp.h"
#include "input.h"
#include "scene.h"

#define END_SPD  4     /* 4フレームで1px=約15px/s(ゆっくり) */
#define END_BG   1     /* 背景=濃紺 */
#define END_TX   15    /* 文字=白 */

/* クレジット行(旧版 ENDALL 準拠, 描画可能文字 [A-Z0-9 -] のみ)。""=空行(間隔)。 */
static const char *const credits[] = {
    /* ★エンディングストーリー: READMEあらすじ(走れメロス冒頭のオマージュ)への返歌=結末のオマージュ。
       暖かくユーモラスに締める。会話は追加した '"' / ',' グリフを使用。 */
    "IN HIS LONE ZERO",
    "THE SAMURAI HAD SUNK",
    "EVERY LAST BATTLESHIP.",
    "",
    "HIS GREAT WORK DONE,",
    "HE CAME HOME TO HIS VILLAGE",
    "STILL IN THE CLOTHES",
    "THE FIERCE BATTLE HAD TORN.",
    "",
    "THE VILLAGERS CROWDED ROUND,",
    "AND WITH ONE VOICE",
    "THEY PRAISED HIS DEED.",
    "",
    "THEN A YOUNG GIRL HELD OUT",
    "A CRIMSON CLOAK TO HIM.",
    "THE SAMURAI STOOD AT A LOSS.",
    "",
    "A GOOD FRIEND OF THE VILLAGE,",
    "QUICK TO CATCH ON,",
    "KINDLY TOLD HIM,",
    "",
    "\"WHY, YOU ARE ALL BUT NAKED.",
    "HURRY, PUT ON THAT CLOAK.",
    "THIS DEAR GIRL CANNOT BEAR",
    "TO HAVE ALL SEE YOU BARE.\"",
    "",
    "THE SAMURAI BLUSHED",
    "A DEEP, DEEP RED.",
    "",
    "",
    "STAFF",
    "",
    "ORIGINAL PLAN",           /* 原案 */
    "SUZUKI-BLACK",
    "",
    "ORIGINAL CONCEPT",
    "SUZUKI-BLACK",
    "",
    "DIRECTION",
    "SUZUKI-BLACK",
    "",
    "PROGRAM",
    "CLAUDE CODE",
    "",
    "GRAPHICS",
    "CLAUDE CODE",
    "",
    "SOUND",
    "CLAUDE CODE",
    "",
    "TITLE ILLUSTRATION",
    "MS COPILOT",
    "",
    "CO-TITLE ILLUSTRATION",   /* 共同=CO-(映画クレジット流) */
    "CLAUDE CODE",
};
#define NROWS ((u8)(sizeof(credits) / sizeof(credits[0])))

/* 中央寄せX(8px/char)。 */
static u8 center_x(const char *s) {
    u8 n = 0; while (s[n]) n++;
    return (u8)((256 - (u16)n * 8) / 2);
}

/* ★1文字だけ page0(px,py) へ描く最小blit(vdp_cmd_wait を呼ばない=直接VRAM書込のみ)。
   vdp_text の per-call オーバーヘッド(cmd_waitのCEスピン等)がカクつき原因かの切り分け用。 */
static void end_putc(u8 px, u8 py, char c) {
    const u8 *g = vdp_glyph((u8)c);
    u8 gr;
    for (gr = 0; gr < 8; gr++) {
        u8 fb = g[gr], b;
        vdp_write_addr((u16)((u16)(u8)(py + gr) * 128 + (px >> 1)));
        for (b = 0; b < 8; b += 2) {
            u8 hi = (u8)((fb & (u8)(0x80 >> b))       ? END_TX : END_BG);
            u8 lo = (u8)((fb & (u8)(0x80 >> (b + 1))) ? END_TX : END_BG);
            vdp_data((u8)((hi << 4) | lo));
        }
    }
}

/* エンディング本体(ブロッキング)。スクロール→THE END→タイトルへ。
   ★行を事前にバッファBへ描く方式(B=最大31行の固定上限)をやめ、スクロールで下端に入る行だけを
     その場で直接描画する(=クレジット行数の上限が消える=融通が利く)。描画位置は可視域(212px)の
     直下の非可視帯なので、スクロールで上がってくる頃には描き終わっている=tearしない。 */
static u8 run_ending(void) {
    s16 cam, stopcam;
    s16 rrow = -1;    /* ★先読み描画中の行。1フレーム1文字ずつ描くので行の途中状態を保持 */
    u8  rci  = 0xFF;  /* rrow内の描画カーソル(0xFF=この行は描き終わり=次行へ進める) */
    u8  rx   = 0;     /* rrow の中央寄せ開始x */
    u8  sc = 0;
    u16 f;

    /* ★_bcall は banked 実行中ずっと di だが、本体は vdp_wait_frame(JIFFYを割込みで更新)で
       毎フレーム待つ=di のままだと JIFFY が進まず無限ループ(フリーズ)。BGM再生ISRは曲を
       RAM(bgm_ram)から読み 0xA000窓に触れないので、ここで ei しても bank7 窓は壊れない。 */
    __asm ei __endasm;
    vdp_set_vscroll(0);
    vdp_set_hscroll(0, 0);   /* ★横スクロール(蛇行weaveX)も解除=実プレイ(クリア→エンディング)で残ると全体が右に寄る */
    vdp_set_display_page(0);
    vdp_fill(0, 0, 256, 256, END_BG);      /* page0 リングをクリア */

    stopcam = (s16)((NROWS - 1) * 16 + 24);
    cam = -212;
    while (cam < stopcam) {
        /* ★スクロールをカクつかせない要(実測で確定):
           (1) 1行を一気に描かず **1フレームに最大1文字(or 1行クリア)だけ** 描く(char分散)。行は先読み(+1行)
               で前もって描き切るので、上がってくる頃には完成している。→ 行数の上限なし(融通)。
           (2) 文字描画は vdp_text でなく **end_putc(=vdp_cmd_wait を呼ばない直接VRAM書込)** を使う。
               ★vdp_text 内の vdp_cmd_wait(CEポーリング=di/ei+S#2選択の反復)を毎フレーム呼ぶと、VBLANK割込みの
                 タイミングを乱してフレーム超過を量産し、約38%のフレームがカクついた(素のループは0%)。cmd_wait を
                 外したら hitch≒0 に。→ **スクロール中のホットパスで vdp_cmd_wait を毎フレーム呼ぶな**。
           先読みは+1行まで(リング16行/可視≒13.25行=余白約2.75行=44px。基準+13px＋16px=29px<44pxで
           「上端に残る可視行」と衝突しない。+2行=45pxで一番上の可視行を上書き→表示順が乱れる)。 */
        { s16 want = (s16)((cam + 224) >> 4) + 1;
          if (rci == 0xFF) {                        /* 現在行は完了→次行へ(先読み範囲内なら) */
              if (rrow < want) {                    /* ★NROWSを超えても行送りは続ける=末尾の行もBGクリアして
                                                       リングのラップ(16行前の古いクレジット)が再表示されるのを防ぐ */
                  rrow++;
                  { u16 ry = (u16)(((u16)rrow * 16) & 0xFF);
                    vdp_fill(0, ry, 256, 16, END_BG); }        /* 行クリア(空行・終端後も必ずクリア) */
                  if (rrow < (s16)NROWS && credits[rrow][0]) { rx = center_x(credits[rrow]); rci = 0; }  /* 範囲内で文字あり→描画開始 */
                  /* 空行/終端後は rci=0xFF のまま=次フレームで次行へ(クリアのみ) */
              }
          } else {                                  /* 行の途中: 1文字だけ描く(最小blit=cmd_wait無し) */
              end_putc((u8)(rx + rci * 8), (u8)(((u16)rrow * 16) & 0xFF), credits[rrow][rci]);
              rci++;
              if (credits[rrow][rci] == 0) rci = 0xFF;         /* 行末→完了 */
          }
        }
        vdp_wait_frame();
        vdp_set_vscroll((u8)(cam & 0xFF));          /* VBLANK直後にR#23=無 tearing */
        if (++sc >= END_SPD) { sc = 0; cam++; }     /* END_SPDフレームで1px前進 */
        input_poll();
        if (g_input_edge & INP_TRIG) break;         /* SPACEで飛ばせる */
    }

    /* THE END(静止)→ 保持 → タイトル */
    vdp_set_vscroll(0);
    vdp_fill(0, 0, 256, 256, END_BG);
    vdp_text_s((u8)(128 - 7 * 8), 92, END_TX, END_BG, 2, "THE END");   /* 7字×16=112, 中央 */
    for (f = 0; f < 600; f++) {                      /* 約10秒(トリガで即) */
        input_poll();
        if (g_input_edge & INP_TRIG) break;
        vdp_wait_frame();
    }
    return SC_TITLE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) { vdp_set_display_page(0); }   /* init: 表示ページだけ確定 */
    else g_scene_ret = run_ending();                       /* update: 本体を一気に実行しタイトルへ */
}
