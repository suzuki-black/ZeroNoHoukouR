/* entity.c — 汎用エンティティ・プールの常駐実装。
   behavior は type 別の関数ポインタ表(generalization の要)。描画は今は LMMV 矩形で代用し、
   本番でスプライト/run_ops に差し替える(APIは据え置き)。 */
#include "entity.h"
#include "vdp.h"
#include "fire.h"
#include "sprites.h"
#include "scroll.h"     /* g_cam(砲塔の世界→画面Y変換) */
#include "gamestate.h"  /* g_score(撃破で加算) */
#include "sound.h"      /* sfx(被弾音 SFX_PHIT) */
#include "player.h"     /* g_player_x/y(艦載機の自機追尾) */

u8 g_spr_base;          /* エンティティ描画の開始スプライトスロット(先頭はHUDが確保) */

static Entity pool[ENT_MAX];

/* 各スプライトスロットに最後に書いた単色(0xFF=coltab/未確定=強制書換)。色表の重複16B書込みを省く。 */
static u8 slot_col[32];
/* ★A1: 各スロットに最後に書いた行別色表(coltab)のポインタ(0=単色書込み後で無効)。
   同一slotに同一coltabが既に載っていれば16B書込みを省く。単色書込み時は必ず 0 にして
   「そのslotのVRAMはもう coltab でない」を記録する(=以後の同ポインタ判定が VRAM 実体と一致)。 */
static const u8 *slot_ctab[32];

/* ★毎フレームのAI/移動(behavior群 + ent_update_all)は banked/hot.c(RAM実行)へ移設済み。
   R800のROMフェッチ律速を外すため。ここは spawn / 当たり判定 / カウンタ / 描画 等の常駐機能のみ。
   g_meander(蛇行の横揺れ量)は多数モジュールが参照する常駐グローバルなので、定義はここに残す。 */
s16 g_meander;

u8 ent_count(u8 type) {
    u8 i, n = 0; Entity *e = pool;   /* ポインタ加算走査=pool[i]の乗算を排除 */
    for (i = 0; i < ENT_MAX; i++, e++) if (e->active && e->type == type) n++;
    return n;
}

Entity *ent_at(u8 i) { return &pool[i]; }
Entity *ent_pool(void) { return pool; }   /* 先頭ポインタ(ポインタ加算走査で乗算を避ける用) */

/* ★生存(hp>0)砲台数のO(1)カウンタ。従来は毎フレーム2回(rage/クリア判定)プールをO(30)走査していた。
   spawn_turretで++、当たり判定で撃破(hp→0)時に--。面リスタートは stage_build が0初期化して再spawn。 */
u8 g_lturret;
u8 ent_live_turrets(void) { return g_lturret; }

/* ★敵弾(TEAM_ENEMY通常弾＋信管弾)の同時数のO(1)カウンタ。従来は ent_enemy_bullet_full が発砲弾ごとに
   O(30)全走査していた(4主砲×5way＋AA＋破片で1フレーム数百〜千イテレーション=砲台/敵増で線形悪化)。
   毎フレーム頭で ent_recount_ebul() が1回だけ数え直し(seed)、フレーム内の敵弾spawnで g_ebul++。
   死亡時の減算は次フレームの数え直しが吸収するので不要(=減算漏れバグが原理的に起きない安全設計)。 */
u8 g_ebul;
void ent_recount_ebul(void) {
    u8 i, n = 0; Entity *e = pool;
    for (i = 0; i < ENT_MAX; i++, e++)
        if (e->active && ((e->type == ET_BULLET && e->team == TEAM_ENEMY) || e->type == ET_AABURST)) n++;
    g_ebul = n;
}
u8 ent_enemy_bullet_full(void) { return (u8)(g_ebul >= ENEMY_BULLET_CAP); }

void ent_spawn_explosion(s16 x, s16 y) {
    Entity *e = ent_spawn(ET_EXPLOSION);
    if (e) { e->x = x; e->y = y; e->pat = SPR_EXP0; e->color = 15; e->ftimer = 16; }
}
void ent_spawn_spark(s16 x, s16 y) {
    Entity *e = ent_spawn(ET_SPARK);
    if (e) { e->x = x; e->y = y; e->pat = SPR_FLASH; e->color = 15; e->ftimer = 6; }
}

