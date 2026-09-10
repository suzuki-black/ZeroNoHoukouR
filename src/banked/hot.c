/* hot.c — RAM実行される毎フレームのホットパス: エンティティ更新(ent_update_all+behavior群)＋AA(aa_update/aa_collide)。
   ★このファイルは常駐にリンクせず、hot_ram[] の実番地に単独 --code-loc してバンク格納する(hotcode.h)。
     R800のROMフェッチ律速を外すのが目的なので、必ずRAM上で実行されること。
   ★挙動は scene_stage.c の旧 aa_update/aa_collide と同一(AA走査は§4-2の窓化で可視区間のみに短縮)。
     プールは ent_pool()、AA状態は aa_hot.h の常駐シンボルを参照(RAM実行モジュールにDATAは持たせない)。
   ★当たり判定(ent_resolve_collisions)はRAM化しても実機で速度不変だったため entity.c(ROM)へ戻した。 */
#include "entity.h"
#include "sound.h"
#include "gamestate.h"
#include "scroll.h"       /* SC_SHIP_R0 */
#include "player.h"       /* g_player_x/y */
#include "fire.h"         /* emit/emit_burst/aim_dir */
#include "ship.h"         /* SHIP_NAAG, ship_aag_tables */
#include "sprites.h"     /* SPR_*, barrel_col/flash(behavior群) */
#include "aa_hot.h"       /* AA共有状態(cam/curstage/aa_*)＋rnd/burn_add */
#include "assets_data.h"  /* ship_aagtbl, STAGE_COUNT */

/* ===== エンティティ更新(behavior群 + ent_update_all) : entity.c から移設(RAM実行) ===== */
#define SCR_W 256
#define SCR_H 212

/* ---- behavior: 種別ごとの毎フレーム更新 ---- */
/* ET_NONE/ET_BOUNCER/ET_SHOOTER の no-op behavior(実ゲームでspawnしないデモ枠。enum添字対応の
   ため behaviors[] のスロットは残すが、コードは共有stubで常駐サイズを節約)。 */
static void bh_noop(Entity *e) { (void)e; }

/* 弾: 直進し画面外(±16マージン)で消滅。
   ★スクロールとは完全に独立=毎フレーム画面座標を vx/vy だけ進める(表示上の見かけ速度が一定)。
     蛇行の上り/下り・左右に一切影響されない(甲板追従の補正は入れない)。 */
static void bh_bullet(Entity *e) {
    e->x += e->vx;
    e->y += e->vy;
    /* ★敵弾は艦(背景)と一緒に縦スクロールへ流れる。論理Y自体を流すので、描画・当たり判定・
       画面外消滅・弾数リミッタが全て視覚と一致する(自機弾=TEAM_PLAYERは画面固定のまま)。 */
    if (e->team == TEAM_ENEMY) e->y -= g_scroll_dy;
    if (e->x < -16 || e->x > SCR_W || e->y < -16 || e->y > SCR_H) e->active = 0;
}

