/* scene_ranking.c — ★ランキング(TOP5)の表示とネームエントリー。冷たいシーン(RANK_BANK = bank67)。
   ここへ来る道は 3 つ:
     ・アトラクトモード: タイトル(放置 20 秒)→ デモ → **ここ(8 秒)** → タイトル
     ・ゲームオーバー(コンティニューしない/できない)→ ここ
     ・エンディングの後 → ここ
   ゲームの終わりから来て TOP5 に入っていれば、先にネームエントリーをしてから表を出す(新しい行は点滅)。
   設計は docs/デモとランキング設計.md。表の置き場と形は rank.h。
   ★エンディングと同じく update の中で描いて待ち、終わったら次のシーンを返す(ブロッキング)。
   ★音は鳴らさない(scene.c の scene_bgm で BGM_OFF)。タイトルへ戻ると曲が頭から鳴る。 */
#include "input.h"
#include "scene.h"
#include "vdp.h"
#include "rank.h"
#include "sound.h"    /* snd_active(DEMO_END が効果音の終わりを待つ) */

#define RK_BG   1      /* 背景 = 濃紺(エンディングと同じ) */
#define RK_TX   15     /* 文字 = 白 */
#define RK_TOP  12     /* 1 位・見出し = 橙 */
#define RK_NEW  11     /* 新しく入った行 = 赤(点滅) */
#define RK_HOLD 480    /* 表を出しておく時間: 8 秒(60 フレーム/秒) */
#define NE_TIME 1800   /* ネームエントリーの制限時間: 30 秒 */
#define NE_ED   26     /* 文字の番号: 0〜25 = A〜Z、26 = [ED](終わり) */

#define JIFFY (*(volatile u16 *)0xFC9E)

static const char *const ord[RANK_N] = { "1ST", "2ND", "3RD", "4TH", "5TH" };

/* [ED] = 「END」を 1 マスに詰めた字(左 4 列に E、右 4 列に D)。昔のゲームセンターのネームエントリーの終わりの字。 */
static const u8 glyph_ed[8] = { 0x00, 0xEC, 0x8A, 0xCA, 0x8A, 0xEC, 0x00, 0x00 };

/* 1 行 = 「1ST   10000   SZK」(17 文字 = 136 ドット)。点数は右寄せ、頭の 0 は空白にする。 */
static void rank_line(u8 i, u8 col) {
    char b[18];
    u16 v = g_rank[i].score;
    u8 k;
    for (k = 0; k < 17; k++) b[k] = ' ';
    b[17] = 0;
    b[0] = ord[i][0]; b[1] = ord[i][1]; b[2] = ord[i][2];
    k = 10;                                   /* 点数の最後の桁(6〜10 桁目) */
    do { b[k--] = (char)('0' + (u8)(v % 10)); v /= 10; } while (v && k >= 6);
    b[14] = g_rank[i].name[0]; b[15] = g_rank[i].name[1]; b[16] = g_rank[i].name[2];
    vdp_text(60, (u8)(72 + i * 20), col, RK_BG, b);
}

/* 8x8 の字を 2 倍(16x16)で (x, y) へ。SCREEN5 は 1 バイト = 2 ドット。x は偶数で。 */
static void big_glyph(u8 x, u8 y, const u8 *g, u8 fg) {
    u8 r, c, bits, b;
    vdp_cmd_wait();   /* ★塗り(vdp_fill)が終わってから書く。終わる前に書くと上から塗られる(エンディングの I の欠けと同じ) */
    for (r = 0; r < 16; r++) {
        bits = g[r >> 1];
        vdp_write_addr((u16)((u16)(u8)(y + r) * 128 + (x >> 1)));
        for (c = 0; c < 8; c++, bits = (u8)(bits << 1)) {
            b = (bits & 0x80) ? fg : RK_BG;
            vdp_data((u8)((b << 4) | b));     /* 1 ドットを横 2 ドットに */
        }
    }
}

