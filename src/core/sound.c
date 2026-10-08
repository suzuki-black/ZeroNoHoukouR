/* sound.c — PSG効果音＋BGMドライバ＋60Hz割込み(常駐。毎フレーム raster.c の割込み処理から呼ばれる)。
   前作 BattleshipProto(実機確定)の sfx エンジン/ISR イディオムを移植・整理し、BGMを追加。
   BGM: melody=tone A(SFX SHOTと共有・SFX優先), bass=tone B。曲データはバンク8→RAMコピー。 */
#include "sound.h"
#include "pcm.h"   /* スネアの拍に内蔵 PCM を重ねる */
#include "bank.h"       /* data_read(曲データをバンク→RAM) */
#include "vdp.h"        /* vdp_wait_frame(ファンファーレの前景同期) */
#define ASSETS_BGM          /* ★BGM の表だけを取り込む(艦などの表の複製を作らない) */
#include "opll.h"          /* ★FM(MSX-MUSIC)を添えて音を厚くする */
#include "assets_data.h"   /* 自動生成: bgm_notetp[48] / bgm_off[] / bgm_len[] / BGM_BANK / BGM_RAM_MAX */

/* ---- PSG ポートI/O(規約非依存にファイルスコープ変数経由) ----
   0xA0=レジスタ選択, 0xA1=データ。PSGはVDPと独立ポートなので di 不要。 */
static u8 g_pr, g_pv;
static void psg(u8 r, u8 v) {
    g_pr = r; g_pv = v;
    __asm
        ld   a, (_g_pr)
        out  (0xA0), a
        ld   a, (_g_pv)
        out  (0xA1), a
    __endasm;
}

/* ---- SFX 状態 ---- */
#define SFX_THUNDER_DUR 42   /* 60Hz ISR で 42 フレーム=0.7秒。クラッシュの停止(24フレーム@30fps=48 ISRフレーム)に収まる長さ */
#define SFX_RUMBLE_DUR  56   /* 60Hz ISR で 56 フレーム≒0.93秒。津波の助走に合わせて打ち直す */
#define SFX_LOOP_DUR    46   /* 60Hz で 46 フレーム≒0.77秒=宙返り(30fps で 16 コマ=32 ISR フレーム)＋抜けていく余韻 */
static const u8 sfxDur[SFX_COUNT] = { 0, 8, 16, SFX_LOOP_DUR, 4, 28, 6, SFX_THUNDER_DUR, SFX_RUMBLE_DUR };
/* ★tone B の3種(SHOT/PHIT/LOOP)は同じ形: 周期 = 始め + 経過フレーム×傾き、音量 = 残り2フレームまで一定→ぷつっと落とす。
   経過フレーム = 持続長-1-残り(SHOT は 7-rem、PHIT は 15-rem で従来と同じ値)。
   LOOP: 周期 500(約224Hz)→ 230(約486Hz)=唸りが引き起こしで1オクターブ上ずる。音量は 15 と 10 を 2 フレームずつ交互=エンジンの
   「ぶるる」という震え。★最初は 900→438(124Hz〜)・音量11 だったが、実機で全く聞き取れなかった(本体スピーカーは低音が出ず、BGM に埋もれる)。 */
static const u16 sfxB0[3] = { 40, 120, 500 };
static const s8  sfxBs[3] = { 62, 30, -6 };
static const u8  sfxBv[3] = { 13, 12, 15 };
static const u8  sfxBk[3] = { 6, 5, 5 };
                              /* NONE/SHOT/HIT/BOOM/PHIT/EFIRE/THUNDER/RUMBLE */
static u8 sfxType[SND_CH];
static u8 sfxTimer[SND_CH];

u8 g_rumble_lv;   /* SFX_RUMBLE の音量(0..15)。演出側が波の進行に合わせて上げていく */

volatile u16 snd_ticks;
u16 g_bgm_t0;   /* ★最後に bgm_play した瞬間の snd_ticks。最終面の登場演出を曲のイントロに合わせる */
volatile u8  snd_active;
static u8 sfx_busy_b;   /* このフレーム tone B を SFX(SHOT/PHIT)が使用中 → BGM bass は譲る(★メロディ=chAは死守) */
static u8 sfx_busy_c;   /* このフレーム noise C を SFX(HIT/BOOM/EFIRE)が使用中 → BGM drum は譲る */

/* 効果音トリガ。type/timer の2バイト更新中に ISR が割り込むと中途半端を読むので di/ei 原子化。
   破壊音(BOOM)再生中は命中音(HIT)で上書きしない(鳴り切らせる)。 */
