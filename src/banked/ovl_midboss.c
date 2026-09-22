/* ovl_midboss.c — 1面の中ボス Fw 200 コンドル(ROADMAP B4)。中ボス用オーバレイ(OVL8_BANK)で動く。
   ★大きさは 64x64(16x16 を 4x4)。32x32 では「中ボスの迫力が無い」、2機にしても同じと実機で指摘された。
   ★回転は 32 方向。色は部位ごと(迷彩/エンジン/ガラス/国籍標識)で、向きごとに焼いてある(tools/gen_fw200.py)。
     1色だと「いかにもMSX」、画面の行で陰影を付けると「旋回で色が変わる」と実機で指摘された。
     16x16 のマスごとに本体(1行1色)＋最大2マスだけ重ね(1行1色)＋影4(2x2)。1面も影を付ける(中ボス共通の決まり)ため重ねを6→2に減らした。
   ★向きのデータは ROM にあり、オーバレイからは読めない: 向きを変えるときは g_mb_req に置き、常駐がフレームの終わりに
     MB_BUF へ読む。次のフレームでここがパターン・色・位置をまとめて書く(midboss.h)。
   ★スプライトは最低優先の末尾(32-n..31)。重ねを前(優先)、本体を後ろに、絵のあるマスだけ詰める(16〜18枚)。
     エンティティは g_spr_limit=32-n の手前まで。混んだ走査線では中ボスの方が先に欠ける(弾が見えなくなるよりよい)。
   ★呼ぶのは ent_draw_all の**後**: その末尾に書かれる停止マーカ(Y=216)を、中ボスの手前まで隠しスプライトで埋め直すため。
   ★動き(突進): 自機の位置を見て 8 方向のどれかを決め、向き直ってから一直線に突っ込む。止まったらまた自機を見る、の繰り返し。
     回転が見えるのは向き直る間だけ(ずっと回っているのは「せわしない」と実機で指摘)。向き直る間に自機狙いの3方向弾。
     ★弾は**背景に描く**(ovl_bgbul.c)。スプライトで撃つと、中ボス18枚＋HUD9枚＋アイコンで自機と弾に4枠しか残らず、
       弾がちらついた(実機で指摘)。その容量のため、この中ボスの間は衝撃波が出ない(5面と同じ)。
     胴体に触れると被弾。40秒で逃げる。撃墜で 500点＋残り時間ボーナス＋メガクラッシュ1回。
   ★突進の予告: 突っ込む前に MB_WARN_T フレーム止まって白く明滅する(大きな体当たりを避ける合図)。
   ★手負い: 耐久が半分を切ったら片側の内側エンジンが燃え(爆発を繰り返し出す)、向き直りが倍速・待ちが半分・突進が 4px/f になる。
   ★終わったら g_mb=MB_RESTORE にするだけ。オーバレイの入れ替えとパターンの書き戻しは常駐が行う。 */
#include "types.h"
#include "vdp.h"
#include "entity.h"
#include "fire.h"       /* aim_dir / dvx,dvy */
#include "gamestate.h"
#include "player.h"     /* g_player_x/y */
#include "sound.h"
#include "midboss.h"
#include "bgbul.h"      /* 背景に描く弾(ovl_bgbul.c。5面と共有) */
#include "curtain.h"    /* curtain_reset: 背景弾の表を借りた CPU 弾幕の帯を返す */

extern u8 rnd(void);

#define MB_HP       200     /* 半分単位(通常弾 2)=通常弾で100発(400 は1面には硬すぎた) */
#define MB_TIMEOUT  1200    /* 40秒で逃げる */
#define MB_AIM_T    30      /* 向き直りの最短フレーム(180°の向き直りは 32 フレーム) */
#define MB_DASH_T   45      /* 突進のフレーム数(3px/f で 135 ドット。画面の端に着いたらそこで止まる) */
#define MB_WARN_T   12      /* 突進の予告(止まって明滅)のフレーム数。突進のフレームに含まない */
#define MB_HURT     (MB_HP / 2)   /* これを切ったら手負い */
#define MB_PIERCE   0x7ABC  /* 貫通弾に付ける印(同じ弾が毎フレーム当たらないように) */
#define MB_SH_OFF   12      /* 影を右下へずらす量(低空) */