/* 8方向単位ベクトル(0=上,1=右上,2=右,3=右下,4=下,5=左下,6=左,7=左上)。 */
static u8 dir8(s16 dx, s16 dy);        /* 前方宣言(bh_turret が使う。定義は下) */
static u8 step_dir(u8 cur, u8 tgt);
static const s8 dirdx8[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const s8 dirdy8[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };

/* 対空砲の時限信管弾: 直進しつつ ftimer(信管)を数え、0で空中炸裂。自機帯(y>=185)より下では不発。
   炸裂時は下向き3破片(右下/下/左下)を速度3で撒き、その場に爆発演出。旧版 airburst の移植。 */
static void bh_aaburst(Entity *e) {
    e->x += e->vx;
    e->y += e->vy;
    e->y -= g_scroll_dy;         /* ★信管弾も敵弾=艦と一緒に縦スクロールへ流れる(論理Yを流す) */
    if (e->x < 0 || e->x > 255 || e->y < 16 || e->y > 220) { e->active = 0; return; }
    if (e->ftimer == 0) {                       /* 信管作動 */
        if (e->y < 185) {                       /* 自機帯より上でのみ炸裂(下から湧かない) */
            static const u8 shdir[3] = { 3, 4, 5 };   /* 右下/下/左下の扇 */
            u8 k;
            for (k = 0; k < 3; k++) {
                Entity *f;
                if (ent_enemy_bullet_full()) break;   /* ★弾幕上限リミッタ(破片も一元管理) */
                f = ent_spawn(ET_BULLET);
                if (f) {
                    f->team = TEAM_ENEMY;
                    f->x = e->x; f->y = (s16)(e->y + k * 3);   /* Yを少しずらし同一走査線回避 */
                    f->vx = (s16)(dirdx8[shdir[k]] * 3); f->vy = (s16)(dirdy8[shdir[k]] * 3);
                    f->color = 12; f->pat = SPR_BULLET;   /* 破片=橙(敵弾統一色) */
                    g_ebul++;   /* ★敵弾カウンタ(上限O(1)判定用) */
                }
            }
            ent_spawn_explosion(e->x, e->y);    /* 炸裂の見た目 */
            sfx(2, SFX_HIT);                     /* 炸裂音(小) */
        }
        e->active = 0;
        return;
    }
    e->ftimer--;
}

/* 砲塔: 艦上の世界座標(ax,ay)から画面座標へ。縦=ay-cam(艦と一緒にスクロール)、
   横=ax+weaveX(蛇行の横揺れに追従)。画面内に居る時だけ発砲(動く的)。
   ★可動砲身: 自機を狙って段階回転(vx=向き0-7, vy=旋回冷却)。命中フラッシュ(h=残フレーム, 白coltab)。 */
#define BARREL_OFF 5   /* 砲身スプライトを狙い方向へ突き出す量(ドームから砲身が出る) */
static void bh_turret(Entity *e) {
    s16 dx, dy; u8 d;
    if (e->hp == 0) { e->hidden = 1; return; }         /* 撃破済み: 砲身消失・不動(炎上はscene側BGで) */
    dx = e->ax + g_meander;                            /* ドーム位置(照準/発砲/当たりの基準) */
    dy = e->ay - (s16)g_cam;
    e->x = dx; e->y = dy;
    if (e->h) { e->h--; e->coltab = barrel_flash; }   /* 命中で白フラッシュ */
    else        e->coltab = barrel_col;               /* 通常=金属シェード */
    if (dy > -16 && dy < 212) {
        u8 tgt = dir8((s16)g_player_x - dx, (s16)g_player_y - dy);
        if (e->vy) e->vy--;                            /* 旋回冷却 */
        else { e->vx = (s16)step_dir((u8)e->vx, tgt); e->vy = 5; }   /* 5fごとに1段 */
        run_fire(e);                                   /* 発砲(ドーム位置から, 画面内のみ) */
    } else e->ftimer = 1;                              /* 画面外はチャージ据置 */
    d = (u8)e->vx & 7;
    e->pat = (u8)(SPR_BARREL0 + d * 4);                /* 向きに応じた砲身バー(上下=縦, 左右=横, 斜め) */
    e->x = dx + (s16)dirdx8[d] * BARREL_OFF;           /* ★砲身をドームから狙い方向へ突き出す=切れて見えない */
    e->y = dy + (s16)dirdy8[d] * BARREL_OFF;
}

/* 敵戦闘機(空戦): 下方向へ進み画面下で消滅。fireを持てば発砲も。
   ★所属国別の飛び方(archetype=e->ax, 位相=e->ay):
     0=独(急降下): 徐々に加速する直進降下(一撃離脱)。
     1=英(旋回機): 16fごとにvx反転=横蛇行しながら降下。
     2=米(直進/数): 一定速の直進(端で反転)。scene側で出現間隔を詰めて数で押す。 */
static void bh_fighter(Entity *e) {
    e->y += e->vy;
    e->x += e->vx;
    if ((u8)e->ax == 0) {                                 /* 独: 急降下(加速) */
        if ((++e->ay & 31) == 0 && e->vy < 6) e->vy++;
    } else if ((u8)e->ax == 1) {                          /* 英: 横蛇行 */
        if ((++e->ay & 15) == 0) e->vx = (s16)(-e->vx);
    }
    if (e->x < 0 || e->x > (s16)(SCR_W - e->w)) e->vx = (s16)(-e->vx);  /* 端で横反転 */
    e->pat = (u8)(SPR_PLANE_L + dir8(e->vx, e->vy) * 4);  /* ★8方向: 移動方向に機首を向ける(大サイズ) */
    if (e->fire) run_fire(e);
    if (e->y > SCR_H + 8) e->active = 0;                  /* 下へ抜けたら消滅 */
}

/* (dx,dy)に最も近い8方向index(0=上,時計回り)。旧版 dir8 移植。 */
static u8 dir8(s16 dx, s16 dy) {
    s16 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax > ay * 2) return dx > 0 ? 2 : 6;    /* 右/左 */
    if (ay > ax * 2) return dy > 0 ? 4 : 0;    /* 下/上 */
    if (dx > 0) return dy > 0 ? 3 : 1;         /* 右下/右上 */
    return dy > 0 ? 5 : 7;                      /* 左下/左上 */
}
/* cur から tgt へ8方向を短い方に1歩(旧版 step_dir)。 */
static u8 step_dir(u8 cur, u8 tgt) {
    u8 d = (u8)((tgt - cur) & 7);
    if (d == 0) return cur;
    return (u8)((d <= 4) ? (cur + 1) & 7 : (cur + 7) & 7);
}