/* ---- 当たり判定 ---- */
u8 g_kills;
u8 g_gun_kills;
u8 g_playerhit;
u8 g_pinv;
u8 g_miss;
u8 g_hitstop;
u8 g_shake;

/* ===== 破壊点数ポップアップ: 撃破位置の真上に加算点を数字スプライトで数フレーム表示 ===== */
#define SPOP_MAX    5
#define SPOP_FRAMES 45           /* 30fps固定で約1.5秒 */
static s16 spop_x[SPOP_MAX], spop_y[SPOP_MAX];   /* 撃破オブジェクトの画面座標(左上) */
static u16 spop_val[SPOP_MAX];                    /* 加算点(10/20/30/60 等) */
static u8  spop_t[SPOP_MAX];                      /* 残表示フレーム(0=空き) */
/* 撃破サイトから呼ぶ: 画面座標(x,y)＋点数を登録。空きが無ければ最も残り少ないものを上書き。 */
void scorepop_add(s16 sx, s16 sy, u16 val) {
    u8 i, slot = 0, tmin = 0xFF;
    for (i = 0; i < SPOP_MAX; i++) {
        if (!spop_t[i]) { slot = i; break; }              /* 空き優先 */
        if (spop_t[i] < tmin) { tmin = spop_t[i]; slot = i; }
    }
    spop_x[slot] = sx; spop_y[slot] = sy; spop_val[slot] = val; spop_t[slot] = SPOP_FRAMES;
}
static void scorepop_reset(void) { u8 i; for (i = 0; i < SPOP_MAX; i++) spop_t[i] = 0; }

/* ent_resolve_collisions — 当たり判定(最重ホットパスの一つ)。
   ★かつてRAM実行化(hot_ram)を試したが実機turboRで速度変化ゼロ(戦闘中の衝突は自機弾数発×敵数体＝
     X早期棄却込みで数十イテレーションと元々軽く、フレーム時間の支配要因ではない)と実証されたため、
     ROM実行へ戻した。貴重な page3 RAM枠(0xE000天井)は、効果のあった AA走査(hot.c)へ充てる。 */
