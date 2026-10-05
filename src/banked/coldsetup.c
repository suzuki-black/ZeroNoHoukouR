/* coldsetup.c — 面の準備でしか使わない配置処理(bank30)。常駐リクレイムのため scene_stage.c から移設。
   ★中身は RAM(エンティティ・対空砲の表)を書くだけで、VRAM もバンク窓も触らない=バンクで動かせる。
     呼ぶのは stage_build から1回だけ(bcall_to(COLDSETUP_BANK))。
   ★面別の表(主砲の発砲スクリプト・耐久・対空砲の耐久)も一緒に持ってきた(常駐の節約)。 */
#include "types.h"
#include "entity.h"
#include "fire.h"        /* FIRE_* */
#include "gamestate.h"
#include "scroll.h"      /* SC_SHIP_R0 */
#include "ship.h"        /* SHIP_NAAG / g_shipargs */
#include "sprites.h"     /* SPR_BARREL0 / SPR_HELLCAT / barrel_col */
#include "aa_hot.h"      /* aa_fire / aa_hp / aa_dead */
#include "assets_data.h" /* STAGE_COUNT */
#include "final.h"       /* STAGE_FINAL */
#include "opll.h"        /* ★FM の検出(起動時1回。g_cold_mode=COLD_OPLL で呼ばれる) */
#include "msx.h"         /* MSX_VER(0x002D: 0=MSX1 / 1=MSX2 / 2=MSX2+ / 3=turboR) */
#include "pcm.h"         /* ★PCM が使えるかの検出(起動時1回)＋スネアの素材を RAM へ写す */
#ifndef DEBUG_PROF
#include "snare_data.h"  /* 自動生成(tools/gen_snare.py)。256B の波形 */
#endif

/* ★常駐(scene_stage.c)が持っているもの。面の準備で先に埋まっている */
extern u8  cur_gun_x[4];   /* 主砲4基の艦内x */
extern u16 cur_gun_y[4];   /* 同 y */
extern u8  cur_ctab[16];   /* この面の機体の行別色(停泊機の色) */

/* ★主砲の発砲スクリプトは**常駐**(scene_stage.c)に置く。ここ(バンク)に置くと砲台が持つポインタが
   バンクの窓(0xA000〜)を指し、窓が既定へ戻った瞬間に中身が変わって撃たなくなる(実機で「2面で全く撃ってこない」と指摘)。 */
extern const u8 *const fd_gun_stage[STAGE_COUNT];

/* 破壊物の耐久(半分単位。通常の自機弾は1発=2)。面が進むほど硬い。
   ★2面(空母)の主砲は小さな5インチ砲で、左前の砲は甲板の停泊機の列の奥にある(弾は停泊機に当たると消える)ので
     他より軟らかくした(48 では「やたら硬い」と実機で指摘)。 */
static const u8 gun_hp[STAGE_COUNT]    = { 40, 36, 56, 64, 80 };   /* 主砲4基(通常弾で20/18/28/32/40発) */
static const u8 aa_big_hp[STAGE_COUNT] = { 14, 16, 18, 22, 26 };   /* 大型対空砲14基 */
static const u8 aa_sml_hp[STAGE_COUNT] = {  8,  9, 10, 12, 14 };   /* 小型対空砲9基 */

static void spawn_turret(u8 shipX, u16 shipY, u8 delay) {
    Entity *e = ent_spawn(ET_TURRET);
    if (e) {
        g_lturret++;   /* ★生存砲台O(1)カウンタ(stage_buildで0初期化済み。当たり判定の撃破で--) */
        e->ax = (s16)shipX - 8;                /* 砲塔中心x→スプライト左上(中心x=shipX) */
        e->ay = (s16)(SC_SHIP_R0 * 16 + shipY) - 8;   /* 砲身スプライト(旋回中心=8,8)をドーム中心に合わせる */
        e->hp = gun_hp[curstage]; e->fire = fd_gun_stage[curstage]; e->ftimer = delay;
        e->vx = 4; e->vy = 0; e->h = 0;        /* 砲身の向き=下 / 旋回冷却 / 命中フラッシュ残 */
        e->pat = (u8)(SPR_BARREL0 + 4 * 4);    /* 可動砲身(下向き, BGドームに重なる) */
        e->coltab = barrel_col;                /* 金属シェード(行別カラー) */
    }
}