enum { ST_ENTER, ST_AIM, ST_DASH, ST_LEAVE, ST_DIE, ST_DONE };

/* 8方向の突進速度(0=上, 時計回り)。斜めは 2,2 で約 2.8 */
static const s8 dx8[8] = { 0, 2, 3, 2, 0, -2, -3, -2 };
static const s8 dy8[8] = { -3, -2, 0, 2, 3, 2, 0, -2 };
static const s8 hx8[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };   /* 向き直る間の惰性(1px/f) */
static const s8 hy8[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static u8  st, st_t, d8, fcur, flast, flash, fcool, coldirty, hurt, lastc;   /* fcool=白く光った後、次の白を出さない残り */
static u16 t, hp;   /* bm=本体のマス / om=重ねのマス(いま VRAM に載っている向き) */
static s16 bx, by;          /* 64x64 の左上(画面座標) */

void ovl_mb_init(void) {
    st = ST_ENTER; t = 0; hp = MB_HP;
    bx = 96; by = -64;
    fcur = 16; flast = 16; flash = 0; fcool = 0; coldirty = 1; hurt = 0;
    g_mb_n = 0; g_mb_req = 16; g_mb_new = 0;   /* 最初の向き(真下)は常駐がすぐ読む */
    lastc = g_crush;
    pb_init();
}

static void turn_to(u8 tgt) {
    u8 d = (u8)((tgt - fcur) & 31);
    if (d && ((t & 1) || hurt)) fcur = (u8)((fcur + ((d < 16) ? 1 : 31)) & 31);
}

/* ent_draw_all の後に呼ぶ: 重ね→本体→影の順に末尾の枠へ置き、エンティティとの間の枠は隠して停止マーカを消す。
   影はいつも同じ色(13)。被弾や予告で白くなるのは機体だけ(中ボス共通の決まり)。 */
static void put_sprites(void) {
    u8 c, j = 0, pass, sl, s0 = (u8)(32 - g_mb_n);
    const u8 *col = (const u8 *)(MB_BUF + 708);
    for (sl = g_spr_used; sl < s0; sl++) vdp_sprite_pos(sl, 0, 220, MB_CELL_PAT(0));
    sl = s0;
    for (pass = 0; pass < 3; pass++) {
        u16 m = (pass == 0) ? g_mb_om : (pass == 1) ? g_mb_bm : 0x0660;   /* 影は 2x2(マス 5,6,9,10) */
        for (c = 0; c < 16; c++) {
            s16 x, y;
            u8 off;
            if (!(m & (1u << c))) continue;
            x = (s16)(bx + ((c & 3) << 4) + ((pass == 2) ? MB_SH_OFF : 0));
            y = (s16)(by + ((c >> 2) << 4) + ((pass == 2) ? MB_SH_OFF : 0));
            if (coldirty) {
                if (pass == 2) vdp_sprite_color(sl, 13);
                else if (flash) vdp_sprite_color(sl, 15);   /* 被弾/予告=白 */
                else vdp_sprite_color_tab(sl, col);
            }
            if (pass < 2) col += 16;
            off = (u8)(x < 0 || x > 240 || y < -16 || y > 212);
            vdp_sprite_pos(sl, off ? 0 : (u8)x, off ? 220 : (u8)y,
                           (pass == 0) ? MB_OV_PAT(j) : (pass == 1) ? MB_CELL_PAT(c) : MB_OV_PAT(j));
            j++; sl++;
        }
        if (pass == 1) j = 2;                      /* 影の絵は重ねの後ろ(中くらいの艦載機の枠 2..5) */
    }
    coldirty = 0;
}

/* 自機弾との当たり。威力は弾が持つ(b->hp, 半分単位)。3段目の貫通弾は1発につき1回だけ当たる。 */
static void hit_test(void) {
    u8 i;
    Entity *e = ent_pool();
    for (i = 0; i < ENT_MAX; i++, e++) {
        s16 dx, dy;
        if (!e->active || e->type != ET_BULLET || e->team != TEAM_PLAYER) continue;
        dx = (s16)(e->x - bx - 24); if (dx < 0) dx = -dx; if (dx >= 24) continue;
        dy = (s16)(e->y - by - 24); if (dy < 0) dy = -dy; if (dy >= 22) continue;
        if (e->ax == (s16)MB_PIERCE) continue;
        if (g_pwr < PWR_MAX) e->active = 0; else e->ax = (s16)MB_PIERCE;
        hp = (hp > e->hp) ? (u16)(hp - e->hp) : 0;
        if (!fcool) { flash = 1; fcool = 4; coldirty = 1; }   /* 白は1フレームだけ。その後3フレームは光らせない(当て続けてもチカチカ) */
        if ((t & 3) == 0) { ent_spawn_spark(e->x, e->y); sfx(1, SFX_HIT); }
    }
}

static void shoot(void) {
    s16 ox = (s16)(bx + 24), oy = (s16)(by + 24);
    u8 a;
    if (oy < 0 || oy > 176) return;
    a = (u8)(aim_dir(ox, oy, g_player_x, g_player_y) - 2);
    {   u8 k;   /* 弾の四角(4x4)の左上=中心-2。速さはスプライトの弾と同じ 3 ドット/フレーム(1/8 単位で dv*3) */
        for (k = 0; k < 3; k++, a += 2) pb_add((s16)(ox + 6), (s16)(oy + 6), (s8)(dvx[a & 31] * 3), (s8)(dvy[a & 31] * 3)); }
    sfx(2, SFX_EFIRE);
}

/* 自機の位置を見て突進の方向を決め、向き直りへ。画面の下半分に居るときは自機の真上の上空へ引き返す
   (でないと自機に重なったまま下端で止まり、突っ込みが続かない)。 */
static void aim(void) {
    d8 = (u8)(((aim_dir((s16)(bx + 24), (s16)(by + 24), g_player_x, (by > 70) ? 8 : g_player_y) + 2) >> 2) & 7);
    st = ST_AIM; st_t = 0;
}

/* 画面の範囲(機体の 1/4 までははみ出してよい)に留める。はみ出しかけたら 1 */
static u8 move(s8 vx, s8 vy) {
    s16 x = (s16)(bx + vx), y = (s16)(by + vy);
    u8 out = 0;
    if (x < -16) { x = -16; out = 1; } else if (x > 208) { x = 208; out = 1; }
    if (y < -16) { y = -16; out = 1; } else if (y > 150) { y = 150; out = 1; }
    bx = x; by = y;
    return out;
}

void ovl_mb_frame(void) {
    u8 tgt = fcur;
    if (st == ST_DONE) return;
    if (g_mb_recol) { g_mb_recol = 0; coldirty = 1; }   /* ★津波が色表を奪った(scene_stage)。塗り直す */
    /* ★ボムを使った: 津波が画面の弾を消し、リングも描き直した(背景の弾の絵はもう無い)ので表を空にする。
       ★表と海のひな形は CPU 弾幕の帯を借りているので、ボムの curtain_reset(常駐)がその帯に 0 を書いている。
         ひな形も読み直す(でないと弾を消した跡に黒い点が残る) */
    if (g_crush < lastc) pb_init();
    lastc = g_crush;
    pb_update();
    t++;
    if (fcool) fcool--;
    if (flash && !--flash) coldirty = 1;
    switch (st) {
    case ST_ENTER:
        by += 2; tgt = 16;
        if (by >= 8) aim();
        hit_test();
        break;
    case ST_AIM: {                          /* 向き直りながら惰性でゆっくり進む。途中で撃つ */
        u8 f8 = (u8)(((fcur + 2) >> 2) & 7);
        move(hx8[f8], hy8[f8]);
        tgt = (u8)(d8 << 2);
        if (++st_t == 12) shoot();
        if (st_t >= (hurt ? MB_AIM_T / 2 : MB_AIM_T) && fcur == tgt) { st = ST_DASH; st_t = 0; }
        hit_test();
        break; }
    case ST_DASH:                           /* 予告(止まって明滅)のあと、一直線に突っ込む */
        if (++st_t <= MB_WARN_T) {
            if ((st_t & 3) == 1) { flash = 1; coldirty = 1; }
        } else if (move(dx8[d8], dy8[d8]) || (hurt && move(hx8[d8], hy8[d8])) || st_t >= MB_WARN_T + MB_DASH_T) aim();
        hit_test();
        break;
    case ST_LEAVE:                          /* 上へ向き直りながら上へ抜ける */
        tgt = 0;
        by -= 3;
        if (by < -72) st = ST_DONE;
        break;
    case ST_DIE:
        by++;
        if ((t & 3) == 0) {
            ent_spawn_explosion((s16)(bx + 8 + (rnd() & 31)), (s16)(by + 8 + (rnd() & 31)));
            if ((t & 7) == 0) sfx(2, SFX_BOOM);
        }
        if (((t >> 1) & 1) != flash) { flash = (u8)((t >> 1) & 1); coldirty = 1; }
        if (t >= 60) st = ST_DONE;
        break;
    }
    if (st == ST_DONE) { pb_clear(); curtain_reset(); mb_finish(); return; }
    if (st == ST_AIM && t >= MB_TIMEOUT) st = ST_LEAVE;
    if (st == ST_AIM || st == ST_DASH) {   /* 胴体に触れたら被弾(翼は当たらない。宙返り中は ent_player_hit が無視する) */
        s16 dx = (s16)(bx + 24 - g_player_x), dy = (s16)(by + 24 - g_player_y);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx < 12 && dy < 12) ent_player_hit(g_player_x, g_player_y);
    }
    if (st < ST_LEAVE && hp && hp < MB_HURT) {   /* 手負い: 片側の内側エンジン(機首方向へ約9・左へ約18ドット)が燃える */
        u8 f8 = (u8)(((fcur + 2) >> 2) & 7), l8 = (u8)((f8 + 6) & 7);
        if (!hurt) { hurt = 1; g_shake = 8; sfx(2, SFX_BOOM); }
        if ((t & 7) == 0)
            ent_spawn_explosion((s16)(bx + 24 + dx8[f8] * 3 + dx8[l8] * 6), (s16)(by + 24 + dy8[f8] * 3 + dy8[l8] * 6));
    }
    if (st < ST_LEAVE && hp == 0) {   /* 撃墜 */
        u16 pts = (u16)(500 + (MB_TIMEOUT - t) / 3);    /* 残り1秒=10点 */
        st = ST_DIE; t = 0;
        g_score = (g_score > 65535u - pts) ? 65535u : (u16)(g_score + pts);
        scorepop_add((s16)(bx + 24), (s16)(by + 24), pts);
        if (g_crush < CRUSH_MAX) g_crush++;
        g_hitstop = 4; g_shake = 8;
        shock_at((s16)(by + 32));
        sfx(2, SFX_BOOM);
    }
    turn_to(tgt);
    if (fcur != flast && g_mb_req == 0xFF && !g_mb_new) { flast = fcur; g_mb_req = fcur; }   /* 読み込み中は待つ */
    if (g_mb_new) { mb_upload(320, 1); coldirty = 1; }   /* 常駐が読んだ絵をパターン表へ(midboss.h) */
    put_sprites();
}
