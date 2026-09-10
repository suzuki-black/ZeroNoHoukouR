/* scene_stage.c — ★1本の連続縦スクロール面(海→戦艦 地続き。画面カット無し)。
   フェーズ0: 海=蛇行なしの直進スクロール(前進)。戦艦の船尾が見えたら交戦へ。
   フェーズ1: 戦艦=船首↔船尾の往復蛇行スクロール(縦の往復＋横揺れ weaveX)＝前作の戦艦戦。
   自機は下部固定(scroll側のY補正＋weaveXは自機に非適用)。砲塔/戦闘機/撃破は次段で統合。 */
#include "scene.h"
#include "vdp.h"
#include "entity.h"
#include "sprites.h"
#include "scroll.h"
#include "ship.h"          /* 旧版忠実の艦レンダラ */
#include "fire.h"
#include "sound.h"
#include "hud.h"
#include "gamestate.h"
#include "input.h"
#include "player.h"        /* g_player_x/y(対空砲の自機狙い) */
#include "bank.h"          /* data_read(艦体OPSをバンク→RAM) */
#include "assets_data.h"   /* 自動生成: ship_ops_off/len, ship_hull/bowcnt/bowyb, SHIP_OPS_RAM_MAX, ASSET_BANK */
#include "hotcode.h"       /* ent_resolve_collisions/aa_update/aa_collide のRAM実行ラッパ */
#include "aa_hot.h"        /* AA状態(cam/curstage/aa_*)を hot.c と共有(公開=非static化) */
#include "ramexec.h"       /* ★§4-3: page1_use_ram/use_cart(ホット区間だけpage1をRAM実行化) */
#ifdef DEBUG_PROF
#include "prof.h"
#define PROF_CALL(grp, call) do { PROF_T0(_pt); call; PROF_ADD(grp, _pt); } while (0)
#else
#define PROF_CALL(grp, call) do { call; } while (0)
#endif
/* 撃破!! パネル(1bpp)は常駐節約のためデータバンク(bank8)へ。PANEL_* / panel_off は assets_data.h。 */

/* ★艦(上面視, 496px)は旧版 BattleshipProto の艦システムを忠実移植(ship.c)。海テンプレを下地に
   paint_hull＋波切り艦首＋主砲/艦橋/煙突OPS＋対空砲23基を バッファB(SC_SHIPBUF_Y=528)へ事前描画。
   前部Anton/Bruno(超越)＋後部Cäsar/Dora の4主砲は破壊可能スプライト(ET_TURRET)。全撃破でクリア。 */

/* 主砲の発砲(★面別難易度)。{interval, suppress, op, a(弾数), kind, spd, END}。
   後半ほど 間隔↓(速い)・弾数↑(3→5-way)・弾速↑・suppress↓(安全半径が狭い=肉薄が難しい)。
   ★suppress: 半径内(≒ゼロ距離)に自機が居ると発射スキップ=肉薄で撃たせない教育メカ。1面ほど広い。
   面順=BB/Carrier/Hood/Twins/Iowa。 */
static const u8 fd_gun_bb[]   = { 56, 26, FIRE_AIMFAN, 3, 2, 3, FIRE_END };  /* 1面: 遅い3-way(教育) */
static const u8 fd_gun_cv[]   = { 50, 24, FIRE_AIMFAN, 4, 2, 4, FIRE_END };
static const u8 fd_gun_hd[]   = { 44, 22, FIRE_AIMFAN, 4, 2, 4, FIRE_END };
static const u8 fd_gun_tw[]   = { 38, 20, FIRE_AIMFAN, 5, 2, 4, FIRE_END };
static const u8 fd_gun_iowa[] = { 32, 18, FIRE_AIMFAN, 5, 2, 5, FIRE_END };  /* 5面: 速い5-way高速弾 */
static const u8 *const fd_gun_stage[STAGE_COUNT] = {
    fd_gun_bb, fd_gun_cv, fd_gun_hd, fd_gun_tw, fd_gun_iowa
};
/* 戦闘機の発砲: 45f毎に自機狙い＋散らし円錐(±3)。空中の的なので抑え込みは無し(suppress=0)。 */
static const u8 fd_faim[] = { 40,  0, FIRE_AIMED, 3, 1, 3, FIRE_END };

/* ★海イントロ敵機=各面ボス艦の所属国の典型機(主人公=零戦/日本なので敵は各国海軍)。
   面順=BB(独)/Carrier(米)/Hood(英)/Twins(独)/Iowa(米)。形(pat)＋視認性優先色(col)で識別。 */
/* ★空戦戦闘機は8方向手続き生成(SPR_PLANE_*)へ移行。機種の雰囲気は生成器の翼幅(pl_wingd)で出す。 */
static const u8 fighter_col[STAGE_COUNT]  = { 3, 12, 9, 14, 11 };  /* 独緑/米橙/英オリーブ/独灰/米赤(単色fallback) */
static const u8 fighter_arch[STAGE_COUNT] = { 0, 2, 1, 0, 2 };     /* 挙動: 独=急降下/米=直進/英=蛇行 */
static const u8 fighter_iv[STAGE_COUNT]   = { 40, 28, 40, 40, 28 };/* 出現間隔(米面は数で押す=短い) */

/* ★海イントロ共通BGM(gen_assets track7=スロー渋・予感)。海(敵艦未出現)の間だけ鳴らし、敵艦が見えたら面別へ切替。 */
#define BGM_SEA_INTRO 7
/* 敵機の行別カラー(陰影16B)は面別にデータバンク(fighter_ctab_off)へ置き、stage_build で当該面の
   16BをRAMへ読む(常駐節約)。海イントロ機／艦載機の coltab に使う。 */
static u8 cur_ctab[16];

static u16 rng;
u8  rnd(void) { rng = rng * 25173 + 13849; return (u8)(rng >> 8); }   /* ★非static: hot.c(RAM実行のaa_update)から参照 */

u8 curstage;   /* 現在の面(0..STAGE_COUNT-1)。stage_init が g_stage_sel から設定。★非static: hot.c(aa)と共有 */
/* 面名(開始カード)/撃沈メッセージ(結果画面)はデータバンク(stagename_off/sunk_off, 各16Bスロット)に置き、
   stage_build で当該面の16BをRAMへ読む(常駐節約)。旧版 g_L[10..14] の撃沈メッセージ相当。 */
static char cur_name[16];   /* 現在面の艦名(NUL終端) */
static char cur_sunk[16];   /* 現在面の「[艦名] SUNK」(NUL終端) */

/* 主砲塔(破壊可能)を艦上の世界座標に配置。艦中心 x=120(sprite左上→中心128)。世界Y=艦頭(SC_SHIP_R0*16)+艦内y。
   hp=5(旧版準拠): 抑え込み(肉薄で撃たせない)で安全に連射しないと落としにくい=「ゼロ距離抑え込み=最速撃破」を要求。
   撃破後は active維持のまま hidden(砲身消失)＝scene が炎上残骸をBGへ焼付ける(burn_new_turrets)。 */