void sfx(u8 ch, u8 type) {
    __asm di __endasm;
    if (!(type == SFX_HIT && sfxType[ch] == SFX_BOOM && sfxTimer[ch] != 0)) {
        sfxType[ch] = type;
        sfxTimer[ch] = sfxDur[type];
    }
    __asm ei __endasm;
}

/* 毎フレーム更新(ISRから呼ぶ=外部リンケージ)。各chの残り時間に応じ周波数/音量を更新(簡易エンベロープ)。 */
void sfx_update(void) {
    u8 ch, cnt = 0, bb = 0, bc = 0;
    for (ch = 0; ch < SND_CH; ch++) {
        u8 t, rem;
        if (sfxTimer[ch] == 0) continue;
        sfxTimer[ch]--;
        t = sfxType[ch];
        rem = sfxTimer[ch];
        if (t <= SFX_LOOP) {                       /* tone B の3種(★メロディ=A死守。ベースが譲る) */
            u8 i = (u8)(t - 1);
            u16 p = (u16)(sfxB0[i] + (s16)(u8)(sfxDur[t] - 1 - rem) * sfxBs[i]);
            bb = 1;
            psg(2, p & 0xFF); psg(3, (p >> 8) & 0x0F);   /* ★chB tone period */
            psg(9, (rem < 2) ? (u8)(rem * sfxBk[i]) : (t == SFX_LOOP && (rem & 2)) ? 10 : sfxBv[i]);   /* ★chB volume(宙返りは震わせる) */
        } else {
            bc = 1;                                /* 残り(HIT/BOOM/EFIRE/THUNDER/RUMBLE)は noise C を占有 */
        }
        if (t == SFX_HIT) {                        /* 短いノイズ "コッ" */
            psg(6, 15); psg(10, rem * 3);
        } else if (t == SFX_EFIRE) {               /* 敵発砲: 静かな短いノイズ "プッ" */
            psg(6, 12); psg(10, (u8)(rem * 2));    /* 低音量(自機弾より静か) */
        } else if (t == SFX_THUNDER) {             /* ★雷鳴: 鋭い炸裂→深い轟き。うねりながら長く減衰 */
            u8 el = (u8)(SFX_THUNDER_DUR - rem);   /* 経過フレーム */
            u8 np = (el < 5) ? (u8)(1 + el * 2) : (u8)(11 + (el >> 1));   /* noise周期 鋭(1)→深(31) */
            if (np > 31) np = 31;
            psg(6, np);
            { u8 v;
              if (el < 3)      v = 15;                       /* 立ち上がりの炸裂 */
              else             v = (u8)((rem * 15) / SFX_THUNDER_DUR);   /* 以後は直線減衰 */
              if (el >= 6 && (el & 4)) { if (v < 15) v++; }  /* 轟きのうねり(一定に減らさない) */
              psg(10, v); }
        } else if (t == SFX_RUMBLE) {              /* ★地鳴り: 最も深いノイズ。音量は呼び側が決める */
            u8 el = (u8)(SFX_RUMBLE_DUR - rem);    /* 経過フレーム */
            psg(6, (u8)(28 + (el & 3)));           /* noise周期を最深付近で微妙に揺らす=地面が鳴る感じ */
            /* ★音量を持続長から作ると、鳴らし続けるために打ち直すたびに音量が振り出しへ戻り、
               「ゴゴゴゴ」が育たずブツ切れになる(実測でそうなった)。演出の進行(波の距離)を
               そのまま音量にしたいので、**呼び側が g_rumble_lv に入れる**。 */
            psg(10, (g_rumble_lv > 15) ? 15 : g_rumble_lv);
        } else if (t == SFX_BOOM) {                /* 長い "ズガーン" */
            u8 np = (u8)(3 + (27 - rem));          /* noise周期 3(鋭)→30(深) */
            if (np > 31) np = 31;
            psg(6, np);
            psg(10, (rem >= 20) ? 15 : (u8)((rem * 15) / 20));
        }
        if (sfxTimer[ch] == 0) psg((u8)(8 + ch), 0);   /* 終了で該当chを無音 */
        else cnt++;
    }
    snd_active = cnt;
    sfx_busy_b = bb;
    sfx_busy_c = bc;
}

