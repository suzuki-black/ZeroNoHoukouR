/* hud.c — スプライトHUD実装。数字パターンは自前8x8フォント('0'..'9')を
   16x16 スプライトの左上 8x8 へ写して生成(vdp_text と同じ vdp_glyph 経由=書体を統一)。
   ★HUD は**最後尾のスロット**(HUD_SL0..31)＝スプライトの最低優先度。中ボスや撃破点数の
     ポップが HUD の手前に出る。V9938 は優先度と「1走査線8枚」の順序が同じなので、最後尾に
     置くと混んだ行では HUD が先に欠ける。そこで**同じ行に並べる枚数を減らして**ある:
       ・上位2桁を 1 枚に詰める(16x16 の左8列=万の位 / 右8列=千の位)＝スコアは4枚
       ・残機表示は下段(ボム棒と同じ行)へ移動＝上段はスコアだけ
     詳しい実測値と根拠は hud.h の頭を参照。 */
#include "hud.h"
#include "vdp.h"
#include "sprites.h"
#include "entity.h"   /* g_spr_base(エンティティ描画の開始スロット) */
#include "gamestate.h" /* g_crush: メガクラッシュ残数 */
#include "bank.h"      /* bcall_to: 数字パターンの投入を冷たいバンクへ出した */
#include "opll.h"      /* g_cold_mode / COLD_HUDDIG */

static void crush_pattern(u8 bars);

/* HUD が VRAM に持っているもの(色表＋ボム棒のパターン)を置き直す。
   ★hud_init だけでなく、**他の用途に奪われた後の復旧**にも呼ぶ: 津波は32枚すべての色表と
     ボム棒のパターン枠(SPR_CRUSH=SPR_WAVE4)を自前の水で上書きする。 */
void hud_colors(void) {
    u8 d;
    if (g_crush) crush_pattern((u8)(g_crush <= 3 ? g_crush : 1));   /* 棒のパターンを描き直す */
    for (d = 0; d < 4; d++) vdp_sprite_color((u8)(HUD_SL0 + d), 15);   /* スコア=白(上位2桁詰め＋3桁) */
    /* ★ボム(メガクラッシュ)残数(画面下)。棒は**行別カラー**で「熱い棒」に見せる
       (白い縁→橙→赤い芯→橙→白い縁)。単色の白い棒だと HUD の数字と区別がつかず、
       ボムのストックに見えない。数字(4本目以降)は白。 */
    { static const u8 crush_col[16] = { 15,15,15, 12,12,12, 11,11,11,11,11, 12,12,12, 15,15 };
      vdp_sprite_color_tab(HUD_SL_BAR, crush_col); }
    vdp_sprite_color(HUD_SL_BNUM, 15);
    vdp_sprite_color(HUD_SL_LICON, 3);    /* 残機アイコン=緑(零戦シルエット) */
    vdp_sprite_color(HUD_SL_LNUM, 11);    /* 残機数=黄 */
#ifdef DEBUG_FPS
    { u8 sl; for (sl = (u8)(HUD_SL0 + 8); sl < 32; sl++) vdp_sprite_color(sl, 13); }   /* FPS2桁＋mask値2桁=ほぼ黒(視認性) */
#endif
}

/* ★スコアの上位2桁を 1 枚のパターンに詰める。16x16 のパターンは前半16B=左8列/後半16B=右8列
   なので、左に万の位、右に千の位の 8x8 グリフを置けばよい。桁が変わったときだけ焼き直す。
   ★作業場は pat_buf を借りる(常駐 RAM が足りない。焼くのは 1 面に数回だけなので毎回 0 で埋め直してよい)。
     ローカル配列にすると sdcc が IX のスタック枠を組んで常駐を 150B 食った。 */
u8 g_hud_skip;
static u8 pat_buf[32];     /* 32B すべてを毎回書く用途の作業場(数字の投入・ボム棒・上位2桁・HUD の属性)。
                              ★ローカル配列にすると sdcc が IX のスタック枠を組んで常駐を食う。 */
static u8 dig[5];          /* 今のスコアの 5 桁 */

/* ★asm(C だと常駐を 30B 近く食った)。A = a、L = b */
static void score_hi_pattern(u8 a, u8 b) __naked {
    (void)a; (void)b;
    __asm
        ld   c, a                ; C = 万の位
        ld   e, l                ; E = 千の位
        ld   hl, #_pat_buf
        ld   b, #32
        xor  a
    00001$:
        ld   (hl), a             ; 下半分は 0
        inc  hl
        djnz 00001$
        push de
        ld   a, c
        ld   de, #_pat_buf
        call 00010$              ; 左 8 列 = 万の位
        pop  de
        ld   a, e
        ld   de, #_pat_buf + 16
        call 00010$              ; 右 8 列 = 千の位
        ld   de, #_pat_buf
        ld   a, #SPR_SCOREHI
        jp   _vdp_sprite_pattern
    00010$:                      ; 数字 A の 8 行を DE へ
        push de
        add  a, #0x30            ; '0'
        call _vdp_glyph          ; DE = グリフ
        ex   de, hl
        pop  de
        ld   bc, #8
        ldir
        ret
    __endasm;
}