static void spawn_turret(u8 shipX, u16 shipY, u8 delay) {
    Entity *e = ent_spawn(ET_TURRET);
    if (e) {
        g_lturret++;   /* ★生存砲台O(1)カウンタ(stage_buildで0初期化済み。当たり判定の撃破で--) */
        e->ax = (s16)shipX - 8;                /* 砲塔中心x→スプライト左上(中心x=shipX) */
        e->ay = (s16)(SC_SHIP_R0 * 16 + shipY) - 8;   /* 砲身スプライト(旋回中心=8,8)をドーム中心に合わせる */
        e->hp = 5; e->fire = fd_gun_stage[curstage]; e->ftimer = delay;   /* 耐久5(旧版) */
        e->vx = 4; e->vy = 0; e->h = 0;        /* 砲身の向き=下 / 旋回冷却 / 命中フラッシュ残 */
        e->pat = (u8)(SPR_BARREL0 + 4 * 4);    /* 可動砲身(下向き, BGドームに重なる) */
        e->coltab = barrel_col;                /* 金属シェード(行別カラー) */
    }
}
/* 各面の主砲4基の艦内(x,y)はデータバンク(gun_off, 面別12B=[x0-3,u16 y0-3])に置き、
   stage_build で当該面をRAMへ読む(常駐節約)。空母/双子はx左右に分かれる。 */
static u8  cur_gun_x[4];
static u16 cur_gun_y[4];

/* 空母(2面)の停泊F6F(甲板8機)。エンティティ(ET_PARKED)で艦上に静止=破壊/発艦できる。中央列4＋左列4。 */
static void spawn_parked(u8 shipX, u16 shipY) {
    Entity *e = ent_spawn(ET_PARKED);
    if (e) {
        e->ax = (s16)shipX - 8;                         /* 艦上世界アンカー(中心=shipX,ship座標y) */
        e->ay = (s16)(SC_SHIP_R0 * 16 + shipY) - 8;
        e->pat = SPR_HELLCAT; e->coltab = cur_ctab; e->shadow = 1;
    }
}

/* 戦艦を バッファB へ事前描画(旧版忠実)。海テンプレ→OPS(+ops2)をRAMへ読み ship_render(艦種別)。
   ★重い(数千VDP塗り)ので、Bに既に現在の艦が居るなら再生成しない(ミス再挑戦=即再開)。 */
/* ★ship_ram: 艦体OPSの展開バッファ。stage_build 時だけ使う冷データ(gameplay中は死)なので、常駐DATAを
   食わず hot_ram(RAM実行コード)枠を空けるため固定番地 0xE700 に置く(g_card_ram 0xE100 の直後、gameplay中フリー帯)。 */
static u8 __at(0xE700) ship_ram[SHIP_OPS_RAM_MAX];
static u8 ship_ram2[128];        /* ops2(空母のみ, max85) */
static s8 rendered_stage = -1;   /* バッファBに描画済みの面(-1=未) */
/* 面別テーブルを当該面ぶんRAMへ(艦名/撃沈文/敵機カラー/主砲座標)。★開始カードは stage_build より
   先に艦名を描くので、prerender_ship の early-return とは独立に、カード描画前にも呼べるよう分離。 */
static void load_stage_data(void) {
    data_read(ASSET_BANK, (u16)(stagename_off + ((u16)curstage << 4)), (u8 *)cur_name, 16);
    data_read(ASSET_BANK, (u16)(sunk_off + ((u16)curstage << 4)), (u8 *)cur_sunk, 16);
    data_read(ASSET_BANK, (u16)(fighter_ctab_off + ((u16)curstage << 4)), cur_ctab, 16);
    { u8 g[12], k; data_read(ASSET_BANK, (u16)(gun_off + (u16)curstage * 12), g, 12);   /* 主砲4基の艦内座標 */
      for (k = 0; k < 4; k++) { cur_gun_x[k] = g[k]; cur_gun_y[k] = (u16)(g[4 + k * 2] | ((u16)g[5 + k * 2] << 8)); } }
}
static void prerender_ship(void) {
    if (rendered_stage == (s8)curstage) return;   /* 既に描画済み=再生成不要 */
    vdp_sprites(0);                            /* ★数千の一括塗り中はスプライトOFF=VDP帯域回復(生成が高速化)。
                                                  カード表示中でスプライトは元々不要。 */
    scroll_build_sea();                        /* 海テンプレート(512) */
    data_read(ASSET_BANK, ship_ops_off[curstage],  ship_ram,  ship_ops_len[curstage]);
    data_read(ASSET_BANK, ship_ops2_off[curstage], ship_ram2, ship_ops2_len[curstage]);
    ship_render(ship_kind[curstage], ship_hull[curstage], ship_bowcnt[curstage], ship_bowyb[curstage],
                ship_aagtbl[curstage], ship_aagp[curstage], ship_ram, ship_ram2);
    vdp_sprites(1);                            /* スプライト復帰(ゲーム開始前) */
    rendered_stage = (s8)curstage;
}

/* 開始カードの艦画像: 事前ベイク 64x48 を バンク→常駐RAM(g_card_ram)へ読み、2倍拡大(重いループ)は
   冷たいバンク(draw_card_banked)で page0 中央窓へ展開(常駐節約)。data_read は常駐で先に済ませる。 */
static void draw_card_ship(void) {
    data_read(SHIP_CARD_BANK, ship_card_off[curstage], g_card_ram, SHIP_CARD_LEN);
    draw_card_banked();
}

#define WMAX 40   /* 横揺れ(weaveX)の振幅 */

u16 cam;   /* ★非static: hot.c(aa_update の画面Y算出)と共有 */
static u8  phase;     /* 0=海(直進) / 1=戦艦(往復蛇行) */
static u8  sdiv, wtimer, ftick;
static s8  camdir;    /* 往復方向: -1=船首へ / +1=船尾へ */
static s16 weaveX;
static s8  wdir;
static u8  dmode;     /* 撃破演出モード(0=通常 / 1=炎上スペクタクル中) */
static u8  dtimer;    /* 撃破演出の残フレーム */
static u8  raided;    /* 1=戦艦出現時に空襲(戦闘機/敵弾)を一掃済み */
#ifdef DEBUG_FPS
u8 g_dbgmask;   /* ★デバッグ: bit1=海/bit2=AA/bit4=更新/bit8=衝突/bit16=描画 を個別停止。M(TRIGB)でプリセット巡回。hud_drawが値表示 */
#define DBG_ON(bit) (!(g_dbgmask & (bit)))
#else
#define DBG_ON(bit) 1
#endif

/* weaveX を横HWスクロール(R#26/27)へ。滑らかな左寄せは R#26=ceil(s/8)/R#27=(8-frac)。 */
static void apply_weave(void) {
    u8 s = (u8)((256 - (weaveX & 0xFF)) & 0xFF);
    u8 frac = (u8)(s & 7);
    vdp_set_hscroll((u8)(((s >> 3) + (frac ? 1 : 0)) & 0x1F), (u8)((8 - frac) & 7));
    g_meander = weaveX;   /* 砲塔スプライトの横追従(次段の砲塔統合で使用) */
}

/* ===== 対空砲23基の発砲(旧版 airburst 移植) =====
   艦の対空砲は バッファB に描画済みで実体は持たない。ここで座標(ship_aag_pos)を毎フレーム回し、
   画面帯[8,200]に居るものだけ自機狙いで撃つ。前14=大型→時限信管エアバースト / 後9=小型→通常小弾。
   面別間隔 aafire_iv[](小=激しい)＋砲ごとの位相ずらし。艦が画面に居る時(sy帯内)だけ発砲。 */
/* ★AA状態は hot.c(RAM実行の aa_update/aa_collide)と共有するため非static化(aa_hot.h で公開)。
   実体は常駐DATA(このTU)に置き、hot.c は extern 参照する(RAM実行モジュールにDATAを持たせない規律)。 */