/* 艦載機(F6F/F4U): ホバー(展開)→8方向で自機を大回り追尾。20fに1歩だけ向き直り speed2 で直進。
   旧版の空母/アイオワ機を移植。HP1(弾/体当りで消滅)。画面外(±18マージン)で消滅。 */
static void bh_pursuer(Entity *e) {
    if (e->ftimer) {                           /* ホバー/展開: 動かず常時自機を向く */
        e->ftimer--;
        e->ax = (s16)dir8((s16)g_player_x - e->x, (s16)g_player_y - e->y);
    } else {                                   /* 追尾: 20fに1歩向き直り(大回り), speed2直進 */
        if (e->ay) e->ay--;
        else { e->ax = (s16)step_dir((u8)e->ax, dir8((s16)g_player_x - e->x, (s16)g_player_y - e->y)); e->ay = 20; }
        e->x += (s16)dirdx8[e->ax & 7] * 2;
        e->y += (s16)dirdy8[e->ax & 7] * 2;
    }
    /* ★浮上アニメ: ホバー中 ftimer で 小→中、飛行(ftimer=0)で 大。8方向を機首へ反映。 */
    { u8 base = (e->ftimer > 22) ? SPR_PLANE_S : (e->ftimer ? SPR_PLANE_M : SPR_PLANE_L);
      e->pat = (u8)(base + ((u8)e->ax & 7) * 4); }
    if (e->x < -18 || e->x > 274 || e->y < -18 || e->y > 226) e->active = 0;
}

/* 潜水艦ミサイル(フッド固有): 舷側から発進(ph1)→浮上し弱誘導(ph2)→点火点滅→8方向炸裂(ph3)。
   ph>=2(浮上後)は体自体が危険。撃墜不可(ライフサイクル完遂)。旧版4相を3相にコンパクト移植。 */