static void big_char(u8 x, u8 y, u8 code, u8 fg) {
    if (code == NE_ED) big_glyph(x, y, glyph_ed, fg);
    else               big_glyph(x, y, vdp_glyph((u8)(code < NE_ED ? 'A' + code : ' ')), fg);
}

/* 何位に入るか(入らなければ RANK_N)。同点は既にある方が上 */
static u8 rank_pos(u16 sc) {
    u8 i;
    for (i = 0; i < RANK_N; i++) if (sc > g_rank[i].score) return i;
    return RANK_N;
}

/* 2 桁の数(残り時間)を文字列に */
static void two_digits(char *b, u8 v) { b[0] = (char)('0' + v / 10); b[1] = (char)('0' + v % 10); b[2] = 0; }

/* ---- ネームエントリー。決まった名前を nm[0..2] に('A'〜'Z' / 空白) ---- */
#define NE_X0 104      /* 名前の 3 マスの左端(16 ドット + 間 8 ドット = 24 ずつ。3 マスで 64 ドット、中央) */
#define NE_Y  120
static void name_entry(u8 pos, u16 sc, char *nm) {
    char b[18];
    u8 slot = 0, code = 0, lastsec = 0xFF, rep = 0, k;
    u16 t0 = JIFFY, el;
    nm[0] = nm[1] = nm[2] = ' ';
    vdp_fill(0, 0, 256, 212, RK_BG);
    vdp_text_s(48, 20, RK_TOP, RK_BG, 2, "NEW RECORD");       /* 10 字 × 16 = 160 ドット。★フォントに ! は無い */
    for (k = 0; k < 17; k++) b[k] = ' ';
    b[17] = 0;
    b[0] = ord[pos][0]; b[1] = ord[pos][1]; b[2] = ord[pos][2];
    { u16 v = sc; k = 16; do { b[k--] = (char)('0' + (u8)(v % 10)); v /= 10; } while (v && k >= 12); }
    vdp_text(60, 60, RK_TX, RK_BG, b);                      /* 「2ND         12345」 */
    vdp_text(68, 88, RK_TX, RK_BG, "ENTER YOUR NAME");        /* 15 字 = 120 ドット */
    vdp_text(32, 184, RK_TX, RK_BG, "LEFT RIGHT  A OK  B BACK");   /* 24 字 = 192 ドット。★フォントに < > は無い */
    for (;;) {
        u8 e, in;
        vdp_wait_frame();
        input_poll();
        e = g_input_edge; in = g_input;
        el = (u16)(JIFFY - t0);
        /* ---- 残り時間。時間切れは、そこまでの文字で決まり ---- */
        if (el >= NE_TIME) break;
        { u8 sec = (u8)((NE_TIME - el + 59) / 60);
          if (sec != lastsec) { lastsec = sec; two_digits(b, sec);
                                vdp_text(100, 156, RK_TX, RK_BG, "TIME ");
                                vdp_text(140, 156, (sec <= 5) ? RK_NEW : RK_TX, RK_BG, b); } }
        /* ---- ←→ で文字を回す(押し続けると連続で送る) ---- */
        if (in & (INP_LEFT | INP_RIGHT)) {
            if ((e & (INP_LEFT | INP_RIGHT)) || ++rep >= 20) {
                if (!(e & (INP_LEFT | INP_RIGHT))) rep = 14;          /* 2 回目からは 6 フレームごと */
                else                               rep = 0;
                if (in & INP_RIGHT) code = (u8)(code >= NE_ED ? 0 : code + 1);
                else                code = (u8)(code == 0 ? NE_ED : code - 1);
            }
        } else rep = 0;
        /* ---- A で決める / B で 1 文字戻る ---- */
        if (e & INP_TRIG) {
            if (code == NE_ED) break;                                  /* [ED] = ここで終わり */
            nm[slot] = (char)('A' + code);
            big_char((u8)(NE_X0 + slot * 24), NE_Y, code, RK_TX);
            vdp_fill((u16)(NE_X0 + slot * 24), NE_Y + 18, 16, 2, RK_BG);   /* 下線を消す */
            if (++slot >= RANK_NAME) break;                            /* 3 文字入れたら終わり */
        } else if ((e & INP_TRIGB) && slot) {
            vdp_fill((u16)(NE_X0 + slot * 24), NE_Y, 16, 20, RK_BG);   /* 今のマスを消して前のマスへ */
            slot--;
            code = (u8)(nm[slot] - 'A');                               /* 戻った先の字を候補に(先に読んでから消す) */
            nm[slot] = ' ';
        }
        /* ---- 今のマス: 候補の字を点滅、下に下線 ---- */
        if (slot < RANK_NAME) {
            u8 x = (u8)(NE_X0 + slot * 24);
            big_char(x, NE_Y, code, (el & 16) ? RK_TOP : RK_TX);
            vdp_fill(x, NE_Y + 18, 16, 2, RK_TOP);
        }
    }
}