u8 aa_fire[SHIP_NAAG];
u8 aa_hp[SHIP_NAAG];     /* 対空砲の耐久(大型=前14基:2 / 小型=後9基:1)。0で aa_dead */
u8 aa_dead[SHIP_NAAG];   /* 1=破壊(発砲停止・炎上) */
/* ★可視AAリスト: aa_update が23基を1回走査する際、当たり範囲(-8..216)内の生存AAだけを
   sx/sy付きで記録。aa_collide は23基再走査・sx/sy再計算をせず、このリストだけを回す
   (艦496px>画面212pxで常に約半数が画面外=当たり判定の23基ループを可視分に短縮)。
   ★aa_update→(ent_resolve_collisions)→aa_collide の順で、間にAA状態は変化しないので有効。 */
u8  aa_nvis;
u8  aa_vis_i[SHIP_NAAG];   /* 可視AAの砲index */
s16 aa_vis_sx[SHIP_NAAG];  /* 画面X(蛇行込み) */
s16 aa_vis_sy[SHIP_NAAG];  /* 画面Y */
/* aa_update/aa_collide の本体は banked/hot.c(RAM実行)へ移設。aafire_iv も hot.c 側へ移した。 */
static void aa_reset(void) {
    u8 i;
    for (i = 0; i < SHIP_NAAG; i++) {
        u16 t = (u16)60 + (u16)i * 11;   /* ★u16で計算し255クランプ。u8のままだと高iで桁溢れ(例 i=20→320&FF=64)し初期CDが乱れる */
        aa_fire[i] = (t > 255) ? 255 : (u8)t;
        aa_hp[i]   = (i < 14) ? 2 : 1;   /* 旧版準拠: 大型HP2 / 極小HP1 */
        aa_dead[i] = 0;
    }
}
static u8 aa_alive(void) { u8 i, n = 0; for (i = 0; i < SHIP_NAAG; i++) if (!aa_dead[i]) n++; return n; }
/* aa_update / aa_collide の本体は banked/hot.c(RAM実行)へ移設。ここからは hotcode.c のラッパ
   (aa_update/aa_collide=hot_ram のジャンプテーブル)を aa_hot.h 経由で呼ぶ。 */

/* ===== 破壊した砲台/対空砲の常時炎上(BG火球ブリット。旧版 fireball 移植) =====
   スプライト枠(32/8perline)を使わず、事前ベイクした火球を page1リングへ透過コピーで毎フレーム重ねる。
   → 全27エンプレを同時炎上でき、破壊済みか一目で分かる(小炎が消える時間帯が無い)。 */
#define FB_PAGE0_Y 0             /* 火球ベイク先(page0の非表示域 上端。ゲーム中は表示page1) */
static const u8  fb_box[3] = { 32, 22, 16 };            /* 0=大(主砲=ドームを包む) 1=中(大型AA) 2=小(極小AA) */
/* page0非表示域の各コマのベイクX(6枚: s0f0,s0f1,s1f0,s1f1,s2f0,s2f1)。1行内に横並び(<256px)。 */
static const u8  fb_px[6] = { 0, 32, 64, 86, 108, 124 };

/* 火球6枚(丸・2コマ×3サイズ: gen_assetsがビルド時ベイク)を バンク→RAM→page0非表示域 へ展開。 */
/* ★fb_ram: 火球ベイクの一時バッファ。bake_fireballs(セットアップ時)だけ使う冷データなので、
   常駐DATAを食わず hot_ram 枠を空けるため固定番地 0xE900 に置く(ship_ram 0xE700 の後、gameplay中フリー帯)。 */
static u8 __at(0xE900) fb_ram[FB_RAM_MAX];
static void bake_fireballs(void) {
    u8 k;
    for (k = 0; k < 6; k++) {
        u8 box = fb_box[k >> 1], bytes = (u8)(box / 2), row;
        data_read(ASSET_BANK, fb_off[k], fb_ram, (u16)((u16)box * bytes));
        for (row = 0; row < box; row++) {
            const u8 *p = &fb_ram[(u16)row * bytes];
            u8 cb;
            vdp_write_addr((u16)((u16)(FB_PAGE0_Y + row) * 128 + fb_px[k] / 2));
            for (cb = 0; cb < bytes; cb++) vdp_data(p[cb]);
        }
    }
}

/* ★炎上サイト表: エンプレ撃破時に (left, worldY上端, サイズ) を1件追記。fire_draw はこの表だけを走査。
   ★炎は「艦バッファBへ焼込み → scroll_repaint_rows で該当世界行だけリングへ反映」の一本道で描く。
     以前は表示リングへ直接 ring_blit_t していたが、リング256px<艦496px のため 256px 離れた砲台に
     炎がエイリアスして乗る不具合(手前砲台を壊すと奥砲台に半分炎)があった。B経由なら draw_row が
     行ごとに正しいリング位置へ写すのでエイリアスしない。 */
#define BURN_MAX 48              /* 主砲4＋対空砲23＋撃破時の散布火球 */
static u8  burn_left[BURN_MAX];  /* 火球の左X(page1)。box≤32で0..224=u8可 */
static u16 burn_wtop[BURN_MAX];  /* 火球の世界Y上端 */
static u8  burn_sz[BURN_MAX];    /* 0大/1中/2小 */
static u8  burn_coma[BURN_MAX];  /* ★各炎が現在Bへ焼込み済みのコマ(0/1)。fire_draw はコマ変化時のみ再焼込み=無駄コピー排除 */
static u8  nburn;
/* 炎球1個を艦バッファBへ透過焼込み。src=page0の火球コマ列位置(fb_px添字)。 */
static void burn_bake(u8 left, u16 wtop, u8 box, u8 src) {
    u16 bufY = (u16)((s16)SC_SHIPBUF_Y + (s16)wtop - SC_SHIP_R0 * 16);
    vdp_copy_t(fb_px[src], FB_PAGE0_Y, (u16)left, bufY, box, box);
}
/* ★撃破時に1回: 表へ登録＋frame0をBへ焼込み＋その世界行だけ即リングへ反映(画面内で死んでも即炎)。
   アニメ上書きは fire_draw が8フレームに1回。 */
void burn_add(s16 cx, u16 worldY, u8 s) {   /* ★非static: hot.c(aa_collide)が撃破時に呼ぶ */
    u8 box = fb_box[s]; s16 left = (s16)(cx - box / 2); u16 wtop;
    if (nburn >= BURN_MAX) return;
    if (left < 0) left = 0; else if (left > (s16)(256 - box)) left = (s16)(256 - box);
    wtop = (u16)((s16)worldY - box / 2);
    burn_left[nburn] = (u8)left; burn_wtop[nburn] = wtop; burn_sz[nburn] = s;
    burn_coma[nburn] = 0;   /* frame0を焼込む(下)ので現コマ=0。以後 fire_draw はコマ変化時のみ再描画 */
    nburn++;
    rendered_stage = -1;   /* ★Bを炎で汚したので、同一面リスタート時は必ず艦を描き直させる
                              (でないと撃墜やり直しで前ライフの破壊痕がBに残り「壊れた艦＋復活砲台」に) */
    burn_bake((u8)left, wtop, box, (u8)(s * 2));                 /* frame0をBへ焼込み */
    scroll_repaint_cols((s16)(wtop >> 4), (s16)((wtop + box - 1) >> 4), (u8)left, box);  /* ★炎の列幅だけ即リングへ(全幅256の約1/8) */
}
/* 新たに撃破された主砲(hp==0)を検出し burn_add で登録＋B焼込み。
   ★炎の2コマ点滅アニメ: 「毎フレーム1炎だけ」ラウンドロビンで現コマをBへ再焼込み＋その2-3行だけ
     リング反映(scroll_repaint_rows=B経由=正位置・エイリアス無し)。負荷を1炎/フレームに分散するので、
     以前の「8フレーム毎に全域(14行)一括再描画」で起きた約0.5秒周期のヒッチが出ない。 */