/* ---- BGM(データバンクの曲データを ISR で再生。旧cportのドライバを移植) ----
   曲データ(RAMコピー)= [nMel,nBas,basStep, melPeak,melSus,melVib, basPeak,basSus, drumOn, sweep,
                          melLoop, basLoop, melNote(nMel), melLen(nMel), basNote(nBas)]。
   melody=tone A(可変長), bass=tone B(固定basStep), drum=noise C(標準マーチ)。各パート独立ループ。
   ★melLoop/basLoop: 末尾まで行ったら**そこへ戻る**(0=先頭)。最終面の「イントロ1回→本編だけループ」。
     ループ位置が付いた曲はドラムを本編(ベースがループ位置に来た瞬間)から鳴らす。
   エンベロープ: 発音開始 peak → 毎フレーム-1 → sustain、末尾2フレーム無音。vib で伸ばし音に揺れ。 */
#define BGM_REST 255
static const s8 bgm_vibt[8]   = { 0, 1, 2, 1, 0, -1, -2, -1 };
static const u8 bgm_drmNP[4]  = { 0, 20, 7, 3 };   /* noise周期(全style共通) 1=キック/2=スネア/3=ハット */
static const u8 bgm_drmDec[4] = { 0, 3, 2, 3 };    /* 毎フレーム減衰(全style共通) */
/* ★面別ドラム: スタイル別の [pat16, v0(4), tempo] はデータバンク(drum_off)に置き、bgm_play で当該21BをRAMへ。 */
static u8 drm_blk[DRUM_STYLE_BYTES];   /* [0..15]=pattern / [16..19]=v0 / [20]=tempo */

/* ★曲データの RAM は固定番地 0xE100(開始カード画像 g_card_ram と共用)。常駐 _DATA は 0xE000 の天井まで
   残り数十バイトしか無く、最終面の長い曲が入らなかった。共用してよい根拠: カードを描くのは
   draw_stage_card の中だけで、そこは bgm_stop 済み(ISR は曲データを読まない)。カードの後は必ず
   bgm_play で読み直す(stage_intro)。bgm_resume はステージ中のメガクラッシュ明けだけで、その間に
   カードは描かれない。 */
#define BGM_RAM_ADDR 0xE100
static u8 __at(BGM_RAM_ADDR) bgm_ram[1536];
/* ★曲ヘッダの長さ。12B の素データ ＋ FM の曲ごとの設定 11B。生成は tools/gen_assets.mjs。 */
#define BGM_HDR 23
/* ★FM の設定は**変数へ写さず bgm_ram から直に読む**。bgm_ram は固定番地(0xE100)なので
   `bgm_ram[12]` は静的変数の読み出しと同じ 1 命令になり、**常駐が増えない**。
   値はそのまま OPLL のレジスタへ書ける形で入っている(常駐で組み立てない)。 */
#define FM_PAD     bgm_ram[12]      /* (音色<<4)|音量 → R#30,31,32(和音の3ch) */
#define FM_MEL     bgm_ram[13]      /* 同 → R#33(主旋律の重ね) */
#define FM_R36     bgm_ram[14]      /* BD の音量 */
#define FM_R37     bgm_ram[15]      /* 上位=HH / 下位=SD */
#define FM_R38     bgm_ram[16]      /* 上位=TOM / 下位=TC */
#define FM_RHY(t)  bgm_ram[16 + (t)]   /* ドラム種別 1..3 で立てる R#0E のビット */
#define FM_CHD(i)  bgm_ram[20 + (i)]   /* 和音の音程(ベース音からの半音)。i=0..2 */
static u8 *mel_n, *mel_l, *bas_n;
static u8  nMel, nBas, basStep, melPeak, melSus, melVib, basPeak, basSus, drumOn;
static u8  melLoop, basLoop, drmGate;
static u8  bassSweep;   /* 1=ベース(chB)をロックマン風シンセドラムに(立上り1oct上→基音へ急降下＋速い減衰) */
static u8  mIdx, mTrem, mCl;    /* melody: index / 残フレーム / 発音長 */
static u8  bIdx, bTrem, bCl;    /* bass */
static u8  drmIdx, drmT, drmType, drmVol;
static u8  bgmOn;
static u8  bgmLoaded;   /* 1=bgm_ram に曲が載っている(bgm_resume の安全弁) */

/* ───────── FM(OPLL)のリズム音源を PSG のドラムに重ねる ─────────
   ★PSG のノイズでやっている打楽器と**同じ拍で**、OPLL のリズム音源も叩く。曲データは一切
     増えない(いまの拍をそのまま使う)のに、音は目に見えて厚くなる。
   ★OPLL のリズムは ch6,7,8 を占有する。音程の無い打楽器なので、残り 6ch は後で
     ベース/パッド/主旋律の重ねに使える。
   ★キーオンは「落としてから立てる」(0x0E を同じフレームで 2 回書く)。立てっぱなしだと鳴らない。
   ★音量は 0 が最大・15 が無音。PSG を食わないよう控えめから始める。 */