void ent_resolve_collisions(void) {
    /* ★プール全走査を1回に統合。1走査で以下へ分類し、以後はコンパクト集合だけを回す(判定・スコア・効果同一):
         pb[]=自機弾 / tgt[]=戦闘機/追尾/停泊/砲台 / th[]=自機に当たる脅威 / player=自機 */
    u8 i, j, npb = 0, nt = 0, nth = 0;
    Entity *b, *t, *e;
    Entity *pb[ENT_MAX], *tgt[ENT_MAX], *th[ENT_MAX], *player = (Entity *)0;
    e = pool;
    for (i = 0; i < ENT_MAX; i++, e++) {
        u8 ty;
        if (!e->active) continue;
        ty = e->type;
        if (ty == ET_BULLET) {
            if (e->team == TEAM_PLAYER) pb[npb++] = e;   /* 自機弾 */
            else                        th[nth++] = e;   /* 敵弾=脅威 */
        } else if (ty == ET_PLAYER) {
            player = e;
        } else if (ty == ET_FIGHTER || ty == ET_PURSUER) {
            tgt[nt++] = e; th[nth++] = e;                /* 戦闘機/追尾=ターゲットかつ脅威(体当り) */
        } else if (ty == ET_PARKED || ty == ET_TURRET) {
            tgt[nt++] = e;                               /* 停泊機/砲台=ターゲットのみ(体当り無し) */
        } else if (ty == ET_AABURST) {
            th[nth++] = e;                               /* 信管弾=脅威 */
        } else if (ty == ET_SMISSILE && e->ax >= 2) {
            th[nth++] = e;                               /* ミサイルは浮上後(相2以上)のみ危険 */
        }
    }
    /* 自機弾 × 敵戦闘機/砲台。overlap()インライン化＋弾座標ホイスト＋X早期棄却＋type1回読み。 */
    for (i = 0; i < npb; i++) {
        s16 bx, by;
        b = pb[i];
        if (!b->active) continue;
        bx = b->x; by = b->y;
        for (j = 0; j < nt; j++) {
            s16 dx, dy; u8 tt;
            t = tgt[j];
            if (!t->active) continue;   /* 先に別の弾で消えた可能性(集合はstale許容) */
            dx = t->x - bx; if (dx < 0) dx = -dx; if (dx >= 14) continue;   /* X早期棄却 */
            dy = t->y - by; if (dy < 0) dy = -dy; if (dy >= 14) continue;   /* Y(=overlap成立) */
            tt = t->type;
            if (tt == ET_FIGHTER || tt == ET_PURSUER || tt == ET_PARKED) {
                b->active = 0; t->active = 0; g_kills++;
                { u16 pts = (tt == ET_PURSUER) ? 20 : 10; g_score += pts;   /* 追尾機20 / 戦闘機・停泊機10 */
                  scorepop_add(t->x, t->y, pts); }                          /* ★破壊点数ポップアップ */
                ent_spawn_explosion(t->x, t->y);   /* 停泊機も自機弾で破壊(体当り判定は持たない) */
                break;
            }
            if (tt == ET_TURRET && t->hp) {
                b->active = 0;
                if (--t->hp == 0) { t->hidden = 1; g_gun_kills++; g_lturret--; g_score += 60; ent_spawn_explosion(t->x, t->y);
                                    scorepop_add(t->x, t->y, 60);  /* ★破壊点数ポップアップ */
                                    sfx(2, SFX_BOOM);              /* ★主砲撃破の爆発音 */
                                    g_hitstop = 4; g_shake = 8; }   /* 撃破=手応え(凍結＋揺れ)。activeは維持し炎上。★g_lturret--=生存砲台O(1) */
                else { t->h = 6; ent_spawn_spark(b->x, b->y); }   /* 非撃破=砲身が白フラッシュ(h)＋火花 */
                break;
            }
        }
    }
    /* 敵弾/敵戦闘機/… × 自機 → 敵を消し被弾+1 */
    if (player) {
        Entity *p = player;
        for (j = 0; j < nth; j++) {
            u8 tol;
            s16 dx, dy;
            e = th[j];
            if (!e->active) continue;
            /* ★自機の当たりは旧版準拠でタイト: 弾/破片=±6、体当り系=±9。 */
            tol = (e->type == ET_BULLET || e->type == ET_AABURST) ? 6 : 9;
            dx = e->x - p->x; dy = e->y - p->y;
            if (dx < 0) dx = -dx; if (dy < 0) dy = -dy;
            if (dx < tol && dy < tol) {
                e->active = 0;                 /* 敵/敵弾は消す(すり抜け防止) */
                if (g_pinv == 0 && !g_invinc) {/* 被弾直後の無敵中/設定無敵 は無傷 */
                    g_playerhit++;
                    sfx(0, SFX_PHIT);          /* 被弾の痛み音(tone A) */
                    ent_spawn_explosion(p->x, p->y);
                    g_hitstop = 5; g_shake = 12;   /* 被弾=強い手応え(凍結＋大きめ揺れ) */
                    if (g_php > 1) { g_php--; g_pinv = 90; }  /* 耐久残=生存(1.5秒無敵点滅) */
                    else { g_php = 0; g_miss = 1; }           /* 耐久尽き=撃墜。残機/リスタートはシーンが処理 */
                }
            }
        }
    }
}

void ent_reset(void) {
    u8 i;
    for (i = 0; i < ENT_MAX; i++) pool[i].active = 0;
    for (i = 0; i < 32; i++) { slot_col[i] = 0xFF; slot_ctab[i] = 0; }   /* 色キャッシュ無効化(面開始/再開で色表を必ず書直す) */
    g_ebul = 0;
    scorepop_reset();   /* ★破壊点数ポップアップも面開始/再開でクリア */
}