static u8 fb_anim;   /* コマ0/1の切替タイマ */
static u8 fb_rr;     /* ラウンドロビン対象の炎添字 */
static void fire_draw(void) {
    u8 i;
    Entity *e = ent_pool();           /* ポインタ加算走査=ent_at(i)の関数呼び+乗算を排除 */
    for (i = 0; i < ENT_MAX; i++, e++) {   /* 新たに撃破された主砲(hp==0)を検出して登録＋B焼込み */
        if (e->active && e->type == ET_TURRET && e->hp == 0 && e->color != 0xFE) {
            e->color = 0xFE; burn_add((s16)(e->ax + 8), (u16)(e->ay + 8), 0);
        }
    }
    if (!nburn) return;
    fb_anim++;
    if (fb_rr >= nburn) fb_rr = 0;
    {   /* ラウンドロビンで1炎を見るが、★コマが変化した時だけ 再焼込み＋その行のリング反映を行う。
           コマは8フレームに1回しか反転しないので、毎フレーム再描画していた従来比で VDPコピーを約1/8へ削減
           (全幅256×16の行再描画がCE待ちの主因だった)。ピークは従来同様「1炎/フレーム」に分散されたまま。 */
        u8 fr = (u8)((fb_anim >> 3) & 1);   /* 8フレーム毎にコマ反転 */
        if (burn_coma[fb_rr] != fr) {
            u8 sz = burn_sz[fb_rr], box = fb_box[sz];
            burn_bake(burn_left[fb_rr], burn_wtop[fb_rr], box, (u8)(sz * 2 + fr));
            scroll_repaint_cols((s16)(burn_wtop[fb_rr] >> 4), (s16)((burn_wtop[fb_rr] + box - 1) >> 4), burn_left[fb_rr], box);  /* ★炎の列幅だけ(部分幅) */
            burn_coma[fb_rr] = fr;
        }
    }
    fb_rr++;
}

/* ===== 艦種別固有兵装(旧版移植) =====
   ボス(戦艦)交戦=phase1 の間だけ、各面のボス艦に固有の攻撃を出す(空母=艦載機射出 等)。
   AA/主砲に上乗せする「その艦らしさ」。面順=BB/Carrier/Hood/Twins/Iowa。 */
static u8 spc_timer;
static void special_reset(void) { spc_timer = 100; }
static void special_update(void) {
    if (phase != 1) return;                    /* 戦艦が出てからのみ */
    if (spc_timer) { spc_timer--; return; }
    if (curstage == 1) {                        /* 2面 空母: 停泊F6Fが「その停泊位置から」発艦→8方向追尾 */
        if (ent_count(ET_PURSUER) < 3) {        /* 同時最大3機(旧版 NPLANE) */
            u8 i; Entity *e = (Entity *)0;
            for (i = 0; i < ENT_MAX; i++) {     /* 画面内の停泊機を1機選ぶ(その位置から浮上=停泊数が減る) */
                Entity *p = ent_at(i);
                if (p->active && p->type == ET_PARKED && p->y > 8 && p->y < 200) { e = p; break; }
            }
            if (e) {                            /* 停泊機→追尾機へ差し替え(x/yは現位置を継承=停泊位置から浮上) */
                e->type = ET_PURSUER; e->ax = 4; e->ftimer = 34;
                sfx(1, SFX_EFIRE);
            }
            spc_timer = diff_interval(120);                     /* 次の発艦まで(旧版 ep_launch) */
        } else spc_timer = 30;                   /* 満杯なら短く再試行 */
    } else if (curstage == 2) {                   /* 3面 フッド: 舷側から潜水艦ミサイル(浮上→弱誘導→8方向炸裂) */
        if (ent_count(ET_SMISSILE) < 3) {
            Entity *e = ent_spawn(ET_SMISSILE);
            if (e) {
                s8 side = (rnd() & 1) ? 1 : -1;
                e->x = (s16)(128 + g_meander) + (s16)side * 46;   /* 舷側 */
                e->y = (s16)(40 + (rnd() % 120));
                e->ax = 1; e->ay = side; e->ftimer = 24;
            }
            sfx(1, SFX_EFIRE);
            spc_timer = diff_interval(90);
        } else spc_timer = 30;
    } else if (curstage == 3) {                   /* 4面 双子: 左右の艦から弾が中心へ収束→合体して自機狙いの高速大弾 */
        if (ent_count(ET_COMBO) < 2) {
            Entity *e = ent_spawn(ET_COMBO);
            if (e) {
                e->ay = (s16)(128 + g_meander);          /* 合体中心x(2隻の中間) */
                e->y  = (s16)(40 + (rnd() % 80));        /* 発射帯 */
                e->x  = 52;                              /* 初期 off = CB_GAP(左右の艦) */
                e->ftimer = 24;                          /* 収束フレーム */
                e->hidden = 1;                           /* 実体は非表示(合体パスで2発描画) */
            }
            sfx(1, SFX_EFIRE);
            spc_timer = diff_interval(80);
        } else spc_timer = 30;
    } else if (curstage == 4) {                   /* 5面 アイオワ: 画面端から連続空襲(F4U)。ホバー無し即追尾=総攻撃 */
        if (ent_count(ET_PURSUER) < 4) {
            Entity *e = ent_spawn(ET_PURSUER);
            if (e) {
                e->x = (rnd() & 1) ? -16 : 268;          /* 左右端 交互 */
                e->y = (s16)(16 + (rnd() % 168));
                e->ax = 4; e->ftimer = 0;                /* 展開無し=即追尾 */
                e->pat = (u8)(SPR_PLANE_L + 4 * 4); e->coltab = cur_ctab; e->shadow = 1;  /* F4U(生成8方向・下向き大初期)＋落ち影 */
            }
            sfx(1, SFX_EFIRE);
            spc_timer = diff_interval(45);                              /* 総攻撃=短間隔 */
        } else spc_timer = 20;
    }
}

/* 設定の残機初期値(config g_lives_idx→2/3/5)。 */
static u8 lives_init(void) {
    static const u8 t[3] = { 2, 3, 5 };
    return t[(g_lives_idx < 3) ? g_lives_idx : 1];
}

/* 1回の挑戦のレイアウトを「表示を切替えず」構築(艦はオフスクリーンのバッファBへ事前描画)。
   ★これを開始カード表示中に呼ぶことで、カードの裏でステージ準備が進む(旧版と同じ)。
   スコア/残機は触らない(それらは新規ゲーム=stage_init が初期化)。 */