#define OPLL_RHY_REG  0x0E          /* bit5=リズムモード / bit4=BD / bit3=SD / bit2=TOM / bit1=TC / bit0=HH */
#define OPLL_RHY_ON   0x20          /* リズムモードだけ立てた状態(全部 off) */
/* ★どの打楽器を鳴らすかは**曲ごと**(FM_RHY)。既定は 1=キック→BD / 2=スネア→SD /
   3=ハット→HH＋シンバル(TC)。
   ★ハットだけだと最大音量にしても埋もれた(帯域比 1.03→1.54 倍止まり)。OPLL の HH は
     短くて細いので、明るく伸びる TC を重ねて抜けを作っている。
   ★重い曲は キック に TOM を重ねて胴を足せる(最終面・中ボス)。TOM を鳴らすなら
     R#38 の上位(TOM の音量)を 0xF=無音 から開けること。 */
static u8 fmDrum;                   /* 1=この曲は FM を重ねる(ドラム＋和音) */

/* ───────── FM の和音(ベースの下支え) ─────────
   ★既存の**ベース譜**が音を変えた瞬間に、OPLL の ch0/1/2 へ
     「根音・5度・オクターブ上」を置く。**曲データは増えない**(ベース譜から作るだけ)。
   ★3度を入れないので長調/短調のどちらにも当たらない＝どの曲へ回しても和声が壊れない。
   ★リズムは ch6,7,8 を使うので、和音は ch0〜2 に置く(主旋律の重ねは ch3〜5 が空く)。
   ★音色と音量は**曲ごと**(FM_PAD)。既定はオルガン(8)を最大で、伸びるので「パッド」になり
     PSG の輪郭を消さない。静かな曲(エンディング・海イントロ)は落としてある。 */
static const u16 opll_fnum[12] = { 172,183,194,205,217,230,244,258,274,290,307,326 };
                                    /* C..B。block=2 で C2(=音符 0)。誤差は最大 0.25% */
static u8 fmPadIdx;                 /* 直前に鳴らしたベース音符の添字(変化を見るだけ) */
/* ★和音の書き込みは**ISR でやらない**。9 レジスタ＝実測で約 1ms かかり、VBLANK に寄せた
   SAT/色表の転送を押し出す(走査線 88 → 105 行目まで伸びた)。ISR は「次の和音」を置くだけにして、
   本体のループが VBLANK の仕事を**終えてから** fm_flush() で流す。
   ★伸ばす和音なので 1 フレーム遅れても聞こえない(ドラムは拍が命なので ISR のまま)。 */
static u8 fmPadNote = 0xFF;         /* 次に置く和音の根音(0xFF=用事なし) */

/* ───────── 主旋律の重ね(ch3) ─────────
   ★PSG の主旋律が音を変えた瞬間に、**1 オクターブ下**で同じ音を ch3 へ置く。
     同じ高さに重ねると PSG の輪郭が消えるので、下に敷いて太さだけ足す。
   ★音色と音量は**曲ごと**(FM_MEL)。既定はホルン(9)で、立ち上がりが柔らかく伸びるので PSG の角と
     喧嘩しない。トランペット(7)のような鋭い音は二重に聞こえて濁るが、押したい曲
     (5面のクライマックス・最終面・警報)では逆にそれが要るので曲側で選ぶ。
   ★2 本重ねない。ch4/ch5 は空けておく(将来の余地)。 */
#define OPLL_MEL_CH   3
static u8 fmMelIdx;                 /* 直前に鳴らした主旋律の音符の添字 */
static u8 fmMelNote = 0xFF;         /* 次に置く音(0xFF=用事なし / 0xFE=切る) */

static void opll_mel_off(void) { opll_w((u8)(0x20 + OPLL_MEL_CH), 0); }

/* ch へ音符 nn(0..47)を打ち直す。キーオフ → fnum → サステイン＋キーオン。
   ★和音(ch0-2)と主旋律の重ね(ch3)で中身が同じだったのを 1 本にした。12 での割り算・剰余は
     SDCC だとランタイム呼び出しになるので、2 か所に書くと**そのぶん丸ごと重複**する。 */
static void opll_key(u8 ch, u8 nn) {
    u16 f   = opll_fnum[nn % 12];
    u8  blk = (u8)(2 + nn / 12);
    opll_w((u8)(0x20 + ch), 0);                       /* いったんキーオフ(打ち直し) */
    opll_w((u8)(0x10 + ch), (u8)(f & 0xFF));
    opll_w((u8)(0x20 + ch), (u8)(0x30 | (blk << 1) | (u8)(f >> 8)));
}