Entity *ent_spawn(u8 type) {
    u8 i; Entity *e = pool;   /* ★ポインタ加算走査=pool[i]の乗算を排除。発砲中は弾/爆発/火花で多発するので効く */
    for (i = 0; i < ENT_MAX; i++, e++) {
        if (!e->active) {
            e->active = 1; e->type = type;
            e->x = 0; e->y = 0; e->vx = 0; e->vy = 0; e->ax = 0; e->ay = 0;
            e->w = 16; e->h = 16; e->color = 15; e->pat = 0;
            e->hidden = 0; e->hp = 1; e->team = TEAM_ENEMY;
            e->fire = (const u8 *)0; e->ftimer = 0;
            e->coltab = (const u8 *)0;
            e->shadow = 0;
            return e;
        }
    }
    return (Entity *)0;
}

/* スプライト描画: active な実体を先頭スロットから詰めて属性/色を書き、
   残りは停止マーカで隠す。ハードウェア合成なので消去は不要。
   ※画面外(y が縦範囲外)は描画しない: スプライトYは u8 なので世界アンカーの砲塔などが
     画面上方(負のy)にある間に (u8)y へ折り返して海上に幽霊表示されるのを防ぐ。
   ★V9938 mode2 は1走査線8枚まで(9枚目以降は欠落)。弾幕対策:
     - HUD(slot 0..g_spr_base-1) と 自機 は固定の最優先スロットで絶対に欠けさせない。
     - 残り(弾/敵/砲塔/エフェクト)は毎フレーム割当開始を回転(rot)させ、9枚以上の走査線での
       欠落を「常に同じ弾が消える」でなく「フレーム毎に入れ替わるちらつき」に分散する。
     - 総数は 32 枚で頭打ち(それ以上は描かない)。 */
/* ★色表(mode2=16B/枚)を毎フレーム全枚書くと弾数比例で重い(もたつきの一因)。前フレームと同じ単色なら
   VRAMに既にその色=16B書込みを省く(弾は殆ど橙12で殆ど省ける)。coltabは毎回書きキャッシュ無効(0xFF)。
   隠し(Y=216)は属性のみ変更で色表は残るためキャッシュ有効。ent_reset で 0xFF 初期化。 */
static void spr_col1(u8 slot, u8 color) {
    if (slot_col[slot] != color) { vdp_sprite_color(slot, color); slot_col[slot] = color; slot_ctab[slot] = 0; }
}
/* ★draw1: 描画の最ホットパス(毎フレーム最大32回)。SDCCのCコードは Entity* をレジスタに保持できず
   フィールドアクセス毎に pop/push でスタック往復＋IXフレーム＋4引数の vdp_sat_pos 呼びでスタック渡し、と
   膨大な殻を生んでいた(生成ASMで確認)。ここを __naked ASM で e を IY に固定=全フィールドをオフセット直読み、
   SAT影は sat_shadow へ直書き(vdp_sat_pos呼びを排除)に置換。色判定/効果は従来と完全同一(検証: SATダンプ一致)。
   規約: A=slot, DE=e, 戻り A=slot+1(SDCC観測)。色関数 spr_col1(A=slot,L=color)/vdp_sprite_color_tab(A=slot,DE=coltab)。
   Entity offset: x=2, y=4, color=16, pat=17, coltab=24(2B)。sat_shadow/g_vscroll は vdp.c の非staticグローバル。 */