static void stage_build(void) {
    Entity *e;
    load_stage_data();    /* 艦名/撃沈文/敵機カラー/主砲座標を当該面ぶんRAMへ(ミス再開経路も網羅) */
    prerender_ship();     /* 艦をバッファB(オフスクリーン)へ。stage_begin_display の前に必須 */
    vdp_sprite_init();
    sprites_load(curstage);   /* ★静的パターン＋この面の戦闘機8方向×3サイズを生成 */
    hud_init();           /* 数字パターン投入＋HUDスロット確保(g_spr_base) */
    ent_reset();
    cam = SC_CAM_START; phase = 0; sdiv = 0; wtimer = 0; ftick = 0;
    weaveX = 0; wdir = 1; camdir = -1; g_meander = 0; rng = 0x1234;

    e = ent_spawn(ET_PLAYER);
    if (e) { e->x = 120; e->y = 176; e->pat = SPR_ZERO; e->coltab = zcol; e->shadow = 1; }  /* 零戦＋行別陰影＋落ち影 */

    g_php = g_durability ? g_durability : 1;   /* 1機あたりの耐久HP(設定) */
    g_pinv = 0;
    g_miss = 0;
    dmode = 0; dtimer = 0; raided = 0;

    /* 破壊可能主砲塔(ビスマルク配置=前2/後2。海フェーズ中は画面外)。全撃破でクリア。
       艦内Yは ship_top/ship_bot のマウント位置と一致(前:72/108, 後:300/344)。 */
    g_gun_kills = 0;
    g_lturret = 0;         /* ★生存砲台O(1)カウンタを0初期化(直後のspawn_turret×4で++) */
    aa_reset();            /* 対空砲の発射タイマ初期化 */
    nburn = 0;             /* 炎上サイト表クリア(面リスタートで炎を消す) */
    special_reset();       /* 艦種別固有兵装のタイマ初期化 */
    spawn_turret(cur_gun_x[0], cur_gun_y[0], 30);
    spawn_turret(cur_gun_x[1], cur_gun_y[1], 45);
    spawn_turret(cur_gun_x[2], cur_gun_y[2], 60);
    spawn_turret(cur_gun_x[3], cur_gun_y[3], 75);
    if (curstage == 1) {   /* 空母: 停泊F6F 8機を甲板へ(中央列x120×4＋左列x98×4) */
        u8 i;
        for (i = 0; i < 4; i++) spawn_parked(120, (u16)(130 + i * 40));
        for (i = 0; i < 4; i++) spawn_parked(98,  (u16)(150 + i * 40));
    }
}

/* ★開始直後の数フレーム: 画面揺れ/ヒットストップを強制0にする保険。
   stage開始時(BGM/ファンファーレ起動あたり)に g_shake へ一度だけ大きな値(実測51)が紛れ込む不具合があり、
   出だしのスクロールが数フレーム上下にガクつく。ソース上 g_shake への代入は 6/8/12 の3箇所のみ=51は誤書込。
   出だしは正規の揺れも無いので、開始後 SFRESH_HOLD フレームは強制的に 0 へ落として出だしを安定させる。 */
static u8 sfresh;
#define SFRESH_HOLD 8

/* 地形リングを表示(page1)＝ここでゲーム画面が現れる。scroll_init は prerender 済みが前提。 */
static void stage_begin_display(void) {
    scroll_init();               /* display=page1(以降 page0 は非表示=火球ベイク用に空く) */
    bake_fireballs();            /* 破壊エンプレ炎上用の火球6枚を page0(非表示域)へ事前ベイク */
    sea_init(curstage);          /* 艦種別の海コラム帯を選択 */
    vdp_set_hscroll(0, 0);
    g_shake = 0; g_hitstop = 0; sfresh = SFRESH_HOLD;   /* ★出だしの誤揺れ抑止(stage_update冒頭でも数フレーム強制0) */
}

/* ミス再挑戦: 開始カード/ファンファーレ無しで即再構築。海(phase0)から再開なので海イントロ共通BGMへ戻す。 */
static void stage_setup(void) {
    stage_build();
    bgm_play(BGM_SEA_INTRO);
    stage_begin_display();
}

/* 面別BGM(gen_assets のトラック順: 0=title/1=BBマーチ/2=ED/3=空母/4=フッド哀歌/5=双子/6=Iowa/7=海イントロ)。
   順=BB/Carrier/Hood/Twins/Iowa。 */
static const u8 stage_bgm[STAGE_COUNT] = { 1, 3, 4, 5, 6 };

/* ステージ開始シーケンス(旧版準拠)。各面の開始時(新規ゲーム/次面へ)にだけ実行=ミス再挑戦では出さない。
   手順: BGM停止(無音) → カード(STAGE n/TARGET/艦名/シルエット)を page0 に描く
        → ★カードの裏でステージ準備(stage_build=艦の事前描画・砲台配線をオフスクリーンで)
        → 開始ファンファーレ(BGM無音でこれだけ鳴る) → 余韻
        → メインBGM開始 → 地形を表示(stage_begin_display)＝ゲーム開始。
   艦名は可変長なので中央寄せ(8px/char)。 */
/* 開始カード(STAGE n / TARGET / 艦名 / 艦シルエット)を page0 に描く。stage_intro とビューアで共用。 */
static void draw_stage_card(void) {
    const char *nm;
    u8 n = 0;
    char num[2];
    load_stage_data();                      /* ★カードで艦名を描く前に当該面の艦名等をRAMへ */
    nm = cur_name;
    while (nm[n]) n++;                       /* 艦名の長さ(中央寄せ用) */

    bgm_stop();                              /* カード中は無音(タイトル/前面のBGMを止める) */
    vdp_set_vscroll(0);                      /* 縦スクロール解除(page0のズレ防止) */
    vdp_sprite_hide_from(0);                 /* スプライト全消し */
    vdp_set_display_page(0);
    vdp_fill(0, 0, 256, 212, 1);            /* 背景=海の濃紺(旧パレット色1=1,4,5)。装飾ラインは無し */

    /* 見出しは2倍角(旧版準拠)。STAGE n / - TARGET - / 艦名 を中央寄せで大きく。地色=背景(1)。 */
    num[0] = (char)('1' + curstage); num[1] = 0;
    vdp_text_s(72, 18, 15, 1, 2, "STAGE");         /* "STAGE"(5字×16=80) 72..152。白で視認性確保 */
    vdp_text_s(168, 18, 15, 1, 2, num);            /* n は空白1つ空けて 168 */
    vdp_text_s(48, 44, 11, 1, 2, "- TARGET -");    /* 10字×16=160 → x48 中央(赤) */
    vdp_text_s((u8)(128 - n * 8), 176, 15, 1, 2, nm);   /* 艦名(2倍角・中央寄せ, 1字=16px) */
    draw_card_ship();                       /* 事前ベイク艦画像を即blit=カード完成(重い生成を待たない) */
}

/* ステージ開始シーケンス(旧版準拠)。各面の開始時(新規ゲーム/次面へ)にだけ実行=ミス再挑戦では出さない。
   カード表示→(裏で)艦事前描画→開始ファンファーレ→余韻→メインBGM→地形表示(ゲーム開始)。 */
static void stage_intro(void) {
    u8 f;
    draw_stage_card();
    stage_build();                          /* ★カードの裏でゲーム本体の艦をバッファBへ生成(重い) */
    play_fanfare_open();                    /* 開始ファンファーレ(BGM無音でこれだけ鳴る) */
    for (f = 0; f < 40; f++) vdp_wait_frame();     /* 少し余韻(旧版と同じ40フレーム) */
    bgm_play(BGM_SEA_INTRO);                 /* まず海イントロ共通BGM(敵艦が見えたら面別へ切替) */
    stage_begin_display();                   /* 地形を表示=ゲーム開始 */
}

/* 画面ビューア: 表示済みの画面でトリガ待ち(連射で飛ばさぬよう一度離してから)。最大4秒で自動復帰。 */
static void view_wait(void) {
    u8 armed = 0; u16 f;
    for (f = 0; f < 240; f++) {
        input_poll();
        if (!(g_input & INP_TRIG)) armed = 1;
        if (armed && (g_input_edge & INP_TRIG)) break;
        vdp_wait_frame();
    }
}

/* シーン入場(新規ゲーム): スコア/被弾/残機を初期化し、開始面(config選択)からレイアウト構築。 */
static void results_and_fanfare(void);   /* 前方宣言(ビューアの結果表示で使う。定義は下方) */