static void opll_mel(u8 note) {
    if (note >= 48) { opll_mel_off(); return; }       /* 休符は切る */
    if (note >= 12) note = (u8)(note - 12);           /* ★1 オクターブ下へ(下げられる時だけ) */
    opll_key(OPLL_MEL_CH, note);
}

static void opll_chord_off(void) {
    u8 i;
    for (i = 0; i < 3; i++) opll_w((u8)(0x20 + i), 0);
}

static void opll_chord(u8 note) {
    u8 i;
    if (note >= 48) { opll_chord_off(); return; }      /* 休符(BGM_REST=255)は切る */
    for (i = 0; i < 3; i++) {
        u8 nn = (u8)(note + FM_CHD(i));
        if (nn >= 48) nn = (u8)(nn - 12);              /* 上がり過ぎたら 1 オクターブ下げる */
        opll_key(i, nn);
    }
}

/* ★本体のループが VBLANK の仕事を終えてから呼ぶ(scene.c)。予約された和音を実際に書く。 */
void fm_flush(void) {
    u8 n = fmPadNote;
    if (n != 0xFF) { fmPadNote = 0xFF; opll_chord(n); }
    n = fmMelNote;
    if (n != 0xFF) { fmMelNote = 0xFF; opll_mel(n); }
}

static void opll_pad_init(void) {
    u8 i;
    for (i = 0; i < 3; i++) opll_w((u8)(0x30 + i), FM_PAD);
    opll_w((u8)(0x30 + OPLL_MEL_CH), FM_MEL);
    fmPadIdx = 0xFF; fmPadNote = 0xFF; fmMelIdx = 0xFF; fmMelNote = 0xFF;
}

/* ★**全曲に付ける**(2026-10-03。1面の曲で実機の感触を確かめたうえでユーザー指定)。
   ★範囲外の track は bgm_play が曲を読まずに戻るので、ここで弾いて FM を仕込まない
     (仕込むと曲が無いのにリズムモードと音色だけ残る)。
   ★ドラムの無い曲(drumOn=0: エンディング・海イントロ・警報)は bgm_drum が呼ばれないので
     リズム音源は鳴らない。和音(ベース譜)と主旋律の重ねだけが乗る。 */
#define FM_DRUM_TRACK(t) ((t) < BGM_TRACK_COUNT)

static void opll_rhythm_init(void) {
    if (!g_opll) return;
    /* リズム音源が使う ch6/7/8 の音程は固定値(YM2413 の作法どおり) */
    opll_w(0x16, 0x20); opll_w(0x26, 0x05);   /* BD    */
    opll_w(0x17, 0x50); opll_w(0x27, 0x05);   /* HH/SD */
    opll_w(0x18, 0xC0); opll_w(0x28, 0x01);   /* TOM/TC */
    opll_w(0x36, FM_R36);                      /* BD の音量(0=最大) */
    opll_w(0x37, FM_R37);                      /* 上位=HH(0=最大) / 下位=SD。★HH は 4 では
                                                  まったく聞こえなかった(帯域比 1.03 倍=実質ゼロ) */
    opll_w(0x38, FM_R38);                      /* 上位=TOM / 下位=TC(0=最大。ハットに重ねる) */
    opll_w(OPLL_RHY_REG, OPLL_RHY_ON);         /* リズムモード on、全部 off */
    opll_pad_init();                           /* 和音(ch0-2)の音色と音量も仕込む */
}
/* ★FM を**完全に**止める。鳴らすのをやめる所からは必ずこれを通すこと。
   ★保留中の予約(fmPadNote/fmMelNote)も捨てる。捨てないと、止めた後に本体のループの
     fm_flush() が流し込んで**また鳴り出す**(「FM が鳴りっぱなし」の正体。実機で指摘)。
   ★bgmOn=0 にするだけの経路(ファンファーレ・沈没音・初期化)は PSG しか止めていなかったので、
     そこからも呼ぶ。 */
void fm_silence(void) {
    if (!g_opll) return;
    opll_w(OPLL_RHY_REG, 0);                   /* リズムモードごと落とす */
    opll_chord_off();                          /* 和音も切る */
    opll_mel_off();                            /* 主旋律の重ねも切る */
    fmPadNote = 0xFF; fmMelNote = 0xFF;        /* ★予約を捨てる(止めた後に流れ込ませない) */
    fmPadIdx  = 0xFF; fmMelIdx  = 0xFF;        /* 次に鳴らすときは必ず置き直す */
}