void hud_init(void) {
    /* ★数字パターンの投入(10桁ぶんの転送)は**冷たいバンク**(coldsetup)へ出してある。
       面の準備で1回しか走らないのに常駐を 60B 超食っていたため(叫びの置き場所が要って見直した)。 */
    g_cold_mode = COLD_HUDDIG; bcall_to(COLDSETUP_BANK); g_cold_mode = COLD_STAGE;
    score_hi_pattern(dig[0], dig[1]);   /* ★0, 0 と焼くと、ミスの再開や次の面で上 2 桁が次に変わるまで 00 に見える(hud_draw は変わるまで焼かない) */
    hud_colors();
    g_spr_base = 0;   /* ★エンティティは slot0 から＝HUD より手前。HUD は最後尾(HUD_SL0..31) */
}

/* 枠の並び(HUD_SL0 からの相対): 0=スコア上位2桁(詰め) / 1..3=百・十・一の位 /
   4=ボム棒 / 5=ボム残数 / 6=残機アイコン / 7=予備機数。
   ★HUD は最後尾＝最低優先なので、8枚/走査線を超えると**HUD の後ろから**欠ける。
     いちばん先に消えるのは予備機数、次が残機アイコン＝困らない順に並べてある。 */
/* ★ボム残数の「棒」パターンを描き直す(bars=1..3)。16x16 の左上から
     幅4px の棒を 6px 間隔で bars 本 …… |  / | |  / | | |
   3本が並ぶ最大の太さが 4px(4+2+4+2+4=16)。1本=小さすぎて見えない、を避けるため
   高さは 14px(row1..14)まで伸ばす。
   ★パターン番号を3つ確保する案は採れなかった(16x16の空き枠は SPR_CRUSH の1つだけ。
     144-156 は津波 SPR_WAVE0 が使う。津波は SPR_PWRLV も借りるが、あれは終了時に
     VRAM の控えから描き直す約束で借りている)。残数が変わったときだけ 32B を書き換える
     ＝1面で数回しか走らないので、VRAM 帯域から見れば無に等しい。 */
static void crush_pattern(u8 bars) {
    u8 r, l = 0xF0, rt = 0x00;   /* 1本目: x0-3 */
    if (bars >= 2) { l |= 0x03; rt |= 0xC0; }   /* 2本目: x6-9 (左2bit＋右2bit) */
    if (bars >= 3) { rt |= 0x0F; }              /* 3本目: x12-15 */
    for (r = 0; r < 16; r++) {
        u8 on = (u8)(r >= 1 && r <= 14);
        pat_buf[r]      = on ? l  : 0;   /* 左半分(x0-7)  */
        pat_buf[16 + r] = on ? rt : 0;   /* 右半分(x8-15) */
    }
    vdp_sprite_pattern(SPR_CRUSH, pat_buf);
}

/* HUD の k 番目の枠(HUD_SL0 + k)の属性を pat_buf に組む。Y は縦スクロールの補正つき、216(停止マーカ)は 215 へ避ける。
   ★asm(C だと 60B あり、常駐に入らなかった)。A = k、L = x、スタックに y・pat(1 バイトずつ。呼ばれた側が片付ける) */
static void hud_put(u8 k, u8 x, u8 y, u8 pat) __naked {
    (void)k; (void)x; (void)y; (void)pat;
    __asm
        pop  bc                  ; 戻り番地
        pop  de                  ; E = y, D = pat
        push bc
        add  a, a
        add  a, a                ; k * 4
        ld   c, l                ; C = x
        ld   hl, #_pat_buf
        add  a, l
        ld   l, a
        jr   nc, 00001$
        inc  h
    00001$:
        ld   a, (_g_vscroll)
        add  a, e
        dec  a                   ; 表示 Y = 属性 Y + 1
        cp   #216
        jr   nz, 00002$
        dec  a                   ; 216 は停止マーカ
    00002$:
        ld   (hl), a
        inc  hl
        ld   (hl), c
        inc  hl
        ld   (hl), d
        inc  hl
        ld   (hl), #0
        ret
    __endasm;
}