void stage_init(void) {
    g_score = 0;
    g_kills = 0; g_playerhit = 0;
    g_lives = lives_init();
    curstage = (g_stage_sel < STAGE_COUNT) ? g_stage_sel : 0;
    /* ★VRAMキャッシュ無効化(リセット漏れ対策): タイトル(SCREEN12)→ゲーム(SCREEN5)入場で scene_video_enter が
       CHGMOD を実行し、実機turboR BIOSはこれで海テンプレ/艦バッファB領域(VRAM 0x10000〜=page2/3)まで消す。
       同一面をやり直すと rendered_stage==curstage で prerender_ship(=scroll_build_sea+艦描画)がスキップされ、
       消えたテンプレが再構築されず海が崩壊する(die→GAMEOVER→タイトル→再スタートで再現。実機のみ=C-BIOSは
       CHGMODの消去範囲が狭く再現しない)。新規ゲーム入場は必ず画面モード往復を伴うので、ここで無効化して
       CHGMOD後の再構築を強制する。RAM状態の残留(g_scene)は既に対策済みだが、VRAMキャッシュ有効フラグと
       実VRAMがCHGMODで乖離する同種の穴だった。 */
    rendered_stage = -1;
    /* ★画面ビューア(config設定): 各画面を個別に表示して確認できる。表示後は stage_update が SC_TITLE を返す。 */
    if (g_view == 1) { draw_stage_card(); view_wait(); return; }              /* ステージ説明カードのみ */
    if (g_view == 2) { load_stage_data(); results_and_fanfare(); return; }    /* 撃破結果(SUNK)画面のみ */
    stage_intro();        /* 1面開始: カード＋ファンファーレ→準備→BGM→開始 */
}

/* スコアを5桁ゼロ詰め文字列へ(結果画面表示用)。 */
static char scorebuf[6];
static void fmt_score(u16 v) {
    u8 i;
    for (i = 5; i > 0; i--) { scorebuf[i - 1] = (char)('0' + (v % 10)); v /= 10; }
    scorebuf[5] = 0;
}

/* 撃破!! パネルの1bppデータをバンク→RAMへ(結果画面の直前に1回)。 */
static u8 panel_ram[PANEL_WB * PANEL_H];
/* 撃破!! パネルを透過描画(1bit=1px正確)。★旧版(cport)踏襲: 各行で立ちビットのラン(連続)を検出し
   1本を vdp_fill(LMMV=コマンド)で塗る。以前の「vdp_write_addr+vdp_data で1画素ずつ直接VRAM書込」は、
   細切れの di/ei の隙間に60Hz割込み(BIOSがVDPステータス読取)が刺さってVDP状態がずれ、横方向に
   潰れて化けた(撃沈画面の撃破!!が白黒ぐしゃ)。コマンドエンジンは1コマンド=原子的で割込み耐性がある。
   透過: 立ちビットのランだけ oncol で塗る=オフ画素は下地/影が残る。dstx は偶数前提。 */
static void blit_panel_t(u16 dstx, u16 dsty, u8 oncol) {
    u8 y, c;
    for (y = 0; y < PANEL_H; y++) {
        const u8 *row = &panel_ram[(u16)y * PANEL_WB];
        c = 0;
        while (c < PANEL_W) {
            if (row[c >> 3] & (u8)(0x80 >> (c & 7))) {
                u8 run = 1;
                while ((u8)(c + run) < PANEL_W &&
                       (row[(u8)(c + run) >> 3] & (u8)(0x80 >> ((c + run) & 7)))) run++;
                vdp_fill((u16)(dstx + c), (u16)(dsty + y), run, 1, oncol);   /* ラン1本=1回のLMMV(割込み耐性) */
                c = (u8)(c + run);
            } else c++;
        }
    }
}

/* 撃破結果画面(page0)＋勝ちどきファンファーレ(前景・ブロッキング)。終わりにトリガ待ち。 */
static void results_and_fanfare(void) {
    u8 f;
    vdp_set_vscroll(0);                 /* 縦スクロール解除(page0テキストのズレ＋上端ゴミを防ぐ) */
    vdp_set_hscroll(0, 0);              /* ★横スクロール(蛇行weaveX)も解除=残ると画面全体が右に寄る */
    vdp_sprite_hide_from(0);            /* スプライト全消し(停止マーカを slot0 へ) */
    vdp_set_display_page(0);            /* 結果は非スクロールの page0 に描く */
    vdp_fill(0, 0, 256, 212, 1);        /* 背景=エンディング/開始カードと同じ青(色1)。トーン統一 */
    /* 撃破!! (魏碑の筆文字)。枠・赤は無し。ブラウン管ゴースト風に黒影を横+6/縦+2へ大きくずらす。
       ★透過blitで「青地 → 影(黒) → 本体(白)」の順に重ねる(不透過blitだと本体の地塗りが影を消す)。 */
    data_read(ASSET_BANK, panel_off, panel_ram, PANEL_LEN);   /* 撃破!!パネルをバンク→RAM */
    blit_panel_t(78, 42, 0);            /* 影=黒。透過=隙間から本体の裏へ影が残る。横+6/縦+2で大きく覗く */
    blit_panel_t(72, 40, 15);           /* 本体=白。透過で影の上に重ねる。中央 x72(=(256-112)/2) */
    /* [艦名] SUNK を白・2倍角で中央。 */
    { const char *m = cur_sunk; u8 n = 0;
      while (m[n]) n++;
      vdp_text_s((u8)((256 - (u16)n * 16) / 2), 100, 15, 1, 2, m); }
    if (g_score > g_hiscore) g_hiscore = g_score;
    fmt_score(g_score);
    vdp_text(72, 132, 15, 1, "SCORE");         /* すべて白=青背景でも視認できる */
    vdp_text(120, 132, 15, 1, scorebuf);
    fmt_score(g_hiscore);
    vdp_text(72, 152, 15, 1, "HI");
    vdp_text(120, 152, 15, 1, scorebuf);
    play_fanfare();                     /* 勝ちどき(BGM停止・前景同期) */
    vdp_text(88, 176, 15, 1, "PUSH SPACE");
    { u8 armed = 0;                     /* ★連射ホールドで一瞬で飛ばされないよう「一度離してから押す」を要求 */
      for (f = 0; f < 240; f++) {       /* 約4秒 or 新規トリガ押下で次へ */
          input_poll();
          if (!(g_input & INP_TRIG)) armed = 1;
          if (armed && (g_input_edge & INP_TRIG)) break;
          vdp_wait_frame();
      }
    }
}

/* 自機撃墜の沈没演出: スクロール凍結・自機位置に火球を降らせ轟音(旧版 player_burst 相当・短縮)。 */
/* 沈没演出/ゲームオーバー画面は冷たいバンク(bank16)へ移設(常駐節約)。ship.h の play_death_banked/
   game_over_banked を bcall。data_read を伴わない=窓を差し替えないので安全。 */

/* ゲームオーバー画面＋コンティニュー選択(旧版準拠・カウントダウン無し)。戻り 1=CONTINUE / 0=TITLE。
   継続ON時のみ CONTINUE/TITLE のカーソルメニュー。CONTINUE=残機初期化＋同面再開(スコア保持)。 */

