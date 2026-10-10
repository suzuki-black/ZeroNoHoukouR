/* demo.c — ★アトラクトモードのデモ(自動操縦)。冷たいバンク(DEMO_BANK = bank68)。
   設計は docs/デモとランキング設計.md。
   ・本物の面を、無敵・無音で動かす。入力の記録の再生はしない(乱数・処理落ち・割込みの時刻で
     進み方がずれ、記録とゲームが食い違う)。毎フレーム入力を作って差し込むだけ。
   ・scene.c のループが、デモ中は毎フレーム面の処理の**後**でここを呼ぶ(input_poll は呼ばない)。
     ここで本物の入力を読み、抜けるかを決めてから、次のフレームの g_input / g_input_edge を自動操縦の値にする。
   ・抜けるときは g_scene_ret を直接書く(scene.c はその直後に遷移を見る)。
     設定を元に戻すのは、抜けた先のシーン(ランキング・タイトル)の入場で(DEMO_END)。
   ・先頭の枠(0〜6)に PUSH SPACE KEY を出す。HUD はデモ中は描かない(scene.c)ので、その枠(24〜31)はゲームに回す
     (scene_stage.c の上限 sp_top と rank.h の SPR_GAME_TOP)。
     文字の絵は数字の絵(SPR_DIGIT0〜)を借りる。数字を使う得点のポップはデモ中は出さない(entity.c)。
     本物のゲームでは面の準備(hud_init)が数字を描き直すので元に戻る。
   ★状態は static に置かない(0xE000〜は面の途中で呼ぶ他のバンクと取り合う)。全部 rank.h の 7B。 */
#include "input.h"
#include "scene.h"
#include "vdp.h"
#include "sprites.h"
#include "hud.h"
#include "player.h"
#include "entity.h"     /* g_spr_base(デモ中は先頭 7 枠を文字に譲る) */
#include "rank.h"

#define JIFFY    (*(volatile u16 *)0xFC9E)
#define DEMO_LEN 1500   /* 25 秒(60 ティック/秒) */
#define TXT_Y    194    /* PUSH SPACE KEY の高さ(最下段)。★最上段は敵が出てくる行で、横 1 列 8 枚の制限に
                           かかって後ろの文字が欠けた(HUD の枠は最低優先)。自機は下の自動操縦で上に保つ */
#define TXT_X    72     /* 14 文字 × 8 = 112 ドットを中央に */

/* 16x16 1 枚に 8x8 の文字を 2 つ(左 8 列/右 8 列)。空白は ' '。 */
static const char txt[7][2] = {
    { 'P', 'U' }, { 'S', 'H' }, { ' ', 'S' }, { 'P', 'A' }, { 'C', 'E' }, { ' ', 'K' }, { 'E', 'Y' }
};

/* 自動操縦の目標(自機の x)。経過時間で順に切り替える(状態を持たない) */
static const u8 tgt_x[8] = { 128, 60, 190, 100, 170, 40, 210, 128 };

/* ★文字は**いちばん優先度の高い枠(0〜6)**に置く。HUD の枠(最低優先)に置くと、戦艦の砲台や弾が同じ行に来たとき
   横 1 列 8 枚の制限で後ろの文字が欠けた(4・5 面の戦艦。2026-10-09)。エンティティは g_spr_base=7 から描かせる
   (entity.c はもともと先頭を譲る作り。g_spr_base は面の処理が毎フレーム決めるので、scene_stage.c 側で 7 にする)。混んだ行では文字の代わりに敵の弾が欠けるが、デモなので構わない。
   ★宙返り(ovl_rot)は 0〜3 番を使うので、デモの自動操縦では宙返りをしない。 */
#define TXT_SL0 0
static void text_setup(void) {
    u8 buf[32];
    u8 i, r;
    for (i = 0; i < 7; i++) {
        const u8 *g;
        for (r = 0; r < 32; r++) buf[r] = 0;
        g = vdp_glyph((u8)txt[i][0]);
        if (txt[i][0] != ' ') for (r = 0; r < 8; r++) buf[r] = g[r];
        g = vdp_glyph((u8)txt[i][1]);
        if (txt[i][1] != ' ') for (r = 0; r < 8; r++) buf[16 + r] = g[r];
        vdp_sprite_pattern((u8)(SPR_DIGIT0 + i * 4), buf);
        vdp_sprite_color((u8)(TXT_SL0 + i), 15);
    }
    ent_spr_cache_inval(0);   /* ★HUD の枠(24〜31)はデモ中ゲームが使う。HUD の準備が色表を直接書いたので、色の控えを捨てて書き直させる */
}

static void text_draw(void) {
    u8 i;
    for (i = 0; i < 7; i++) {
        vdp_sprite_pos((u8)(TXT_SL0 + i), (u8)(TXT_X + i * 16), TXT_Y, (u8)(SPR_DIGIT0 + i * 4));
        vdp_sprite_color((u8)(TXT_SL0 + i), 15);   /* ★毎フレーム白に(デモの直前に敵が使っていた枠の色の書込みが残っていて、赤などに化けた) */
    }
}

void banked_entry(void) {
    u16 el;
    u8 inp, x, y, tx;

    /* ★面に入ってから。タイトルが g_demo=1 にしたフレームのうちにも scene.c はここを呼ぶ(呼ぶのは面の処理の後)。
       そこで始めると、文字の絵を描くのも時計を始めるのも面の準備より前になり、準備が数字の絵で描き直して
       「0123456」と出た(デモも準備のぶん短くなった)。 */
    if (g_scene != SC_STAGE) return;
    input_poll();                                   /* 本物の入力(抜けるかの判断だけに使う) */
    if (g_demo == 1) {                              /* 最初のフレーム */
        g_demo = 2;
        g_demo_t0 = JIFFY;
        g_demo_prev = 0;
        text_setup();
    }
    el = (u16)(JIFFY - g_demo_t0);
    {
        /* ★押した瞬間(g_input_edge)では判定できない: input_poll は「前の g_input」との差で瞬間を作るが、
             その g_input は下で自動操縦の値(トリガ押しっぱなし)に置き換えているので、本物のトリガは
             「ずっと押している」扱いになり瞬間が出ない(最初そう書いて抜けられなかった)。押されているかで見る。
             デモはタイトルを 20 秒放置してから始まるので、始まった時点で押されたままということは無い。 */
        if (g_input & (INP_TRIG | INP_TRIGB)) g_scene_ret = SC_TITLE;     /* 触ったらタイトルへ */
        else if (el >= DEMO_LEN)                    g_scene_ret = SC_RANKING;   /* 時間切れはランキングへ */
    }

    /* ---- 自動操縦: 撃ち続けながら、目標の x へ寄る。y は下の方を保つ ---- */
    inp = INP_TRIG;
    x = g_player_x; y = g_player_y;
    tx = tgt_x[(u8)((el / 80) & 7)];
    if ((u8)(x + 6) < tx)      inp |= INP_RIGHT;
    else if (x > (u8)(tx + 6)) inp |= INP_LEFT;
    if (y > 150)      inp |= INP_UP;      /* ★自機と影を文字の行(TXT_Y)へ下ろさない */
    else if (y < 110) inp |= INP_DOWN;
    g_input = inp;
    g_input_edge = (u8)(inp & ~g_demo_prev);
    g_demo_prev = inp;

    text_draw();
}