/* 空母(2面)の停泊F6F(甲板8機)。エンティティ(ET_PARKED)で艦上に静止=破壊/発艦できる。 */
static void spawn_parked(u8 shipX, u16 shipY) {
    Entity *e = ent_spawn(ET_PARKED);
    if (e) {
        e->ax = (s16)shipX - 8;                /* 艦上世界アンカー(中心=shipX,ship座標y) */
        e->ay = (s16)(SC_SHIP_R0 * 16 + shipY) - 8;
        e->pat = SPR_HELLCAT; e->coltab = cur_ctab; e->shadow = 1;
    }
}


/* ───────── FM(MSX-MUSIC)の検出。起動時に1回だけ(g_cold_mode=COLD_OPLL) ─────────
   ★手順と「やってはいけないこと」は opll.h に書いた。要点だけ再掲:
     ・**内蔵(APRLOPLL)を先に**探す。見つかったら 0x7FF6 には**触らない**
       (触ると一部の MSX2+ で壊れる。外付けだけを見ると内蔵機で鳴らない)。
     ・スロットは BIOS の RDSLT で読む(自分のページを差し替えないので安全)。 */
static u8  sl_slot;
static u16 sl_addr;
static u8  sl_val;

static void sl_read(void) __naked {
    __asm
        ld   a, (_sl_slot)
        ld   hl, (_sl_addr)
        push ix
        push iy
        call 0x000C            ; RDSLT (A=slotID, HL=番地 → A=値)
        pop  iy
        pop  ix
        ld   (_sl_val), a
        ret
    __endasm;
}
static void sl_write(void) __naked {
    __asm
        ld   a, (_sl_val)
        ld   e, a
        ld   a, (_sl_slot)
        ld   hl, (_sl_addr)
        push ix
        push iy
        call 0x0014            ; WRSLT (A=slotID, HL=番地, E=値)
        pop  iy
        pop  ix
        ret
    __endasm;
}
static u8 rd(u8 slot, u16 addr) { sl_slot = slot; sl_addr = addr; sl_read(); return sl_val; }

/* ページ1(0x4018)に 8 バイトの印があるスロットを探す。無ければ 0xFF。 */
static u8 find_sig(const u8 *sig) {
    u8 ps, ss, nss, slot, i;
    for (ps = 0; ps < 4; ps++) {
        nss = (u8)((*(volatile u8 *)(0xFCC1 + ps) & 0x80) ? 4 : 1);   /* EXPTBL: bit7=拡張スロット */
        for (ss = 0; ss < nss; ss++) {
            slot = (u8)((nss > 1) ? (0x80 | (ss << 2) | ps) : ps);
            for (i = 0; i < 8; i++)
                if (rd(slot, (u16)(0x4018 + i)) != sig[i]) break;
            if (i == 8) return slot;
        }
    }
    return 0xFF;
}

/* ───────── turboR 未満お断り(起動時に1回) ─────────
   ★本作は**turboR 専用**。R800 を前提に組んであり、MSX2+ 以下では「動くには動くが
     まともに遊べない」状態になる(フレーム落ち・ホットコードの RAM 実行が効かない 等)。
     openMSX が turboR を実機 BIOS ごと動かせるようになった今、中途半端に起動させるより
     **はっきり断る**方が親切なので、起動時に機種を見て止める(2026-10-03 ユーザー指定)。
   ★機種は**主 BIOS ROM の 0x002D**(0=MSX1 / 1=MSX2 / 2=MSX2+ / 3=turboR)。page0 は起動時から
     主 BIOS なので、そのまま読んでよい(sys.c の R800 ブーストも同じ値を見ている)。
   ★ここは**冷たいバンク**に置く。常駐は残り 100B 程度しか無く、起動時に1回しか使わない
     ものを常駐へ置く余裕が無い。FM の検出(COLD_OPLL)と同じ経路に相乗りするので、
     **常駐のコードは 1 バイトも増えない**。
   ★表示は BIOS の SCREEN0(CHGMOD=0x005F)＋CHPUT(0x00A2)。VDP を自前で初期化する前の段階でも
     確実に文字が出せる。戻らない(HALT ループ)。 */