static u8 d1slot;              /* ASM draw1のslot退避(呼び出しを跨いで保持) */
static const u8 *d1ctab;      /* ASM draw1のcoltab退避(vdp_sprite_color_tab呼びでDE破壊のため) */
static u8 draw1(u8 slot, const Entity *e) __naked {
    (void)slot; (void)e;
    __asm
        ld   (_d1slot), a          ; slot保存
        push iy
        push de
        pop  iy                    ; IY = e
        ; --- SAT影書込(IYがe確実。呼び出し前に完了) ---
        ld   a, (_d1slot)
        ld   l, a
        ld   h, #0
        add  hl, hl
        add  hl, hl                ; slot*4
        ld   de, #_sat_shadow
        add  hl, de                ; HL = &sat_shadow[slot*4]
        ld   a, 4 (iy)             ; e->y(低位)
        ld   c, a
        ld   a, (_g_vscroll)
        add  a, c
        dec  a
        ld   (hl), a               ; p[0]=y+vscroll-1
        inc  hl
        ld   a, 2 (iy)             ; e->x(低位)
        ld   (hl), a               ; p[1]=x
        inc  hl
        ld   a, 17 (iy)            ; e->pat
        ld   (hl), a               ; p[2]=pat
        ; --- 色 ---
        ld   e, 24 (iy)            ; coltab低
        ld   d, 25 (iy)            ; coltab高
        ld   a, d
        or   e
        jr   z, 00011$             ; coltab==0 → 単色
        ; coltab有: slot_ctab[slot]==coltab? (DE=coltab)
        ld   a, (_d1slot)
        ld   l, a
        ld   h, #0
        add  hl, hl                ; slot*2
        ld   bc, #_slot_ctab
        add  hl, bc                ; &slot_ctab[slot]
        ld   a, (hl)
        inc  hl
        ld   h, (hl)
        ld   l, a                  ; HL=既存coltab
        or   a
        sbc  hl, de                ; ==coltab?
        jr   z, 00019$             ; 同一=16B書込省略
        ld   (_d1ctab), de         ; coltab退避
        ld   a, (_d1slot)
        call _vdp_sprite_color_tab ; A=slot, DE=coltab
        ld   a, (_d1slot)          ; slot_ctab[slot]=coltab
        ld   l, a
        ld   h, #0
        add  hl, hl
        ld   bc, #_slot_ctab
        add  hl, bc
        ld   de, (_d1ctab)
        ld   (hl), e
        inc  hl
        ld   (hl), d
        ld   a, (_d1slot)          ; slot_col[slot]=0xFF
        ld   l, a
        ld   h, #0
        ld   bc, #_slot_col
        add  hl, bc
        ld   (hl), #0xFF
        jr   00019$
    00011$:
        ld   l, 16 (iy)            ; e->color
        ld   a, (_d1slot)
        call _spr_col1             ; A=slot, L=color
    00019$:
        pop  iy
        ld   a, (_d1slot)
        inc  a                     ; 戻り=slot+1
        ret
    __endasm;
}

/* 落ち影: 自身のパターンを暗色13で右下へオフセット描画(海面に落ちた影)。 */
#define SHADOW_DX 5
#define SHADOW_DY 6
static u8 draw_shadow(u8 slot, const Entity *e) {
    spr_col1(slot, 13);   /* ほぼ黒(1,1,1)。単色=差分書換 */
    vdp_sat_pos(slot, (u8)(e->x + SHADOW_DX), (u8)(e->y + SHADOW_DY), e->pat);   /* ★A6: シャドウへ */
    return (u8)(slot + 1);
}
/* ★プール全走査を1回に統合(従来は自機探索/可視収集/合体弾/影 で4回走査していた=各エンティティの
   active/type/x/y を4回読み直していた)。1走査で自機・通常描画対象(vis)・合体弾(co)・影(sh)へ分類し、
   以後は各リストだけを回す。描画順(自機→回転割当vis→合体弾→影)とスロット優先/影の包含は従来と同一。 */