void bgm_play(u8 track) {
    u8 *p;
    bgmOn = 0;                  /* 再構築中は ISR に BGM を無視させる(単バイト) */
    fm_silence();               /* ★前の曲の FM を必ず落としてから(落とし忘れると鳴り続ける) */
    fmDrum = (u8)(g_opll && FM_DRUM_TRACK(track));
    if (track >= BGM_TRACK_COUNT) { fmDrum = 0; return; }
    data_read(BGM_BANK, bgm_off[track], bgm_ram, bgm_len[track]);
    p = bgm_ram;
    nMel = p[0]; nBas = p[1]; basStep = p[2];
    melPeak = p[3]; melSus = p[4]; melVib = p[5];
    basPeak = p[6]; basSus = p[7]; drumOn = p[8]; bassSweep = p[9];
    melLoop = p[10]; basLoop = p[11];
    drmGate = (u8)(basLoop == 0);   /* ループ位置付きの曲はドラムを本編から */
    if (drumOn) { u16 doff = (u16)(drum_off + ((drumOn - 1) << 5));   /* style別21B(32Bストライド)をRAMへ */
                  data_read(BGM_BANK, doff, drm_blk, DRUM_STYLE_BYTES); }
    mel_n = p + BGM_HDR;
    mel_l = p + BGM_HDR + nMel;
    bas_n = p + BGM_HDR + nMel + nMel;
    /* ★FM の仕込みは**曲を読んでから**。音色・音量・和音の音程は bgm_ram の中にあるので、
       data_read より前に仕込むと**前の曲の設定**で鳴り出す。 */
    if (fmDrum) opll_rhythm_init();
    /* ★bgm_voice/bgm_drum は「trem/drmT==0 なら *先に* idx を進めてから鳴らす」実装。0 始まりだと
       1音目を飛ばす。**最初の前進で 0 になる値**から始めること(音符は 255、16手ドラムは 15)。 */
    mIdx = 255; mTrem = mCl = 0;   /* ★最初の前進で (u8)(255+1)=0 → 1音目。n-1 始まりだとループ位置付きの曲で */
    bIdx = 255; bTrem = bCl = 0;   /*   最初の前進がループ位置へ飛び、イントロが丸ごと飛ばされた(一度そうなった) */
    drmIdx = 15; drmT = drmType = drmVol = 0;   /* 16手ドラムも最初の前進で 0 へ */
    bgmLoaded = 1;
    g_bgm_t0 = snd_ticks;
    bgmOn = 1;
}

/* ★止めた曲を**続きから**鳴らし直す(メガクラッシュの停止明け)。
   bgm_stop() は bgmOn を落として消音するだけで、音符インデックス(mIdx/bIdx/drmIdx)も
   bgm_ram の曲データもそのまま残る。そこへ bgmOn=1 を戻せば中断した小節から続く。
   bgm_play() で再開すると毎回イントロから鳴り直してしまう(実機で指摘された)。
   ★一度も bgm_play していない場合は mel_n 等が未設定なので何もしない。 */
void bgm_resume(void) {
    if (!bgmLoaded) return;
    /* ★FM は bgm_stop で完全に止めてあるので、仕込み直さないと**戻ってこない**
       (メガクラッシュ明けに FM だけ鳴らなくなる) */
    if (fmDrum) opll_rhythm_init();
    bgmOn = 1;
}

void bgm_stop(void) {
    bgmOn = 0;
    fm_silence();               /* ★FM も完全に止める(リズム・和音・主旋律・保留中の予約まで) */
    psg(8, 0); psg(9, 0);       /* melody(A)/bass(B) 消音 */
    psg(10, 0);                 /* ★drum/noise(C) も消音。放置すると直前のドラム音量が残り
                                   「さーーー」とノイズが鳴り続ける(ステージ開始カードで顕在化)。
                                   SFXがCを使う場合も sfx_update が次フレーム音量を再設定するので安全。 */
}