/* 撃破演出(炎上スペクタクル): スクロール凍結・艦上へ爆発を降らせる＋轟音。尺が尽きたら結果へ。 */
static u8 defeat_update(void) {
    u8 iv;
    scroll_to(cam);                     /* 表示維持(cam凍結) */
    vdp_set_vscroll((u8)((s16)cam + (s16)(rnd() % 7) - 3));   /* 撃破の迫力: 縦±3px揺れ */
    iv = (dtimer < 50) ? 1 : 3;         /* クライマックス(残り<50)で爆発を倍密に */
    if ((dtimer & iv) == 0) ent_spawn_explosion((s16)(80 + (rnd() % 96)), (s16)(20 + (rnd() % 172)));  /* 艦の全幅に降らす */
    if ((dtimer % 12) == 0) sfx(2, SFX_BOOM);
    ent_update_all();                   /* 爆発アニメを進める */
    fire_draw();                        /* 全炎上サイト(27基＋撃破時に足した散布)を2コマ描画=艦を炎に包む */
    ent_draw_all();
    if (dtimer) dtimer--;
    if (dtimer == 0) {
        results_and_fanfare();
        if (curstage + 1 < STAGE_COUNT) {   /* 次の面へ(スコア/残機は持ち越し) */
            curstage++;
            stage_intro();      /* 次面開始: カード＋ファンファーレ→準備→BGM→開始 */
            return SCENE_NONE;
        }
        return SC_ENDING;               /* 最終面クリア → エンディング */
    }
    return SCENE_NONE;
}