/* pos に入れる(下を 1 つずつずらす) */
static void rank_insert(u8 pos, u16 sc, const char *nm) {
    u8 i;
    for (i = RANK_N - 1; i > pos; i--) g_rank[i] = g_rank[i - 1];
    g_rank[pos].score = sc;
    g_rank[pos].name[0] = nm[0]; g_rank[pos].name[1] = nm[1]; g_rank[pos].name[2] = nm[2];
    if (g_rank[0].score > g_hiscore) g_hiscore = g_rank[0].score;
}

static u8 run_ranking(void) {
    u16 f, sc;
    u8 i, pos;
    char nm[3];
    __asm ei __endasm;            /* ★_bcall は di のまま来る。vdp_wait_frame は割込みで進む JIFFY を待つ */
    /* ★入るかどうかは、デモの設定を戻す**前**に決める(デモは無敵で遊んだ点が g_score に残っている)。
       無敵の設定で遊んだ点は記録しない(開発用の設定なので)。 */
    sc = g_score;
    pos = (!g_demo && !g_invinc) ? rank_pos(sc) : RANK_N;
    g_score = 0;                  /* ★見たら捨てる(デモの後のランキングで二重に入らないように) */
    DEMO_END();                   /* ★デモから来たら、書き換えた設定を元に戻す */
    /* ★面(デモ・ゲームオーバー)から来ると画面モードは同じ SCREEN5 なので CHGMOD が走らず、面の VDP の状態
       (スプライトの表・拡大・MSK・パレットの天候など)が残る。ここで SCREEN5 を張り直して素の状態にする */
    vdp_screen5();
    vdp_palette_game();
    vdp_sprite_hide_from(0);
    vdp_set_vscroll(0);
    vdp_set_hscroll(0, 0);
    vdp_set_display_page(0);
    if (pos < RANK_N) {
        name_entry(pos, sc, nm);
        rank_insert(pos, sc, nm);
    }
    vdp_fill(0, 0, 256, 212, RK_BG);
    vdp_text_s(80, 28, RK_TOP, RK_BG, 2, "BEST 5");   /* 6 字 × 16 = 96 ドット、中央 */
    for (i = 0; i < RANK_N; i++) rank_line(i, (i == 0) ? RK_TOP : RK_TX);
    for (f = 0; f < RK_HOLD; f++) {
        vdp_wait_frame();
        if (pos < RANK_N && (f & 15) == 0) rank_line(pos, (f & 16) ? RK_NEW : RK_TX);   /* 新しい行を点滅 */
        if ((f & 31) == 0) {                                  /* ★PRESS SPACE KEY を約 0.5 秒ずつ点滅(タイトル・デモとそろえる) */
            if (f & 32) vdp_fill(68, 184, 120, 8, RK_BG);
            else        vdp_text(68, 184, RK_TX, RK_BG, "PRESS SPACE KEY");   /* 15 字 = 120 ドットを中央に */
        }
        input_poll();
        if (g_input_edge & INP_TRIG) break;
    }
    return SC_TITLE;
}

void banked_entry(void) {
    if (g_scene_phase == 0) { vdp_set_display_page(0); }
    else g_scene_ret = run_ranking();
}