/* 1声を進める。busy(SFXがこのchを使用中)なら PSG 書込を譲る。ch: 0=toneA / 1=toneB。 */
static void bgm_voice(u8 *idx, u8 *trem, u8 *curlen,
                      const u8 *notes, const u8 *lens, u8 n, u8 loop,
                      u8 fixed, u8 ch, u8 peak, u8 sustain, u8 vib, u8 sweep, u8 busy) {
    u8 note, el, vol;
    u16 p;
    if (*trem == 0) {
        if ((u8)(*idx + 1) >= n) *idx = loop; else (*idx)++;
        *trem = fixed ? fixed : lens[*idx];
        *curlen = *trem;
    }
    (*trem)--;
    if (busy) return;
    note = notes[*idx];
    if (note == BGM_REST || *trem < 2) { psg((u8)(8 + ch), 0); return; }   /* 休符/末尾=無音 */
    el  = (u8)(*curlen - *trem);
    p = bgm_notetp[note];
    if (sweep) {
        /* ロックマン風シンセドラム: 立上り(el 1..4)で約1oct上→基音へ急降下グライド＋2倍速の打撃減衰。 */
        if (el <= 4) p = (u16)(p - (u16)(((u16)(p >> 1) * (u8)(5 - el)) >> 2));
        { u8 d = (u8)((el - 1) << 1); vol = (d < peak) ? (u8)(peak - d) : 0; }
    } else {
        vol = (el <= (u8)(peak - sustain)) ? (u8)(peak - (el - 1)) : sustain;
        if (vib && el > 8) p = (u16)((s16)p + bgm_vibt[(el >> 1) & 7]);
    }
    psg((u8)(ch * 2), (u8)(p & 0xFF));
    psg((u8)(ch * 2 + 1), (u8)((p >> 8) & 0x0F));
    psg((u8)(8 + ch), vol);
}

/* ドラム(noise ch=2)。tempo(style別)毎に次ステップ。busy(SFX命中/破壊)中は譲る。 */
static void bgm_drum(u8 busy) {
    if (drmT == 0) {
        drmIdx  = (u8)((drmIdx + 1 >= 16) ? 0 : drmIdx + 1);
        drmT    = drm_blk[20];              /* テンポ(style: 標準/重い=8, 激しい=6) */
        drmType = drm_blk[drmIdx];          /* パターン(style別) */
        drmVol  = drm_blk[16 + drmType];    /* 初期音量 v0[type](style別) */
        /* ★スネアの拍(drmType==2)だけ turboR 内蔵 PCM を重ねる。鳴らせない機械・設定では
           pcm_snare の中で素通りするので、ここに条件は要らない(常駐を増やさないため)。
           ★ここは ISR。pcm_snare は ei しない入口になっている(pcm.s 参照)。 */
        if (drmType == 2) pcm_snare();
        /* ★FM も同じ拍で叩く。SFX に譲る PSG と違い、FM は専用の ch なので常に鳴らしてよい */
        if (fmDrum && drmType && g_pcm != PCM_ONLY) {
            opll_w(OPLL_RHY_REG, OPLL_RHY_ON);
            opll_w(OPLL_RHY_REG, (u8)(OPLL_RHY_ON | FM_RHY(drmType)));
        }
    }
    drmT--;
    if (busy) return;
    /* ★PCM ONLY(隠し設定): PSG のドラムも鳴らさない＝ドラムは PCM のスネアだけ(聞き比べ用)。
       ch C は無音にしておく(drmType==0 と同じ扱い)。 */
    if (drmType == 0 || g_pcm == PCM_ONLY) { psg(10, 0); return; }
    psg(6, bgm_drmNP[drmType]);
    psg(10, drmVol);
    drmVol = (drmVol > bgm_drmDec[drmType]) ? (u8)(drmVol - bgm_drmDec[drmType]) : 0;
}

/* ISR から毎フレーム(sfx_update の後)。SFX が使う ch は譲る。 */
void bgm_update(void) {
    if (!bgmOn) return;
    bgm_voice(&mIdx, &mTrem, &mCl, mel_n, mel_l, nMel, melLoop, 0,       0, melPeak, melSus, melVib, 0,         0);           /* ★メロディ(chA)は死守=SFXに絶対譲らない */
    bgm_voice(&bIdx, &bTrem, &bCl, bas_n, (const u8 *)0, nBas, basLoop, basStep, 1, basPeak, basSus, 0, bassSweep, sfx_busy_b);  /* ★ベース(chB)がSFX(SHOT/PHIT)に譲る */
    /* ★FM の和音と主旋律の重ね: 音符が変わった瞬間だけ**予約**する(書くのは本体のループ=fm_flush) */
    if (fmDrum) {
        if (bIdx != fmPadIdx) { fmPadIdx = bIdx; fmPadNote = bas_n[bIdx]; }
        if (mIdx != fmMelIdx) { fmMelIdx = mIdx; fmMelNote = mel_n[mIdx]; }
    }
    if (!drmGate && bIdx == basLoop && bTrem == (u8)(basStep - 1)) { drmGate = 1; drmIdx = 15; drmT = 0; }   /* ★本編の頭(ベースがループ位置に来た瞬間)でドラムを小節頭から入れる */
    if (drumOn && drmGate) bgm_drum(sfx_busy_c);
}