void ent_draw_all(void) {
    static u8 rot;
    u8 i, j, n = 0, nco = 0, nsh = 0, slot = g_spr_base;
    Entity *vis[ENT_MAX], *co[ENT_MAX], *sh[ENT_MAX];
    Entity *player = (Entity *)0;
    Entity *e = pool;
    for (i = 0; i < ENT_MAX; i++, e++) {
        u8 ty;
        if (!e->active) continue;
        ty = e->type;
        if (ty == ET_COMBO) {                                   /* 合体弾(hidden)は専用パス。y域のみ判定 */
            if (e->y > -16 && e->y < 212) co[nco++] = e;
            continue;                                           /* hidden=vis/影の対象外 */
        }
        if (e->hidden || e->y <= -16 || e->y >= 212 || e->x < 0 || e->x >= 256) continue;  /* 画面外/不可視 */
        if (ty == ET_PLAYER) player = e;                        /* 自機=最優先スロット(絶対に欠けさせない) */
        else                 vis[n++] = e;                      /* 通常描画対象 */
        if (e->shadow) sh[nsh++] = e;                           /* 影(自機/敵機。可視かつshadow) */
    }
    /* 自機を固定最優先スロット(g_spr_base)へ */
    if (player) slot = draw1(slot, player);
    /* 収集集合内で開始位置を毎フレーム回転させて割当(9枚/走査線超の欠落をちらつきへ均等分散)。
       ★D2(§D2 逆順SAT): 交互フレームで割当て順を"正順/逆順"に反転する。SATは低slot=高優先(1走査線8枚まで)
         なので、順を反転すると混雑ラインで"表示される8枚"が前半⇔後半で交互に入れ替わる=消える弾が
         30Hzで入れ替わり実質16枚/ラインに見える(自機は上で最優先slot固定=ちらつかない)。 */
    if (n) {
        u8 start = (u8)(rot % n);
        u8 rev = (u8)(rot & 1);   /* 交互フレームで反転 */
        for (j = 0; j < n && slot < 32; j++) {
            u8 d = rev ? (u8)(n - 1 - j) : j;   /* 逆順フレームは末尾から割当て=優先が反転 */
            i = (u8)(start + d); if (i >= n) i -= n;
            slot = draw1(slot, vis[i]);
        }
    }
    /* 合体弾パス(双子艦): 中心±off の2発として描く。 */
    for (i = 0; i < nco && slot < 31; i++) {
        s16 cx = co[i]->ay, cy = co[i]->y, off = co[i]->x, lx = cx - off, rx = cx + off;
        if (lx >= 0 && lx < 256) { spr_col1(slot, 12); vdp_sat_pos(slot, (u8)lx, (u8)cy, SPR_EBSHELL); slot++; }
        if (slot < 32 && rx >= 0 && rx < 256) { spr_col1(slot, 12); vdp_sat_pos(slot, (u8)rx, (u8)cy, SPR_EBSHELL); slot++; }
    }
    /* 落ち影パス: 影は最後=最も高いslot=最低優先(混雑ラインではゲーム弾/敵機に譲って先に落ちる)。 */
    for (i = 0; i < nsh && slot < 32; i++) slot = draw_shadow(slot, sh[i]);
    /* ★破壊点数ポップアップ: 撃破位置の"真上"(中心の16px上)に加算点を数字スプライトで表示。
       エンティティの後=高slot=低優先なので混雑走査線ではゲームスプライトへ譲る。ここで残フレームも減らす。
       ★画面内へクランプ: 真上が上端外だとスプライトYがu8回り込みで画面下へ飛び"消える"ため、
         上端(=艦首側の一個目の砲台等)/下端/左右で画面内に留めて必ず見えるようにする。 */
    for (i = 0; i < SPOP_MAX && slot < 32; i++) {
        if (!spop_t[i]) continue;
        { u16 v = spop_val[i]; u8 dbuf[4], nd = 0;
          if (v == 0) dbuf[nd++] = 0; else while (v && nd < 4) { dbuf[nd++] = (u8)(v % 10); v /= 10; }
          { s16 x0 = (s16)(spop_x[i] + 8 - (s16)nd * 4);   /* 中心(x+8)へnd桁(各8px)を中央寄せ */
            s16 y0 = (s16)(spop_y[i] - 16);                /* 真上=1タイル上 */
            s16 xmax = (s16)(256 - (s16)nd * 8);           /* nd桁が右端で切れない上限 */
            u8 k;
            if (y0 < 2)   y0 = 2;                          /* 上端クランプ(見切れて消えるのを防ぐ) */
            if (y0 > 204) y0 = 204;                        /* 下端クランプ */
            if (x0 < 0)   x0 = 0;
            if (x0 > xmax) x0 = xmax;
            for (k = 0; k < nd && slot < 32; k++) {
                spr_col1(slot, 15);                        /* 白(HUDスコアと同色) */
                vdp_sat_pos(slot, (u8)(x0 + (s16)k * 8), (u8)y0, (u8)(SPR_DIGIT0 + dbuf[nd - 1 - k] * 4));
                slot++;
            } }
        }
        spop_t[i]--;   /* 表示時間を1減らす(0で次フレームから消える) */
    }
    /* ★A6: 溜めた属性(g_spr_base..slot-1)を1回のバーストでSATへ(flush内で停止マーカも直書き)。 */
    vdp_sat_flush(g_spr_base, slot);
    rot++;
}