#define BIOS_CHGMOD 0x005F
#define BIOS_CHPUT  0x00A2
static u8 putc_ch;
static void bios_putc(void) __naked {
    __asm
        push ix
        push iy
        ld   a, (_putc_ch)
        call BIOS_CHPUT
        pop  iy
        pop  ix
        ret
    __endasm;
}
static void bios_screen0(void) __naked {
    __asm
        push ix
        push iy
        xor  a
        call BIOS_CHGMOD
        pop  iy
        pop  ix
        ret
    __endasm;
}
static void bios_puts(const char *s) {
    while (*s) { putc_ch = (u8)*s++; bios_putc(); }
}
/* ★2 行目は半角カナ(MSX の ANK フォント)。日本語機以外では別の字が出るので、
   意味は 1〜3 行目の英語だけで通るようにしてある。 */
static const char msg_l1[] = "\r\n\r\n  *** MSX turboR REQUIRED ***\r\n\r\n";
static const char msg_l2[] = "  THIS CARTRIDGE RUNS ONLY ON\r\n  AN MSX turboR (FS-A1ST/A1GT).\r\n\r\n";
static const char msg_l3[] = "  \xC0\xB0\xCE\xDER \xB2\xBC\xDE\xAE\xB3\xC3\xDE \xB7\xC4\xDE\xB3\xBC\xC3 \xB8\xC0\xDE\xBB\xB2\r\n";

static void require_turbor(void) {
    if (MSX_VER >= 3) return;                  /* turboR なら何もしない */
    bios_screen0();
    bios_puts(msg_l1);
    bios_puts(msg_l2);
    bios_puts(msg_l3);
    for (;;) { __asm halt __endasm; }          /* 戻らない */
}

/* ───────── PCM が使えるかを見る(起動時に1回) ─────────
   ★PCM の送出はシステムタイマ(E6h/E7h、1 カウント 3.911us)で「次に出す時刻」を測る。
     タイマが進まない環境だと**その時刻が永遠に来ない**。鳴り終わりを待つ所があると戻らなくなる。
     そこで起動時に**本当に進むか**を確かめ、進まなければ PCM を丸ごと無効にする。
     無効なら送出口は「鳴っていない」で素通りするので、挙動は PCM を入れる前と変わらない。
   ★★ここは**割込みが止まっている**。バンク呼び出しのトランポリン(_bcall)が di〜ei で囲っており、
     冷たいバンクの中身は丸ごとその中で走る。したがって**割込みで進むもの(JIFFY 0xFC9E 等)を
     待ってはいけない**。最初そう書いて、起動直後に戻らなくなった(BIOS が張った青い画面のまま停止)。
     タイマ自身を見れば割込みは要らない。
   ★**上限を必ず付ける**。タイマが死んでいる機械では「変わるまで待つ」は無限ループになる。
     20000 回で諦める(最悪およそ 0.1 秒。PCM が無い機械で起動時に 1 回だけ)。
   ★ここも**冷たいバンク**。起動時に1回しか使わないものを常駐へ置かない。 */
#define PCM_DETECT_TRIES 20000      /* タイマが動かない機械で諦めるまでの回数 */
__sfr __at(0xE6) PCM_TMR_LO;

static void pcm_detect(void) {
    u8  t = PCM_TMR_LO;                         /* 下位バイトが一番速く動く */
    u16 n;
    g_pcm_hw = 0;
    for (n = 0; n < PCM_DETECT_TRIES; n++) {
        if (PCM_TMR_LO != t) { g_pcm_hw = 1; break; }   /* 動いた=PCM が使える */
    }
#ifdef DEBUG_PROF
    /* ★計測用ビルドでは 0xEB00 が区間別 tick の蓄積(g_prof_acc)に使われている。素材を置く場所が
       無いので PCM ごと切る。フレーム時間を測るためのビルドなので実害は無い。 */
    g_pcm_hw = 0;
#else
    /* スネアの素材を RAM(0xEB00)へ写す。★**ここは冷たいバンクなので const はこのバンク自身に居る**
       = 窓を差し替えずに読める。data_read を使ってはいけない(このコード自身の窓が消える)。 */
    {
        u16 i;
        for (i = 0; i < SNARE_DATA_LEN; i++) pcm_snare_buf[i] = snare_data[i];
    }
#endif
    g_pcm = (u8)(g_pcm_hw && g_pcm);            /* ★設定が OFF のまま再起動した場合も OFF のまま */
}