static void bh_smissile(Entity *e) {
    u8 ph = (u8)e->ax;
    if (ph == 1) {                              /* 発進: 舷側から外へ、水中(暗青の小弾) */
        e->x += (s16)e->ay * 2;
        if (e->x < 6) e->x = 6; else if (e->x > 250) e->x = 250;
        e->pat = SPR_BULLET; e->color = 7;
        if (e->ftimer) e->ftimer--; else { e->ax = 2; e->ftimer = 40; }
    } else if (ph == 2) {                        /* 浮上→弱誘導(自機へ1px。舷側の外に留まる) */
        s16 shipC = 128 + g_meander, wantx = (s16)g_player_x;
        if (e->ay < 0) { if (wantx > shipC - 52) wantx = shipC - 52; }
        else           { if (wantx < shipC + 52) wantx = shipC + 52; }
        if (e->x < wantx) e->x++; else if (e->x > wantx) e->x--;
        if (e->y < (s16)g_player_y) e->y++; else if (e->y > (s16)g_player_y) e->y--;
        e->pat = SPR_EBSHELL; e->color = 11;    /* 浮上=赤い誘導弾頭(艦の白/AA橙と区別) */
        if (e->ftimer) e->ftimer--; else { e->ax = 3; e->ftimer = 16; }
    } else {                                     /* 点火(点滅)→8方向炸裂 */
        e->color = 12; e->hidden = (e->ftimer & 2) ? 1 : 0;
        if (e->ftimer) { e->ftimer--; return; }
        e->hidden = 0;
        { u8 k; for (k = 0; k < 8; k++) {
            Entity *b = ent_spawn(ET_BULLET);
            if (b) { b->team = TEAM_ENEMY; b->x = e->x; b->y = e->y;
                     b->vx = (s16)dirdx8[k] * 2; b->vy = (s16)dirdy8[k] * 2;
                     b->color = 12; b->pat = SPR_BULLET; g_ebul++; } } }   /* ★敵弾カウンタ */
        ent_spawn_explosion(e->x, e->y); sfx(2, SFX_HIT);
        e->active = 0;
    }
}

/* 合体弾の予告(双子艦): 左右2発(cx±off)が中心へ収束(off→0)。合体で自機狙いの高速大弾を発射。
   ay=中心x, y=中心y, x=半間隔off(収束), ftimer=残収束フレーム。実体は hidden=1(描画は ent_draw_all の合体パス)。 */
#define CB_GAP      52    /* 収束開始の半間隔(左右の艦=中心±52) */
#define CB_CONVERGE 24    /* 収束フレーム数 */
static void bh_combo(Entity *e) {
    e->y -= g_scroll_dy;                               /* ★合体弾(予告)も敵弾=艦と一緒に流れる */
    if (e->ftimer) {                                   /* 収束中: off を 0 へ */
        e->ftimer--;
        e->x = (s16)((u16)e->ftimer * CB_GAP / CB_CONVERGE);
        return;
    }
    { s16 cx = e->ay, cy = e->y;                        /* 合体→自機狙いの高速大弾 */
      u8 dir = aim_dir(cx, cy, (s16)g_player_x, (s16)g_player_y);
      Entity *b = emit(cx, cy, dir, 0, 9);             /* kind0=橙, spd9=速い */
      if (b) b->pat = SPR_EBSHELL;                     /* 太い合体弾 */
      ent_spawn_explosion(cx, cy); sfx(2, SFX_HIT); }
    e->active = 0;
}

/* 撃破エフェクト: 4コマの火球アニメ(核→炸裂→大輪→残火)を進めつつ 白→橙→赤→暗 と冷めて消滅。
   spawn 時 ftimer=16。elapsed=16-ftimer を >>2 でコマ(0..3)に。 */
static const u8 exp_pat[4]  = { SPR_EXP0, SPR_EXP1, SPR_EXP2, SPR_EXP3 };
static const u8 exp_fcol[4] = { 15, 12, 11, 7 };            /* 白→橙→赤→暗 */
static void bh_explosion(Entity *e) {
    u8 f;
    if (e->ftimer == 0) { e->active = 0; return; }
    e->ftimer--;
    f = (u8)((16 - e->ftimer) >> 2); if (f > 3) f = 3;
    e->pat = exp_pat[f]; e->color = exp_fcol[f];
}