void hud_draw(u16 score, u8 lives) {
    /* ★桁分解(除算×10)はスコア/残機が変化した時だけ=毎フレームの高価な除算を回避(Z80は除算がライブラリ呼び)。
       スプライト位置(vscroll補正で毎フレーム変わる)は毎回書く。 */
    static u16 last_score = 0xFFFF;
    static u8  last_lives = 0xFF;   /* 残機は最大 5(設定の 2/3/5。増える仕組みは無い)なので桁分解は要らない */
    static u8  last_crush = 0xFF;   /* ボム残数: 変化時だけパターンを書き換える */
    static u8  last_vs;
    u8 crush_n = g_crush;
    u8 i;
    /* ★同じフレームの 2 回目(scroll_to で描いた後の VBLANK の分)は、入力が全部同じなら省く(VRAM はもう同じ中身)。
         g_hud_skip は scroll_to が描いた直後に立て、scene.c のループが毎フレーム倒す=前のフレームからは持ち越さない。 */
    if (g_hud_skip && score == last_score && lives == last_lives && crush_n == last_crush && g_vscroll == last_vs) return;
    last_vs = g_vscroll;
    if (score != last_score) {
        static const u16 place[5] = { 10000, 1000, 100, 10, 1 };
        u8 hi0 = dig[0], hi1 = dig[1];
        for (i = 0; i < 5; i++) dig[i] = (u8)((score / place[i]) % 10);
        last_score = score;
        /* ★上位2桁の詰めパターンは、その2桁が変わったときだけ焼く(32B の書込み)。
           点が入るたびに動くのは下位桁なので、ほとんどのフレームで焼き直しは起きない。 */
        if (dig[0] != hi0 || dig[1] != hi1) score_hi_pattern(dig[0], dig[1]);
    }
    last_lives = lives;
    if (crush_n != last_crush) {
        last_crush = crush_n;
        if (crush_n) crush_pattern((u8)(crush_n <= 3 ? crush_n : 1));   /* ★pat_buf を使うので、下で属性を組む前に */
    }
    /* ★8 枚の属性(Y,X,パターン,0)を pat_buf に組み、表ごとに 1 回で送る(vdp_hud_burst)。並びは HUD_SL0 からの枠の順:
         0=スコア上位2桁(詰め) / 1..3=百・十・一の位 / 4=ボム棒 / 5=ボム残数 / 6=残機アイコン / 7=予備機数。
       ★上位2桁は1枚(16x16)、残り3桁は従来どおり1桁1枚。位置は詰める前と同じ並び(16,24,32,40,48)。
       ★左の HUD は X=16 から。面の間は左端 8 ドットを隠すので、X=8 だと隠れた帯にぴったり接する(右の HUD の余白は 14)。 */
    hud_put(0, 16, 2, SPR_SCOREHI);
    for (i = 2; i < 5; i++) hud_put((u8)(i - 1), (u8)(16 + i * 8), 2, (u8)(SPR_DIGIT0 + dig[i] * 4));
    /* ★残機は下段(ボム棒と同じ行)。上段に置くとスコアと同じ走査線を食い、最低優先度の HUD が
       欠けやすくなる(上段4枚なら実測で約3%、5枚以上だと一気に増える)。 */
    hud_put(6, 212, 188, SPR_ZERO);                          /* 残機=零戦シルエット */
    hud_put(7, 234, 190, (u8)(SPR_DIGIT0 + lives * 4));      /* 予備機数(最大 5) */
    /* ★ボム残数を画面下(左)へ。1〜3 は棒の本数そのもの( | / | | / | | | )で数えずに読める。
       4以上(コンフィグや将来のパワーアップ)は棒1本＋数字にする(棒を4本以上並べても読めない)。
       残0のときは画面外へ退避して見せない。 */
    if (crush_n) hud_put(4, 16, 190, SPR_CRUSH);
    else         hud_put(4, 0, 220, SPR_CRUSH);                                  /* 画面下端外へ */
    if (crush_n > 3) hud_put(5, 35, 192, (u8)(SPR_DIGIT0 + (crush_n % 10) * 4));
    else             hud_put(5, 0, 220, SPR_DIGIT0);                             /* 3 本以下(0 を含む)は数字を出さない */
    vdp_hud_burst(pat_buf);
#ifdef DEBUG_FPS
    /* ★デバッグROMのみ。左2桁=g_fps(JIFFY基準の参考値)、右4桁=フレームカウンタ(ストップウォッチ実測用の真値)。
       使い方: 右4桁を読む→スマホで正確に10秒→もう一度読む→(差)/10=実FPS。JIFFYの進み方に依存しない。 */
    /* ★y=24 に FPS2桁(slot9,10)＋mask値2桁(slot11,12)。低slot=高優先なのでゲームスプライトより必ず出る。
       ★"MASK" の文字は廃止(16x16 パターンの空き枠4つを津波 SPR_WAVE0 へ譲った)。 */
    { u8 f = (g_fps > 99) ? 99 : g_fps;
      vdp_sprite_pos((u8)(HUD_SL0 + 8),  96, 24, (u8)(SPR_DIGIT0 + (f / 10) * 4));   /* FPS十の位 */
      vdp_sprite_pos((u8)(HUD_SL0 + 9), 104, 24, (u8)(SPR_DIGIT0 + (f % 10) * 4));   /* FPS一の位 */
      vdp_sprite_pos((u8)(HUD_SL0 + 10), 120, 24, (u8)(SPR_DIGIT0 + (u8)((g_dbgmask / 10) % 10) * 4));   /* マスク十の位 */
      vdp_sprite_pos((u8)(HUD_SL0 + 11), 128, 24, (u8)(SPR_DIGIT0 + (u8)(g_dbgmask % 10) * 4)); }        /* マスク一の位 */
#endif
}