static void opll_detect(void) {
    static const u8 sig_int[8] = { 'A','P','R','L','O','P','L','L' };   /* 内蔵 MSX-MUSIC */
    static const u8 sig_pac[8] = { 'P','A','C','2','O','P','L','L' };   /* 外付け FM-PAC  */
    u8 slot, i;

    g_opll = OPLL_NONE;
    slot = find_sig(sig_int);                      /* ★内蔵を先に */
    if (slot != 0xFF) { g_opll = OPLL_INT; }
    else {
        slot = find_sig(sig_pac);
        if (slot == 0xFF) { g_opll_hw = OPLL_NONE; return; }   /* FM は無い。以降 opll_w は何もしない */
        g_opll = OPLL_PAC;
        sl_slot = slot; sl_addr = 0x7FF6; sl_read();   /* FM-PAC だけ I/O を有効にする */
        sl_val = (u8)(sl_val | 1); sl_write();
    }
    for (i = 0; i <= 0x38; i++) opll_w(i, 0);      /* 全レジスタ 0 ＝ 黙らせる */
    g_opll_hw = g_opll;                           /* ★ハードの検出結果を控える(設定で戻すため) */
    if (!g_fm) g_opll = OPLL_NONE;                /* 設定が OFF のまま再起動した場合 */
#ifdef OPLLTEST
    /* ★検証用(make clean && make OPLLTEST=1): 起動直後に和音を鳴らしっぱなしにする。
       録音して 440/660/880Hz が出ていれば「検出 → I/O 書込み → 発音」の経路が通っている
       (2026-10-02 に openMSX＋実機BIOS機で確認: 440=875 / 660=910 / 880=923、他は無し)。 */
    opll_w(0x30, 0x50); opll_w(0x10, 0x22); opll_w(0x20, 0x19);   /* 根音 */
    opll_w(0x31, 0x50); opll_w(0x11, 0xB3); opll_w(0x21, 0x19);   /* 5度  */
    opll_w(0x32, 0x50); opll_w(0x12, 0x22); opll_w(0x22, 0x1B);   /* 8度上 */
#endif
}

void banked_entry(void) {
    u8 i;
    if (g_cold_mode == COLD_OPLL) { require_turbor(); opll_detect(); pcm_detect(); return; }   /* ★turboR 未満はここで止まる */
    for (i = 0; i < SHIP_NAAG; i++) {          /* 対空砲の発射タイマと耐久 */
        u16 t = (u16)60 + (u16)i * 11;         /* ★u16で計算し255クランプ(u8のままだと高iで桁溢れ) */
        aa_fire[i] = (t > 255) ? 255 : (u8)t;
        aa_hp[i]   = (i < 14) ? aa_big_hp[curstage] : aa_sml_hp[curstage];
        aa_dead[i] = 0;
    }
    if (curstage == STAGE_FINAL) return;       /* 最終面に艦は無い(ボス/弱点はオーバレイ側) */
    spawn_turret(cur_gun_x[0], cur_gun_y[0], 30);
    spawn_turret(cur_gun_x[1], cur_gun_y[1], 45);
    spawn_turret(cur_gun_x[2], cur_gun_y[2], 60);
    spawn_turret(cur_gun_x[3], cur_gun_y[3], 75);
    if (curstage == 1) {                       /* 空母: 停泊F6F 8機を甲板へ(中央列x120×4＋左列x98×4) */
        for (i = 0; i < 4; i++) spawn_parked(120, (u16)(130 + i * 40));
        for (i = 0; i < 4; i++) spawn_parked(98,  (u16)(150 + i * 40));
    }
}