/* ファンファーレ本体(前景・同期再生)。bgmを止め、mel=toneA/har=toneB を直接鳴らして
   vdp_wait_frame で尺を取る(ISRのbgm_updateは bgmOn=0 で沈黙)。終了まで戻らない。 */
void fanfare_seq(const u8 *mel, const u8 *har, const u8 *len, u8 n) {
    u8 i, f;
    bgm_stop();     /* ★FM ごと止める。ここは前景同期で数秒戻らないので、放っておくと鳴り続ける */
    for (i = 0; i < n; i++) {
        u16 tp = bgm_notetp[mel[i]]; psg(0, (u8)(tp & 0xFF)); psg(1, (u8)((tp >> 8) & 0x0F)); psg(8, 14);
        tp = bgm_notetp[har[i]];     psg(2, (u8)(tp & 0xFF)); psg(3, (u8)((tp >> 8) & 0x0F)); psg(9, 11);
        for (f = 1; f < len[i]; f++) vdp_wait_frame();
        psg(8, 0); psg(9, 0);         /* 末尾1フレーム無音=音符の区切り */
        vdp_wait_frame();
    }
}

/* 沈没音(自機撃墜/ゲームオーバー): 44フレームの下降音(周期上昇=音程降下)。旧版移植。前景同期。 */
void play_sink(void) {
    u8 t;
    bgm_stop();     /* ★同上(撃墜/ゲームオーバーで FM が伸びたまま残っていた) */
    for (t = 0; t < 44; t++) {
        u16 p = (u16)(240 + (u16)t * 14);
        psg(0, (u8)(p & 0xFF)); psg(1, (u8)((p >> 8) & 0x0F));
        psg(8, (t < 34) ? 12 : 0);          /* 音量12、最後10フレームでフェード */
        vdp_wait_frame();
    }
    psg(8, 0); psg(10, 0);
}

/* 勝ちどきファンファーレ(撃破演出で使用)。 */
void play_fanfare(void) {
    /* 勝ちどきファンファーレ(旧版 fanVicMel/Har/Len を忠実移植=13音)。8音版より高揚・確定感。 */
    static const u8 fmel[13] = { 33,38,42,45,45,42,45,47,45,42,38,45,45 };
    static const u8 fhar[13] = { 26,30,33,38,38,33,38,42,38,33,30,38,38 };
    static const u8 flen[13] = {  8, 8, 8,16, 8, 8,16, 8,24, 8, 8,16,48 };
    fanfare_seq(fmel, fhar, flen, 13);
}

/* ★開始ファンファーレ(play_fanfare_open)は表ごと bank16 へ移した(ship_render.c の fanfare_open_impl)。
     勝ちどき(play_fanfare)は叫びと重ねるため窓を音声バンクへ向けて鳴らすので、表は常駐に残す。 */

/* 毎フレームの合図(raster.c の走査線割込み。以前は H.TIMI)から呼ばれる ISR。割込み文脈なので使用レジスタを全退避(__naked で自前 ret)。
   snd_ticks を進め、sfx_update を回す(将来 bgm_update もここへ)。 */
void snd_isr(void) __naked {
    __asm
        push af
        push bc
        push de
        push hl
        push ix
        push iy
        ld   hl, (_snd_ticks)
        inc  hl
        ld   (_snd_ticks), hl
        call _sfx_update
        call _bgm_update
        pop  iy
        pop  ix
        pop  hl
        pop  de
        pop  bc
        pop  af
        ret
    __endasm;
}

static void psg_init(void) {
    psg(7, 0x9C);                  /* mixer: tone A,B on / noise C on / tone C off */
    psg(6, 16);                    /* noise period 既定 */
    sfxType[0] = sfxType[1] = sfxType[2] = 0;
    sfxTimer[0] = sfxTimer[1] = sfxTimer[2] = 0;
    snd_ticks = 0;
    snd_active = 0;
    sfx_busy_b = 0;
    bgm_stop();     /* bgmOn=0 ＋ A/B/C 消音 ＋ FM も落とす(起動/初期化でも念のため) */
}

/* ★snd_isr は raster.c の割込み処理が毎フレームの合図(走査線割込み)から呼ぶ。
   以前は BIOS の H.TIMI(VBLANK)に仕込んでいたが、VBLANK 割込みはもう使わない(raster.c 冒頭)。 */
void sound_init(void) {
    psg_init();
}