static u8 vcnt;   /* 縦スクロール速度の位相(5コマ周期)。phase0=ゆっくり / phase1=高速(蛇行)。 */
#define SEA0_DIV 3    /* phase0(海モード)で海うねりを塗る間隔(3=3フレームに1回)。実機turboRに合わせ微調整可。 */
static u8 seatick;    /* phase0 海間引き用カウンタ。 */
u8 stage_update(void) {
    u8 vstep;
    if (g_view) { g_view = 0; return SC_TITLE; }   /* ★ビューア表示(カード/結果)はstage_initで完結→タイトルへ戻る */
    if (dmode) return defeat_update();  /* 撃破演出中は専用処理 */
    if (sfresh) { sfresh--; g_shake = 0; g_hitstop = 0; }   /* 出だしの誤揺れを抑止(上記) */
    if (g_hitstop) { g_hitstop--; return SCENE_NONE; }   /* ★ヒットストップ=数フレーム凍結(手応え) */
#ifdef DEBUG_FPS
    /* ★デバッグ(FPSビルド): M(TRIGB)で有用なプリセットを巡回。各モードのFPSで海モードの重さの内訳を切り分ける。
       0=通常 / 2=AA停止 / 4=更新停止 / 8=衝突停止 / 16=描画停止 / 14=AI全停止 / 1=海停止 / 31=全停止。 */
    if (g_input_edge & INP_TRIGB) {
        static const u8 pr[8] = { 0, 2, 4, 8, 16, 14, 1, 31 };
        static u8 di; di = (u8)((di + 1) & 7); g_dbgmask = pr[di];
    }
#endif
    if (++vcnt >= 5) vcnt = 0;

    /* ★§4-3: 早期return(g_view/dmode/hitstop)を抜けたここから page1 を RAM スロットへ。
       以降 phase処理(scroll_to/蛇行)・HUD・ホット区間(海/AI/衝突/描画/炎)まで page1常駐コードを
       R800で約3.8×速フェッチ。区間内で唯一バンキングする bgm_play(艦出現時1回)だけ一時cartへ退避する。
       区間の出口(fire_draw後 と 全早期return経路)で必ず page1_use_cart() に戻す。 */
    page1_use_ram();

    if (phase == 0) {
        /* 海のみ: ゆっくり前進。★毎フレーム1pxで動かす=停止フレームを作らない(整数スクロールで
           滑らかに出せる最も遅い一定速度)。旧実装は4/5コマだけ動かす=停止コマがカクつき「フレームレート
           低下」に見えていた。1px/f は蛇行(1.6px/f)より遅く、かつ完全に一定速度=滑らか。蛇行なし。 */
        vstep = 1;
        if (cam > SC_CAM_SHIP) { cam = (cam - SC_CAM_SHIP >= vstep) ? (u16)(cam - vstep) : SC_CAM_SHIP; }
        scroll_to(cam);
        /* 空戦(イントロ)は「戦艦が未出現の開けた海」の間だけ。艦が入り始めたら空襲終了
           (でないと戦闘機が上端=艦の上に突然湧いてゴミに見える。HANDOFF §2: 空戦→戦艦)。 */
        if (cam > SC_CAM_SHIP && (++ftick % diff_interval((u8)(fighter_iv[curstage] >> 1))) == 0) {  /* ★出現間隔を半分=海モードの戦闘機を倍増 */
            Entity *f = ent_spawn(ET_FIGHTER);
            if (f) {
                /* ★各面の固有挙動(急降下/直進/蛇行)に「蛇行」を50%混在させる=どの面でも一部が斜めにバンク
                   して8方向スプライトが活きる(1面=急降下+蛇行/2面=直進+蛇行/4面=急降下+蛇行…英3面は元々蛇行)。 */
                u8 arch = (rnd() & 1) ? 1 : fighter_arch[curstage];
                f->x = 24 + (rnd() % 200); f->y = -16;
                f->ax = arch;                                  /* 挙動archetype(bh_fighterが解釈) */
                if (arch == 0)      { f->vx = (rnd() & 1) ? 1 : -1; f->vy = 4; }              /* 独 急降下(以後加速) */
                else if (arch == 1) { f->vx = (rnd() & 1) ? 3 : -3; f->vy = 3; }              /* 英 蛇行 */
                else                { f->vx = (rnd() & 1) ? 1 : -1; f->vy = 3 + (rnd() % 2); }/* 米 直進 */
                f->color = fighter_col[curstage]; f->pat = (u8)(SPR_PLANE_L + 4 * 4);  /* 初期=下向き大。bh_fighterが毎フレーム8方向へ */
                f->coltab = cur_ctab;   /* 行別色=陰影 */
                f->shadow = 1;                        /* 海面へ落ち影(旧版に無い新規) */
                if (rnd() & 1) { f->fire = fd_faim; f->ftimer = 20 + (rnd() % 30); }
            }
            sfx(1, SFX_HIT);
        }
        /* 戦艦出現の瞬間: 海コラム帯を艦回避へ切替＋面別BGMへ。★残っている空襲(戦闘機/敵弾)は
           一掃せず自然に飛び去らせる(以前は ent_clear_enemies で瞬間消去=切替が唐突で敵が消えた)。
           新規戦闘機の湧きは spawn ゲート(cam>SC_CAM_SHIP)が既に止めるので、艦の上に突然湧く心配は無い。 */
        if (cam <= SC_CAM_SHIP && !raided) {
            raided = 1; sea_set_ship(curstage);
            page1_use_cart();                /* ★§4-3: bgm_playはdata_read(バンキング)=cartが必要 */
            bgm_play(stage_bgm[curstage]);   /* ★敵艦が見えた=海イントロ共通→面別BGMへ切替 */
            page1_use_ram();                 /* ★戻す(以降のホット区間へ) */
        }
        if (cam <= SC_CAM_SHIP) { phase = 1; camdir = -1; }   /* ★艦出現(=BGM切替)の瞬間から蛇行開始 */
    } else {
        /* 戦艦: 船首↔船尾の往復(今の高速蛇行≈1.6px/f=緊迫感)。艦出現(cam=SHIP>STERN)から入るので、
           下降中は STERN でクランプせず BOW まで一気に見せ、以後 BOW↔STERN を往復する。 */
        vstep = (vcnt < 3) ? 2 : 1;   /* 2,2,2,1,1=1.6px/f(現状の高速を維持) */
        cam = (u16)((s16)cam + (s16)camdir * (s16)vstep);
        if (camdir < 0) { if ((s16)cam <= SC_CAM_BOW)   { cam = SC_CAM_BOW;   camdir = 1;  } }
        else            { if (cam >= SC_CAM_STERN)      { cam = SC_CAM_STERN; camdir = -1; } }
        PROF_CALL(PF_SCROLL, scroll_to(cam));
        /* 蛇行(横揺れ, 速め) */
        if (++wtimer >= 2) {
            wtimer = 0;
            weaveX += wdir;
            if (weaveX >= WMAX)  wdir = -1;
            if (weaveX <= -WMAX) wdir = 1;
        }
        apply_weave();
    }

    /* ★画面揺れ: 被弾/砲台撃破で数フレーム、R#23を縦±2pxジッタ(既存scroll上に上書き)。 */
    if (g_shake) { g_shake--; vdp_set_vscroll((u8)((s16)cam + (s16)(rnd() % 5) - 2)); }

    /* HUD は R#23(縦スクロール)設定直後・エンティティ描画より前に確定させる。
       画面最上部のHUDは最もラスタ競合しやすく、重い ent_draw_all の後に書くと
       ラスタが既に上端を通過→R#23とズレて1px上下振動する(旧版で残っていた不具合)。 */
    hud_draw(g_score, g_lives);

    g_rage = (phase == 1 && ent_live_turrets() <= 1) ? 1 : 0;   /* ★最後の主砲=レイジ(全発砲が速射) */

    /* ★B6(§B6): CE待ちにCPU仕事を重ねる。海コピー(HMMM=VDP実行待ち主体)を"CPUブロック(更新/当たり判定)の
       前"に発行し、VDPが海を流している間にCPUが計算する。HUDは上のまま(後ろへ動かすとラスタ振動が再発)。
       海コピーは表示リングを書くだけでCPUブロックは純計算(競合するVRAM書きが無い)ので安全に重ねられる。
       ★turboRはCPUがZ80比1/5でVDP待ちが支配的になるため重ね合わせが効く。Z80では利得は小さい(=挙動は不変)。
       ★当初は ent_draw_all 直前に明示 vdp_cmd_wait() を置いて海コピー完了を待つ設計にしたが、その
         CE待ちのスピンがラスタ敏感なHUD(最上部)の表示を乱した(digitに横線)。明示待ちは置かず、次に
         VDPコマンドを出す fire_draw の CE待ちに完了を委ねる(SATバーストは直書きで完了待ち不要)。
       SEA13: 海コラムを1strip位相流し=水が艦に対して流れる擬似多重スクロール。
       ★序盤(phase0=海モード)は全幅塗り(最重)なのでSEA0_DIVで間引く。phase1も7.9ms/fと重いので2フレームに1回。 */
    /* ★§4-1 VDP-CPUオーバーラップ: 海を「1コピーずつ」VRAM非接触のCPUステップ
       (recount/update/aa_update/special/collision)の合間に発行する。各コピーはVDPが裏で実行し、
       その間にCPUが回る=次コピー発行時には完了済み(cmd_wait≒0)。海の見た目・枚数・速度は一括版と
       定義上同一(状態機械は sea_begin/sea_step に1本化=H-B無し)。
       aa_collide は撃破時 burn_add(VDPコマンド)を出すので、その前に海を完全ドレイン(cmd_wait)する。
       ★SEASCRL区間は分散のため計測不可(=0表示)。海のVDP待ちは挟んだ区間へ吸収される。 */
    { u8 sea_on = 0;
      if (DBG_ON(1)) {   /* bit1=海アニ停止 */
          if (phase != 0) { if (++seatick & 1) sea_on = 1; }        /* phase1: 2フレームに1回 */
          else if (++seatick >= SEA0_DIV) { seatick = 0; sea_on = 1; }
      }
      if (sea_on) sea_begin();
      if (sea_on) sea_step();  if (DBG_ON(8)) PROF_CALL(PF_COL,    ent_recount_ebul());
      if (sea_on) sea_step();  if (DBG_ON(4)) PROF_CALL(PF_UPDATE, ent_update_all());
      if (sea_on) sea_step();  if (DBG_ON(2)) PROF_CALL(PF_AA,     aa_update());
      if (sea_on) sea_step();  if (DBG_ON(4)) PROF_CALL(PF_UPDATE, special_update());
      if (sea_on) sea_step();  if (DBG_ON(8)) PROF_CALL(PF_COL,    ent_resolve_collisions());
      if (sea_on) { while (sea_step()) { } vdp_cmd_wait(); }   /* ★aa_collide(burn=VDPコマンド)前に海完全完了 */
      if (DBG_ON(2)) PROF_CALL(PF_AA,     aa_collide());
    }
#ifdef DEBUG_PROF
    { PROF_T0(_pd);
#endif
    if (DBG_ON(16)) ent_draw_all();      /* bit16=描画停止 */
#ifdef DEBUG_PROF
    PROF_ADD(PF_DRAW, _pd); }
#endif
    /* ★炎はここ(ent_draw_allの後)で一括。ent_draw_allはSAT/色をVRAM直書きするため、その最中に
       VDPコマンド(炎コピー)を走らせると競合して双方遅くなる(実測でDRAW+1ms悪化→前に出す案は撤回)。
       VDP並列化はVRAM非接触の純CPU(=海interleaveのAI)とだけ行う。 */
    PROF_CALL(PF_FIRE, fire_draw());
    page1_use_cart();   /* ★§4-3: ホット区間終了→page1をカートリッジへ戻す(以降のバンキング=ミス/クリア/setup可) */

    /* 自機撃墜(ミス): 残機を1減らし、残っていれば面最初から全砲台復活でやり直し。
       尽きたら 継続ONでコンティニュー(残機を初期値へ戻して再挑戦=無限) / OFFでタイトルへ。 */
    if (g_miss) {
        g_miss = 0;
        play_death_banked(cam);         /* 沈没演出(bank16) */
        if (g_lives) g_lives--;
        if (g_lives == 0) {
            if (game_over_banked()) { g_lives = lives_init(); stage_setup(); }  /* CONTINUE=同面再開・スコア保持 */
            else return SC_TITLE;
        } else {
            stage_setup();              /* 残機あり: 面最初から全砲台復活 */
        }
        return SCENE_NONE;
    }
    /* 全エンプレ(主砲＋対空砲)撃破でクリア → 撃破演出へ(炎上→撃破!!→スコア→ファンファーレ→エンディング) */
    if (phase == 1 && ent_live_turrets() == 0 && aa_alive() == 0) {
        dmode = 1; dtimer = 150;   /* 約2.5秒の炎上スペクタクル */
        vdp_set_hscroll(0, 0);     /* 蛇行(横HW)を0に=火球のX基準を艦アートへ揃える(旧版準拠) */
        /* ★艦全体を炎に包む(旧版): 全撃破エンプレ27基に加え散布火球18枚を追加登録。fire_draw が2コマ描画。
           撃破時に一度だけ(毎フレーム散布=もっさりの主因は回避)。 */
        { u8 q; for (q = 0; q < 18; q++) {
            u8 rr = rnd(); s16 fx = (s16)(72 + (rnd() & 127));
            burn_add(fx, (u16)((s16)cam + 12 + (rr & 63) + (rnd() & 127)), (u8)(rr >> 6 > 2 ? 2 : rr >> 6)); } }
        bgm_stop();
        return SCENE_NONE;
    }
    return SCENE_NONE;
}
