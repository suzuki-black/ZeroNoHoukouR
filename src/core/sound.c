/* sound.c — PSG効果音＋BGMドライバ＋H.TIMI 60Hz割込み(常駐)。
   前作 BattleshipProto(実機確定)の sfx エンジン/ISR イディオムを移植・整理し、BGMを追加。
   BGM: melody=tone A(SFX SHOTと共有・SFX優先), bass=tone B。曲データはバンク8→RAMコピー。 */
#include "sound.h"
#include "bank.h"       /* data_read(曲データをバンク→RAM) */
#include "vdp.h"        /* vdp_wait_frame(ファンファーレの前景同期) */
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
static const u8 sfxDur[SFX_COUNT] = { 0, 8, 4, 28, 16, 6 };   /* NONE/SHOT/HIT/BOOM/PHIT/EFIRE */
static u8 sfxType[SND_CH];
static u8 sfxTimer[SND_CH];

volatile u16 snd_ticks;
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
        if (t == SFX_SHOT || t == SFX_PHIT) bb = 1;               /* ★tone B を SFX が占有(メロディ=A死守。ベースが譲る) */
        else if (t == SFX_HIT || t == SFX_BOOM || t == SFX_EFIRE) bc = 1;   /* noise C を占有 */
        if (t == SFX_SHOT) {                       /* 高→低の下降レーザー(tone B) */
            u16 p = 40 + (u16)(7 - rem) * 62;
            psg(2, p & 0xFF); psg(3, (p >> 8) & 0x0F);   /* ★chB tone period */
            psg(9, (rem >= 2) ? 13 : (rem * 6));          /* ★chB volume */
        } else if (t == SFX_PHIT) {                /* 自機被弾: 低い下降の痛み音(tone B) */
            u16 p = (u16)(120 + (u16)(15 - rem) * 30);   /* 周期↑=音程↓(下降) */
            psg(2, p & 0xFF); psg(3, (p >> 8) & 0x0F);   /* ★chB */
            psg(9, (rem >= 2) ? 12 : (u8)(rem * 5));      /* ★chB volume */
        } else if (t == SFX_HIT) {                 /* 短いノイズ "コッ" */
            psg(6, 15); psg(10, rem * 3);
        } else if (t == SFX_EFIRE) {               /* 敵発砲: 静かな短いノイズ "プッ" */
            psg(6, 12); psg(10, (u8)(rem * 2));    /* 低音量(自機弾より静か) */
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
   曲データ(RAMコピー)= [nMel,nBas,basStep, melPeak,melSus,melVib, basPeak,basSus, drumOn,
                          melNote(nMel), melLen(nMel), basNote(nBas)]。
   melody=tone A(可変長), bass=tone B(固定basStep), drum=noise C(標準マーチ)。各パート独立ループ。
   エンベロープ: 発音開始 peak → 毎フレーム-1 → sustain、末尾2フレーム無音。vib で伸ばし音に揺れ。 */
#define BGM_REST 255
static const s8 bgm_vibt[8]   = { 0, 1, 2, 1, 0, -1, -2, -1 };
static const u8 bgm_drmNP[4]  = { 0, 20, 7, 3 };   /* noise周期(全style共通) 1=キック/2=スネア/3=ハット */
static const u8 bgm_drmDec[4] = { 0, 3, 2, 3 };    /* 毎フレーム減衰(全style共通) */
/* ★面別ドラム: スタイル別の [pat16, v0(4), tempo] はデータバンク(drum_off)に置き、bgm_play で当該21BをRAMへ。 */
static u8 drm_blk[DRUM_STYLE_BYTES];   /* [0..15]=pattern / [16..19]=v0 / [20]=tempo */

static u8  bgm_ram[BGM_RAM_MAX];
static u8 *mel_n, *mel_l, *bas_n;
static u8  nMel, nBas, basStep, melPeak, melSus, melVib, basPeak, basSus, drumOn;
static u8  bassSweep;   /* 1=ベース(chB)をロックマン風シンセドラムに(立上り1oct上→基音へ急降下＋速い減衰) */
static u8  mIdx, mTrem, mCl;    /* melody: index / 残フレーム / 発音長 */
static u8  bIdx, bTrem, bCl;    /* bass */
static u8  drmIdx, drmT, drmType, drmVol;
static u8  bgmOn;

void bgm_play(u8 track) {
    u8 *p;
    bgmOn = 0;                  /* 再構築中は ISR に BGM を無視させる(単バイト) */
    if (track >= BGM_TRACK_COUNT) return;
    data_read(BGM_BANK, bgm_off[track], bgm_ram, bgm_len[track]);
    p = bgm_ram;
    nMel = p[0]; nBas = p[1]; basStep = p[2];
    melPeak = p[3]; melSus = p[4]; melVib = p[5];
    basPeak = p[6]; basSus = p[7]; drumOn = p[8]; bassSweep = p[9];
    if (drumOn) { u16 doff = (u16)(drum_off + ((drumOn - 1) << 5));   /* style別21B(32Bストライド)をRAMへ */
                  data_read(BGM_BANK, doff, drm_blk, DRUM_STYLE_BYTES); }
    mel_n = p + 10;
    mel_l = p + 10 + nMel;
    bas_n = p + 10 + nMel + nMel;
    /* ★idx は n-1 で初期化する。bgm_voice/bgm_drum は「trem/drmT==0 なら *先に* idx を進めて
       から鳴らす」実装のため、0 始まりだと最初のtickで idx が 0→1 に進み 1音目(index0)を飛ばし
       2音目から鳴る=「曲が途中から始まる」。n-1 始まりなら最初の前進で 0 に戻り 1音目から正しく
       鳴る(旧版の drmIdx=DRM_N-1 と同じ流儀)。 */
    mIdx = (u8)(nMel ? nMel - 1 : 0); mTrem = mCl = 0;
    bIdx = (u8)(nBas ? nBas - 1 : 0); bTrem = bCl = 0;
    drmIdx = 15; drmT = drmType = drmVol = 0;   /* 16手ドラムも最初の前進で 0 へ */
    bgmOn = 1;
}

void bgm_stop(void) {
    bgmOn = 0;
    psg(8, 0); psg(9, 0);       /* melody(A)/bass(B) 消音 */
    psg(10, 0);                 /* ★drum/noise(C) も消音。放置すると直前のドラム音量が残り
                                   「さーーー」とノイズが鳴り続ける(ステージ開始カードで顕在化)。
                                   SFXがCを使う場合も sfx_update が次フレーム音量を再設定するので安全。 */
}

/* 1声を進める。busy(SFXがこのchを使用中)なら PSG 書込を譲る。ch: 0=toneA / 1=toneB。 */
static void bgm_voice(u8 *idx, u8 *trem, u8 *curlen,
                      const u8 *notes, const u8 *lens, u8 n,
                      u8 fixed, u8 ch, u8 peak, u8 sustain, u8 vib, u8 sweep, u8 busy) {
    u8 note, el, vol;
    u16 p;
    if (*trem == 0) {
        *idx = (u8)((*idx + 1 >= n) ? 0 : *idx + 1);
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
    }
    drmT--;
    if (busy) return;
    if (drmType == 0) { psg(10, 0); return; }
    psg(6, bgm_drmNP[drmType]);
    psg(10, drmVol);
    drmVol = (drmVol > bgm_drmDec[drmType]) ? (u8)(drmVol - bgm_drmDec[drmType]) : 0;
}

/* ISR から毎フレーム(sfx_update の後)。SFX が使う ch は譲る。 */
void bgm_update(void) {
    if (!bgmOn) return;
    bgm_voice(&mIdx, &mTrem, &mCl, mel_n, mel_l, nMel, 0,       0, melPeak, melSus, melVib, 0,         0);           /* ★メロディ(chA)は死守=SFXに絶対譲らない */
    bgm_voice(&bIdx, &bTrem, &bCl, bas_n, (const u8 *)0, nBas, basStep, 1, basPeak, basSus, 0, bassSweep, sfx_busy_b);  /* ★ベース(chB)がSFX(SHOT/PHIT)に譲る */
    if (drumOn) bgm_drum(sfx_busy_c);
}

/* ファンファーレ本体(前景・同期再生)。bgmを止め、mel=toneA/har=toneB を直接鳴らして
   vdp_wait_frame で尺を取る(ISRのbgm_updateは bgmOn=0 で沈黙)。終了まで戻らない。 */
static void fanfare_seq(const u8 *mel, const u8 *har, const u8 *len, u8 n) {
    u8 i, f;
    bgmOn = 0;
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
    bgmOn = 0;
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

/* 開始ファンファーレ(ステージ開始カードで使用。旧版 fanOp を移植)。BGM無音でこれだけ鳴らす。 */
void play_fanfare_open(void) {
    static const u8 omel[8] = { 33,38,42,45,42,38,33,38 };
    static const u8 ohar[8] = { 26,30,33,38,33,30,26,26 };
    static const u8 olen[8] = {  8, 8,16,24, 8, 8, 8,36 };
    fanfare_seq(omel, ohar, olen, 8);
}

/* H.TIMI から呼ばれる ISR。割込み文脈なので使用レジスタを全退避(__naked で自前 ret)。
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
    psg(8, 0); psg(9, 0); psg(10, 0);
    sfxType[0] = sfxType[1] = sfxType[2] = 0;
    sfxTimer[0] = sfxTimer[1] = sfxTimer[2] = 0;
    snd_ticks = 0;
    snd_active = 0;
    sfx_busy_b = 0;
    bgmOn = 0;
}

/* H.TIMI(0xFD9F, 5バイトフック)へ JP snd_isr を仕込む。BIOSの垂直割込みが毎回CALLしてくる。 */
static void install_isr(void) {
    __asm
        di
        ld   a, #0xC3            ; JP opcode
        ld   (0xFD9F), a
        ld   hl, #_snd_isr
        ld   (0xFDA0), hl
        ei
    __endasm;
}

void sound_init(void) {
    psg_init();
    install_isr();
}
