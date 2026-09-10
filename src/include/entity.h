/* entity.h — 汎用エンティティ・プール(常駐/ホット)。
   自機/敵機/弾/砲/エフェクトを1つの固定長プール＋type別 behavior で回す骨格。
   空戦の敵機も戦艦の砲も同じ枠で扱う(HANDOFF §4-3)。将来 run_ops/run_fire を behavior に接続。 */
#ifndef ENTITY_H
#define ENTITY_H

#include "types.h"

#define ENT_MAX 30   /* turboR前提。空母の停泊機(最大8)＋主砲4＋弾/エフェクトを同時に収める */

/* 種別 = behavior テーブルの添字。追加時は entity.c の behaviors[] と対で更新。 */
enum {
    ET_NONE = 0,
    ET_BOUNCER,   /* 骨格デモ用: 画面端で反射 */
    ET_BULLET,    /* 弾: 直進し画面外で消滅(team で自機/敵を区別) */
    ET_SHOOTER,   /* 射手: fire スクリプトで弾を撒く(位置固定) */
    ET_FIGHTER,   /* 空戦の敵戦闘機: 上から侵入し下へ抜ける */
    ET_PLAYER,    /* 自機: 入力で移動＋発砲 */
    ET_TURRET,    /* 戦艦の砲台: 破壊可能(hp)＋fireで発砲。全撃破でクリア */
    ET_EXPLOSION, /* 撃破エフェクト: 数フレーム色を変えて消滅 */
    ET_AABURST,   /* 対空砲の時限信管弾: 飛翔し ftimer(信管)で下向き3破片へ炸裂(下端では不発) */
    ET_PURSUER,   /* 艦載機(空母F6F/アイオワF4U): ホバー→8方向で自機を大回り追尾(旧版移植)。ax=向き/ay=旋回冷却/ftimer=展開 */
    ET_SMISSILE,  /* 潜水艦ミサイル(フッド): 舷側発進→浮上→弱誘導→8方向炸裂。ax=相(1..3)/ay=舷(±1)/ftimer=相タイマ。撃墜不可 */
    ET_COMBO,     /* 合体弾の予告(双子艦): 左右2発が中心へ収束→合体して自機狙いの大弾を発射。ay=中心x/y=中心y/x=半間隔off/ftimer=収束 */
    ET_SPARK,     /* 火花(被弾ヒット/マズルフラッシュ): SPR_FLASHを数フレーム明滅して消滅 */
    ET_PARKED,    /* 空母甲板の停泊F6F: 艦上世界アンカー(ax,ay)で静止。自機弾で破壊可(体当り無し)。発艦でET_PURSUERへ */
    ET_COUNT
};

/* 弾/実体の陣営(当たり判定用) */
#define TEAM_ENEMY  0
#define TEAM_PLAYER 1

typedef struct Entity {
    u8  active;
    u8  type;
    s16 x, y;           /* 位置(px, 左上。画面座標) */
    s16 vx, vy;         /* 速度(px/frame) */
    s16 ax, ay;         /* 世界アンカー(ET_TURRET: 艦上の世界座標。画面へは x=ax+weave, y=ay-cam) */
    u8  w, h;           /* 当たり/反射サイズ(スプライトは16x16固定) */
    u8  color;          /* スプライト色(0-15) */
    u8  pat;            /* スプライトパターン番号(4の倍数) */
    u8  hidden;         /* 1=スプライト描画しない(不可視の発砲点等) */
    u8  hp;             /* 耐久(ET_TURRET等)。0で撃破。既定1 */
    u8  team;           /* TEAM_ENEMY / TEAM_PLAYER(弾の帰属) */
    const u8 *fire;     /* fire スクリプト(ET_SHOOTER/一部FIGHTER)。無ければ NULL */
    u8  ftimer;         /* 次発火/クールダウンの残りフレーム */
    const u8 *coltab;   /* 行別スプライト色表16B(mode2の陰影)。NULLなら単色 color */
    u8  shadow;         /* 1=海面へ落ち影を投げる(自機/敵機。低優先の別スプライト) */
} Entity;

/* 当たり判定を解決(自機弾×敵戦闘機、敵弾/戦闘機×自機)。撃破/被弾数を計上。 */
void ent_resolve_collisions(void);
extern s16 g_meander;    /* 蛇行の横揺れ量(weaveX, ±)。ET_TURRET が x を追従補正 */
extern u8 g_kills;       /* 撃破した敵戦闘機の累計 */
extern u8 g_gun_kills;   /* 撃破した砲台の累計(撃破演出/クリア判定用) */
extern u8 g_playerhit;   /* 自機が被弾した累計(プロト) */
extern u8 g_pinv;        /* 自機の無敵フレーム残(被弾直後のみ>0。0で被弾可) */
extern u8 g_miss;        /* 1=自機撃墜(耐久尽き)。シーンが残機減算＋面リスタート/ゲームオーバーを処理 */
extern u8 g_hitstop;     /* >0: シーンが数フレーム更新を凍結=撃破の"手応え"(ヒットストップ) */
extern u8 g_shake;       /* >0: 画面を数フレーム縦に揺らす(砲台撃破/被弾の迫力) */

extern u8 g_spr_base;               /* エンティティ描画の開始スプライトslot(HUDが先頭を確保) */
void    ent_reset(void);            /* プール全消去 */
Entity *ent_spawn(u8 type);         /* 空きを1つ確保(既定値で初期化)。無ければ NULL */
void    ent_update_all(void);       /* 全 active の behavior update を回す */
void    ent_draw_all(void);         /* active をスプライトへ(hidden除く) */
u8      ent_count(u8 type);         /* active な type の数(撃破判定用) */
Entity *ent_at(u8 i);               /* プールの i 番目(0..ENT_MAX-1)。active は呼び側で確認 */
Entity *ent_pool(void);             /* プール先頭ポインタ(ポインタ加算で走査=添字乗算を避ける) */
void    scorepop_add(s16 sx, s16 sy, u16 val);  /* ★破壊点数ポップアップ登録(画面座標＋加算点)。撃破サイトから呼ぶ */
u8      ent_live_turrets(void);     /* 生存(hp>0)砲台の数(O(1)=g_lturretを返す)。撃破済みは active のまま炎上させるため別カウント */
extern u8 g_lturret;                /* ★生存砲台のO(1)カウンタ。spawn_turretで++、当たり判定の撃破で--、stage_buildで0初期化 */

/* ★画面弾幕リミッタ(全砲台・全敵で一元管理)。敵弾(通常弾＋信管弾＋炸裂破片)の同時数が上限に
   達していれば 1 を返す=以降の敵弾spawnを一律に取り締まる。★30fps固定で計算に余裕が出たため
   負荷軽減で下げていた15を元の設計値20へ復帰(弾幕の濃さを原状回復)。 */
#define ENEMY_BULLET_CAP 20
u8      ent_enemy_bullet_full(void);
extern u8 g_ebul;                   /* ★敵弾同時数のO(1)カウンタ(emit系で++、毎フレーム再計数) */
void      ent_recount_ebul(void);   /* ★毎フレーム頭で敵弾数を1回だけ数え直す(seed) */
void    ent_spawn_explosion(s16 x, s16 y);  /* 撃破エフェクト(火球アニメ)を1つ */
void    ent_spawn_spark(s16 x, s16 y);      /* 小さな火花(被弾ヒット/発砲)を1つ */

#endif /* ENTITY_H */