/* 火花(被弾ヒット/マズルフラッシュ): SPR_FLASH を短時間 白→淡→橙 と明滅して消滅。 */
static const u8 spark_col[4] = { 15, 14, 12, 11 };
static void bh_spark(Entity *e) {
    if (e->ftimer == 0) { e->active = 0; return; }
    e->ftimer--;
    e->color = spark_col[(e->ftimer >> 1) & 3];
}

/* 空母甲板の停泊機: 艦上世界アンカー(ax,ay)で静止し、艦と一緒にスクロール(砲塔と同じ座標系, 発砲なし)。
   自機弾で破壊可(下の当たり判定)。発艦時は scene が type を ET_PURSUER へ差し替える。 */
static void bh_parked(Entity *e) {
    e->x = e->ax + g_meander;
    e->y = e->ay - (s16)g_cam;
}

extern void bh_player(Entity *e);   /* player.c(入力/発砲を持つのでゲーム側モジュールへ) */

typedef void (*Behavior)(Entity *);
static const Behavior behaviors[ET_COUNT] = {
    0,            /* ET_NONE      */
    bh_noop,      /* ET_BOUNCER(デモ枠=no-op) */
    bh_bullet,    /* ET_BULLET    */
    bh_noop,      /* ET_SHOOTER(デモ枠=no-op)  */
    bh_fighter,   /* ET_FIGHTER   */
    bh_player,    /* ET_PLAYER    */
    bh_turret,    /* ET_TURRET(蛇行追従＋発砲。破壊可能) */
    bh_explosion, /* ET_EXPLOSION */
    bh_aaburst,   /* ET_AABURST(時限信管弾→下向き3破片へ炸裂) */
    bh_pursuer,   /* ET_PURSUER(艦載機の8方向追尾) */
    bh_smissile,  /* ET_SMISSILE(潜水艦ミサイル4相) */
    bh_combo,     /* ET_COMBO(双子艦の合体弾予告) */
    bh_spark,     /* ET_SPARK(火花) */
    bh_parked,    /* ET_PARKED(空母甲板の停泊機。艦上静止) */
};


void hot_ent_update_all(void) {
    u8 i;
    for (i = 0; i < ENT_MAX; i++) {
        Entity *e = ent_pool() + i;
        if (e->active && behaviors[e->type]) behaviors[e->type](e);
    }
}

/* ===== 対空砲23基の発砲(RAM実行) =====
   ★挙動は scene_stage.c の旧 aa_update と完全同一。移設のみ(状態は aa_hot.h 経由で常駐を参照)。
     毎フレーム23基を走査するのでRAM実行の効き所。座標表はループ外で1回取得。
   ※窓化(可視区間のみ走査)も試したが実機で速度不変(残コストは走査でなく発砲emit側)だったため撤回。 */
static const u8 aafire_iv[STAGE_COUNT] = { 140, 132, 84, 64, 40 };   /* 面別発砲間隔(小=激しい) */
void hot_aa_update(void) {
    u8 tbl = ship_aagtbl[curstage], i, fired = 0, nv = 0;
    const u8 *aax; const u16 *aay;
    ship_aag_tables(tbl, &aax, &aay);   /* 座標表をループ外で1回取得=毎フレーム23回の関数呼び+switchを排除 */
    for (i = 0; i < SHIP_NAAG; i++) {
        s16 gx, sy, sx; u16 gy;
        if (aa_dead[i]) continue;                           /* 破壊済みは撃たない/当たらない */
        gx = (s16)aax[i]; gy = aay[i];                      /* 直接添字参照(ship_aag_pos関数呼びを回避) */
        sy = (s16)(SC_SHIP_R0 * 16 + (s16)gy) - (s16)cam;   /* 画面Y */
        if (sy < -8 || sy > 216) continue;                  /* 当たり範囲外=発砲も当たりも無い(艦が視界外) */
        sx = gx + g_meander;                                /* 画面X(蛇行に追従) */
        aa_vis_i[nv] = i; aa_vis_sx[nv] = sx; aa_vis_sy[nv] = sy; nv++;  /* 可視AAを記録(aa_collideが使う) */
        if (sy < 8 || sy > 200) continue;                   /* 発砲はより狭い帯でのみ(画面端の砲は撃たない) */
        if (aa_fire[i]) { aa_fire[i]--; continue; }
        if (fired >= 2) { aa_fire[i] = 1; continue; }       /* 同フレーム発砲上限=発砲波の平準化(発射レートは不変) */
        { /* 狙い±3ステップの散らし。rnd()&7 → -3..+4(≒±38°)。マスクのみ=除算を使わない。 */
          u8 dir = (u8)((aim_dir(sx, sy, (s16)g_player_x, (s16)g_player_y) + (rnd() & 7) + 29) & 31);
          if (i < 14) { emit_burst(sx, sy, dir, 2, 42); }    /* 大型=時限信管エアバースト(橙カプセル, fuze42) */
          else { emit(sx, sy, dir, 0, 2); }                  /* 小型=通常小弾(橙ペレット) */
        }
        { u16 iv = (u16)aafire_iv[curstage] + (u16)i * 6;    /* u16で計算し255クランプ(高i=最上部の砲ほど間隔長) */
          aa_fire[i] = diff_interval((iv > 255) ? 255 : (u8)iv); }
        fired++;
        sfx(1, SFX_EFIRE);
    }
    aa_nvis = nv;   /* このフレームの可視AA数(aa_collideが使う) */
}

/* 自機弾 × 対空砲(RAM実行)。★挙動は scene_stage.c の旧 aa_collide と完全同一。移設のみ。 */
void hot_aa_collide(void) {
    u8 v, k, nb = 0;
    Entity *bul[8];                     /* 画面内の自機弾を一度だけ収集(AA毎の全プール再走査=乗算を排す) */
    Entity *e = ent_pool();
    for (k = 0; k < ENT_MAX; k++, e++)
        if (e->active && e->type == ET_BULLET && e->team == TEAM_PLAYER && nb < 8) bul[nb++] = e;
    if (!nb || !aa_nvis) return;        /* 自機弾/可視AAが無ければ即終了 */
    {
    const u8 *aax; const u16 *aay;
    ship_aag_tables(ship_aagtbl[curstage], &aax, &aay);   /* 撃破時のburn_add用(gx/gyを命中時だけ再読込) */
    /* aa_updateが作った可視AAリスト(sx/sy計算済)だけを回す=23基走査→可視分へ短縮＋sx/sy再計算を排除。 */
    for (v = 0; v < aa_nvis; v++) {
        u8 i = aa_vis_i[v];
        s16 sx = aa_vis_sx[v], sy = aa_vis_sy[v];
        for (k = 0; k < nb; k++) {
            Entity *b = bul[k];
            if (!b->active) continue;
            { s16 dx = (s16)(b->x + 8) - sx, dy = (s16)(b->y + 8) - sy;
              if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
              if (dx < 10 && dy < 10) {
                  b->active = 0;
                  if (aa_hp[i]) aa_hp[i]--;
                  if (aa_hp[i] == 0) {
                      u16 pts = (i < 14) ? 30 : 20;
                      aa_dead[i] = 1; g_score += pts;
                      scorepop_add(sx, sy, pts);                     /* ★破壊点数ポップアップ(AA画面座標) */
                      burn_add((s16)aax[i], (u16)(SC_SHIP_R0 * 16 + aay[i]), (u8)((i < 14) ? 1 : 2));  /* 炎上サイト登録 */
                      ent_spawn_explosion(sx, sy); sfx(2, SFX_BOOM); g_shake = 6;
                  } else { ent_spawn_spark(b->x, b->y); sfx(2, SFX_HIT); }
                  break;
              }
            }
        }
    }
    }
}
