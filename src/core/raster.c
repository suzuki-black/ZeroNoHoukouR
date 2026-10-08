/* raster.c — ラスタ割り込みによる画面分割の土台。設計と作法は raster.h を参照。 */
#include "raster.h"
#include "vdp.h"
#include "msx.h"
#include "hotcode.h"  /* HOT_SLOT_PCM_*(面中の PCM は hot_ram 末尾の hot_pcm.s) */

__sfr __at(0x99) RAS_CTRL;   /* VDP コントロール(ステータス読みにも使う) */
__sfr __at(0x9A) RAS_PAL;    /* パレットデータ */

/* ★毎フレームの合図(VBLANK 割込みの代わり)を出す行。表示の最終行(211)の次=帰線の始まり。
   ★2026-10-07 から VBLANK 割込み(IE0)は使わない。面中の PCM を 4 行ごとの走査線割込みで出すための
     下ごしらえ(docs/PCM調査_2026-10-07.md §4.2・§6)。仕掛け:
       ・R#15 を普段から 1(S#1)にしておく。BIOS は H.KEYI のあと「S#0 のつもりで」0x99 を読むが、
         実際は S#1 が返り、その読み出しが FH を落とす。S#1 の最上位ビット(FL)は 0 なので、
         BIOS は VBLANK と見なさず H.TIMI もキー読み取りもせずに戻る(BIOS を素通りできる)。
       ・JIFFY と音(snd_isr)と分割の仕切り直しは、この行の割込みで自分で行う。
   ★割込みが出せるのは表示ライン・カウンタが 0〜244 の行だけ(212 行モード・60Hz。openMSX VDP.cc /
     PR #2191、FS-A1ST 実機の測定に基づく)。R#19-R#23 がそれ以外になると**二度と割込みが来ない**
     (JIFFY が止まってゲームが固まる)。だから R#23 を書き換えたら必ず張り直す(ras_vscroll)。 */
#define RAS_TICK_LINE 212

/* 割込み応答遅れの補正。BIOS ハンドラ→H.KEYI→本体までの分だけ分割線は下へずれるので、
   その分だけ手前で割り込ませる。★実測で決める値(openMSX と実機で確認すること)。 */
#define RAS_LINE_BIAS 2

/* ★g_ras の実体は resram.c にある(リンク順の最後でないと hot_ram の番地を押し下げる)。 */
u8 g_ras_n;

u8 g_ras_i;          /* 今フレームで次に処理する分割の添字。★asm から参照するので非static */
/* ★いま効いている R#23(縦スクロール)の値。次の分割行の R#19 を計算するのに使う。
   分割で R#23 を動かすと「画面行→VRAM行」の対応が変わるので、g_vscroll では駄目
   (raster.h の作法 3-b)。フレーム先頭で g_vscroll から仕切り直す。 */
u8 g_ras_vs;     /* ★非static: ホット区間の hot_pcm.s が読む */
/* ★1 = いま R#19 に張っているのは毎フレームの合図(0 = 分割の行)。割込みはこれで振り分ける。
   「分割が残っているか(g_ras_i < g_ras_n)」で振り分けると、合図を待っている最中に本体が分割の本数を
   増やしたとき(衝撃波は毎フレーム raster_arm で増減する)、合図の割込みを分割と取り違えて**その回の合図が
   消えた**(openMSX の 5 面で 5 秒に最大 22 フレーム抜けた。2026-10-07)。 */
u8 g_ras_tick;   /* 0=分割 / 1=合図 / 2=PCM のサンプル(hot_pcm.s)。★非static */
/* 前の合図の時刻(システムタイマ E6h/E7h、3.911µs)。★分割が遅れて処理され、合図を張る時点で
   もう合図の行を過ぎていたら、その場で合図の仕事をする(遅れても失わない)。ras_isr 参照。 */
u16 g_ras_ttime;  /* ★非static: hot_pcm.s が読む */
#define RAS_FRAME     4267   /* 1 フレーム(262 行 x 63.7µs = 16.69ms)のシステムタイマのカウント */
/* ★遅れの判定は 1 フレームちょうど。以前は少し短く(16.0ms)していたが、遅れた合図の時刻を「前の合図 + 1 フレーム」に
     するようにしてから、下端近く(202〜211 行)の分割を少し遅れて処理しただけで「遅れ」と取り、合図の時刻を未来に
     置いて合図を余分に出した(openMSX の 5 面で 240 秒に +190 回)。 */
#define RAS_TICK_LATE RAS_FRAME

/* 次の割込みの行を R#19 へ。分割が残っていればその行、無ければ毎フレームの合図。
   ★割込みの中か、di の中から呼ぶこと(0x99 への 2 バイト書き込み)。 */
/* ★もう過ぎた分割は、**その場で当てる**。本体の長い di などで分割が遅れると、次の分割の行はもう過ぎていて、
     張っても次のフレームまで来ない。分割は「合図を張る前に全部」処理する作りなので、合図まで 1 フレーム
     遅れる(3 面の中ボスは 32 行おきに 8 本あり、合図が 2 フレームに 1 回になってゲームが半分の速さになった。
     openMSX、2026-10-07)。
   ★最初は「飛ばす」(そのフレームは捨てる)にしたが、最終面でボス帯の拡大(MAG)を**戻す分割**が飛び、
     フレームの 6% で画面の残り全体が拡大されたままになった(ユーザーが目視で指摘。openMSX で 200 秒中 712 回)。
     数行ずれても状態が正しく戻る方がよいので、捨てずにすぐ当てる。
   ★行の時刻は合図(212 行目)からの経過で測る: 表示行 L は合図の (L + 262 - 212) 行後。1 行 = 63.7µs ≒
     システムタイマ 16.29 カウント。判定は「x = L + 45、x × 16.25 カウント」で、**実際の時刻より 5 行以上手前**。
     ★判定は「すぐ当てる」側に倒す。早めに当てても分割の位置が数行ずれるだけだが、過ぎた行を張るとフレームが
       丸ごと滑って合図が抜ける。割込みの応答だけで 3 行ほどかかるので、それより近い分割はどのみち間に合わない
       (最初は 2 行遅らせて判定し、衝撃波の 3 行おきの分割で合図を落とした)。
     ★5 行手前(x = L + 45)。3 行手前では、PCM のサンプルの割込みの中から張った分割が間に合わず 1 フレーム
       滑った(「今」は合図の応答の遅れと切り捨てで 1〜2 行少なめに出る。hot_pcm.s の LEAD と同じ値にしてある)。 */
void ras_next(void) __naked {   /* ★ras_apply(asm)から jp するので非static。C からも呼ぶ(壊すのは AF/BC/DE/HL) */
    /* ★asm(C だと飛ばす判定で 130B 近くになり常駐から溢れた)。中身:
         el = 今 - g_ras_ttime
         分割が残っている間: x = line + 45、el >= x*16 + x/4 なら過ぎた → g_ras_i++ して次を見る
         残っていれば v = line - RAS_LINE_BIAS(g_ras_tick=0)、無ければ v = RAS_TICK_LINE(g_ras_tick=1)
         R#19 = v + g_ras_vs */
    __asm
        call _pcm_now            ; ★タイマはここ経由で読む(2 バイトを別々に読むと繰り上がりで 256 狂う。pcm.s)
        ld   de, (_g_ras_ttime)
        or   a
        sbc  hl, de
        ex   de, hl              ; DE = el
    00001$:
        ld   a, (_g_ras_i)
        ld   hl, #_g_ras_n
        cp   (hl)
        jr   nc, 00010$          ; 分割が残っていない → 合図
        ld   l, a
        ld   h, #0
        ld   c, l
        ld   b, h
        add  hl, hl
        add  hl, hl
        add  hl, hl
        add  hl, bc              ; i * 9
        ld   bc, #_g_ras         ; + line(先頭)
        add  hl, bc
        ld   a, (hl)
        push af                  ; line
        ld   l, a
        ld   h, #0
        ld   bc, #262 - RAS_TICK_LINE - 5
        add  hl, bc              ; x
        ld   c, l
        ld   b, h
        add  hl, hl
        add  hl, hl
        add  hl, hl
        add  hl, hl              ; x * 16
        srl  b
        rr   c
        srl  b
        rr   c                   ; x / 4
        add  hl, bc              ; しきい値
        or   a
        sbc  hl, de              ; しきい値 - el
        pop  bc                  ; B = line(★pop af だとフラグが戻って比較の結果が消える。最初そう書いて判定が効かなかった)
        jr   c, 00002$           ; el > しきい値 → 過ぎた
        jr   nz, 00003$          ; el < しきい値 → まだ来ていない
    00002$:
        jp   _ras_apply          ; ★過ぎた分割はその場で当てる(当てたあと ras_apply が ras_next へ戻ってくる)
    00003$:
        ld   a, b
        sub  a, #RAS_LINE_BIAS
        ld   c, #0
        jr   00011$
    00010$:
        ld   a, #RAS_TICK_LINE
        ld   c, #1
    00011$:
        ld   hl, #_g_ras_vs
        add  a, (hl)
        ld   b, a
        ld   a, (_pcm_active)    ; ★PCM が鳴っている間は、次のサンプルの行と比べて近い方を張る(hot_pcm.s)
        or   a
        jp   nz, _hot_ram + 3*HOT_SLOT_PCM_ARM   ; B = この行、C = 種類
        ld   a, c
        ld   (_g_ras_tick), a
        ld   a, b
        out  (0x99), a
        ld   a, #0x80+19
        out  (0x99), a
        ret
    __endasm;
}

/* ---- 割込みハンドラ本体(H.KEYI から CALL される) ----
   ★割込み文脈なので全レジスタ退避。R#15 は 1 のまま(ステータスは BIOS が読んで FH を落とす)。 */
void ras_apply(void) __naked {   /* ★asm から CALL するので非static */
    /* ★asm で書いている(C だと 9 バイトの構造体の読み出しで 192B あった。常駐の回収)。
       動作は C 版と同じ: reg/val → reg2/val2 → パレット の順に書き、R#23 を書いたら g_ras_vs を追う(作法 3-b)。
       RasSplit = { line, reg, val, reg2, val2, pidx, pr, pg, pb } の 9 バイト(raster.h)。 */
    __asm
        ld   a, (_g_ras_i)
        ld   l, a
        ld   h, #0
        ld   e, l
        ld   d, h
        add  hl, hl
        add  hl, hl
        add  hl, hl
        add  hl, de              ; HL = i * 9
        ld   de, #_g_ras + 1     ; + 1 = reg
        add  hl, de
        ld   c, #0x99
        call 00010$              ; reg / val
        call 00010$              ; reg2 / val2
        ld   a, (hl)             ; pidx
        cp   #0xFF               ; RAS_NOPAL
        jr   z, 00003$
        out  (c), a
        ld   a, #0x80+16         ; R#16 = パレットポインタ
        out  (c), a
        inc  hl
        ld   a, (hl)             ; pr
        inc  hl
        ld   b, (hl)             ; pg
        inc  hl
        rlca
        rlca
        rlca
        rlca                     ; pr << 4(pr は 0-7)
        or   a, (hl)             ; | pb
        out  (0x9A), a
        ld   a, b
        out  (0x9A), a
    00003$:
        ld   hl, #_g_ras_i
        inc  (hl)
        jp   _ras_next
        ; ---- (HL)=レジスタ番号, (HL+1)=値 を書く。HL は 2 進む ----
    00010$:
        ld   a, (hl)
        inc  hl
        ld   b, (hl)
        inc  hl
        cp   #0xFF               ; RAS_NOREG
        ret  z
        out  (c), b
        or   #0x80
        out  (c), a
        cp   #0x80+23
        ret  nz
        ld   a, b
        ld   (_g_ras_vs), a          ; ★表示起点が動いた=以降の R#19 はこれを基準に(作法3-b)
        ret
    __endasm;
}

/* 毎フレームの合図(RAS_TICK_LINE の割込み。以前は VBLANK 割込み)で呼ぶ: 今フレームの分割を先頭から仕切り直す。
   ★これを本体側(stage_update)でやると駄目だった: stage_update はフレーム途中まで走っているので、
     1本目の分割行(画面上部)を既に通り過ぎており、その分割を毎フレーム取りこぼす。
     実際に「復帰用の分割が効かず画面全体が赤くなる」という形で踏んだ。分割の仕切り直しは
     必ず VBLANK 文脈で行うこと。 */
void ras_rearm(void) __naked {
    /* ★asm(常駐の回収。C 版と同じ動作):
         g_ras_i=0 / g_ras_vs=g_vscroll / 分割が無ければ次の合図を張って終わり
         先頭分割の reg2 が 23 なら val2 = g_ras_vs
         先頭が line==0 なら、line==0 が続く限り ras_apply(各 ras_apply が次の行を張る)。そうでなければ ras_next */
    __asm
        xor  a
        ld   (_g_ras_i), a
        ld   a, (_g_vscroll)
        ld   (_g_ras_vs), a
        ld   c, a
        ld   a, (_g_ras_n)
        or   a
        jp   z, _ras_next
        ld   a, (_g_ras + 3)     ; [0].reg2
        cp   #23
        jr   nz, 00001$
        ld   a, c
        ld   (_g_ras + 4), a     ; [0].val2 = g_ras_vs
    00001$:
        ld   a, (_g_ras)         ; [0].line
        or   a
        jp   nz, _ras_next
    00002$:
        call _ras_apply
        ld   a, (_g_ras_i)
        ld   hl, #_g_ras_n
        cp   (hl)
        ret  nc
        ld   l, a
        ld   h, #0
        ld   e, l
        ld   d, h
        add  hl, hl
        add  hl, hl
        add  hl, hl
        add  hl, de
        ld   de, #_g_ras
        add  hl, de
        ld   a, (hl)             ; [i].line
        or   a
        jr   z, 00002$
        ret
    __endasm;
}

/* 合図の時刻(前の合図 + 1 フレーム)まで待つ。hot_pcm.s が、合図が 3 行以内に迫っているのにサンプルを待っていたときに呼ぶ
   (張っても間に合わないので)。割込みの中なら、戻った先の ras_isr の遅れの判定がその場で合図の仕事をする。壊すのは AF/DE/HL */
void ras_tick_wait(void) __naked {
    __asm
    00001$:
        call _pcm_now
        ld   de, (_g_ras_ttime)
        or   a
        sbc  hl, de
        ld   de, #RAS_TICK_LATE
        or   a
        sbc  hl, de
        jr   c, 00001$
        ret
    __endasm;
}

void ras_isr(void) __naked {
    __asm
        push af
        push bc
        push de
        push hl
        push ix
        push iy
        ; ★ステータスは読まない。来る割込みは走査線割込みだけ(VBLANK 割込みは切ってある)。
        ;   FH は、戻った先の BIOS が「S#0 のつもりで」読む S#1 で落ちる(R#15=1 のため)。
        ld   a, (_g_ras_tick)
        dec  a
        jr   z, 00002$           ; 1 = 張っていたのは毎フレームの合図
        jp   p, 00004$           ; 2 = PCM のサンプルの行(hot_pcm.s)
        ld   a, (_g_ras_i)
        ld   hl, #_g_ras_n
        cp   (hl)
        jr   nc, 00003$          ; ★本体が分割を減らしていた → 分割は適用せず合図を張り直す
        call _ras_apply
    00005$:
        ; ★分割を処理し終えて合図を張ったとき、前の合図から 16ms 以上たっていたら、もう合図の行を
        ;   過ぎている(本体の長い di で分割が遅れた)。張った合図は次のフレームまで来ないので、ここで行う。
        ;   これをしないと合図が 1 回抜けた(openMSX の最終面で 5ms の di のあと。2026-10-07)。
        ld   a, (_g_ras_tick)
        or   a
        jr   z, 00001$
        call _pcm_now
        ld   de, (_g_ras_ttime)
        or   a
        sbc  hl, de
        ld   de, #RAS_TICK_LATE
        or   a
        sbc  hl, de
        jr   c, 00001$           ; まだ合図の行の手前
        ;   ★合図の時刻は「今」ではなく「前の合図 + 1 フレーム」(本来来るはずだった時刻)にする。「今」にすると
        ;     次のフレームもこの分割の位置で「前の合図から 16ms たった」と判定され、合図が 212 行目へ戻らず
        ;     ここに居着いた(openMSX の最終面。18 行目の分割に合図が居着き、PCM は「合図からの経過 = 今の行」の
        ;     見積もりが約 190 行狂ってスネアが崩れた。2026-10-08)。
        ;   ★2 フレーム以上遅れていたら(画面モードの切替の後など)今に合わせる。前の合図 + 1 フレームのままだと
        ;     追いつくまで分割のたびに合図を出してしまう。
        ld   de, #RAS_FRAME
        sbc  hl, de              ; (直前の sbc で借りは出ていない)
        jr   nc, 00002$
        ld   hl, (_g_ras_ttime)
        add  hl, de
        ld   (_g_ras_ttime), hl
        jr   00006$
    00003$:
        call _ras_next
        jr   00001$
    00004$:
        call _ras_next           ; ★サンプルの行: 分割と合図の判断は ras_next に任せ(過ぎた分割はすぐ当てる)、
        jr   00005$              ;   hot_pcm.s がサンプルを出して近い方を張る。合図の遅れも分割と同じく見る
    00002$:
        ; 合図の時刻を控え、前回の合図からの経過を見る。
        ; ★1.5 フレーム(6400 カウント)以上たっていたら、合図が 1 回抜けている(張った行が間に合わなかった・
        ;   本体の長い di など)。JIFFY を 2 進め、音も 2 回進めて、ゲームの速さと曲のテンポを保つ
        ;   (PCM を割込みで出すようにしてから、重い面で 240 秒に数十回抜けた。2026-10-08)。
        call _pcm_now            ; HL = 今
        ld   de, (_g_ras_ttime)
        ld   (_g_ras_ttime), hl
        or   a
        sbc  hl, de              ; 経過
        ld   de, #6400
        or   a
        sbc  hl, de
        jr   c, 00006$           ; 1 フレームぶん
        ld   hl, #0xFC9E
        inc  (hl)                ; ★抜けた 1 回ぶん(JIFFY は下位だけ見て繰り上がりを足す)
        jr   nz, 00007$
        inc  hl
        inc  (hl)
    00007$:
        call _snd_isr
    00006$:
        ld   hl, (0xFC9E)        ; JIFFY(BIOS が VBLANK で進めていたもの)を自分で進める
        inc  hl
        ld   (0xFC9E), hl
        call _snd_isr            ; 音(以前は BIOS が H.TIMI から呼んでいた)
        call _ras_rearm          ; ★音の後に張り直す。ここでスネアが鳴り始めたら、最初から割込みで出せる
                                 ;   (逆順だと、鳴り始めから次の分割まで 9ms ほど割込みで出ていなかった)
    00001$:
        pop  iy
        pop  ix
        pop  hl
        pop  de
        pop  bc
        pop  af
        ret
    __endasm;
}

/* 走査線割込みだけで回る状態にする(起動時と、CHGMOD の後)。
   R#15=1 / VBLANK 割込み(IE0)を切る / 走査線割込み(IE1)を立てる / 分割を止めて次の合図を張る。
   ★CHGMOD は R#0・R#1・R#19・R#23 を書き戻す(R#23=0。openMSX+実機BIOSで実測)ので、その後に必ず呼ぶ。
   ★BIOS の影(RG0SAV/RG1SAV)も合わせる。CHGMOD などの BIOS が影から書き戻すため(作法 2)。 */
void ras_resume(void) __naked {
    __asm
        di
        ld   hl, #0xF3E0         ; RG1SAV
        res  5, (hl)             ; IE0=0
        ld   a, (hl)
        out  (0x99), a
        ld   a, #0x81
        out  (0x99), a
        dec  hl                  ; RG0SAV(0xF3DF)
        set  4, (hl)             ; IE1=1
        ld   a, (hl)
        out  (0x99), a
        ld   a, #0x80
        out  (0x99), a
        ld   a, #1               ; R#15=1(普段の値)
        out  (0x99), a
        ld   a, #0x8F
        out  (0x99), a
        xor  a
        ld   (_g_ras_n), a
        ld   (_g_ras_i), a
        ld   (_g_vscroll), a     ; ★CHGMOD が R#23=0 にしている
        ld   (_g_ras_vs), a
        out  (0x99), a
        ld   a, #0x80+23
        out  (0x99), a
        call _ras_next
        ei
        ret
    __endasm;
}

/* CHGMOD の前に呼ぶ: 走査線割込み(IE1)を止める。CHGMOD の最中に割込みが来ないようにする。
   ★di のまま戻る。CHGMOD の後に ras_resume() で戻すこと。 */
void ras_pause(void) __naked {
    __asm
        di
        ld   hl, #0xF3DF         ; RG0SAV
        res  4, (hl)             ; IE1=0
        ld   a, (hl)
        out  (0x99), a
        ld   a, #0x80
        out  (0x99), a
        ret
    __endasm;
}

/* 縦スクロール(R#23)を書く。★R#19 は R#23 を引いた値で比較されるので:
   ・g_ras_vs(いま効いている R#23)は**必ず**更新する。分割が残っている間に更新しないでおくと、その後に
     ras_apply が張る行(残りの分割と毎フレームの合図)が古い R#23 で計算されてずれ、合図が「発火できない行」
     へ落ちてフレームを飛ばした(openMSX の 5 面で 5 秒に最大 22 フレーム。2026-10-07)。
   ・次が合図なら R#19 も張り直す。張り直さないと、スクロール量しだいで二度と割込みが来ない。
   ・分割が残っているときは、待っている分割の R#19 は触らない(以前どおり) */
void vdp_set_vscroll(u8 v) __naked {
    (void)v;                     /* A = v(sdcccall 1) */
    __asm
        di
        ld   (_g_vscroll), a
        ld   (_g_ras_vs), a
        out  (0x99), a
        ld   a, #0x80+23
        out  (0x99), a
        ld   a, (_g_ras_tick)
        or   a
        call nz, _ras_next
        ei
        ret
    __endasm;
}

/* H.KEYI(0xFD9A, 5バイトフック)へ JP ras_isr を仕込み、走査線割込みだけで回る状態にする。 */
void raster_init(void) {
    __asm
        di
        ld   a, #0xC3            ; JP opcode
        ld   (0xFD9A), a
        ld   hl, #_ras_isr
        ld   (0xFD9B), hl
    __endasm;
    ras_resume();
}

/* 今フレームの分割数を確定する。★R#19 の仕込みはここではなく毎フレームの合図(ras_rearm)が行う。
   本体はいつ呼んでもよい(表と n を更新するだけ)。 */
void raster_arm(u8 n) {
    if (n > RAS_MAX) n = RAS_MAX;
    if (n == 0) { raster_off(); return; }
    g_ras_n = n;
}

void raster_off(void) {
    if (g_ras_n) {
        /* ★分割はフレームの途中で止まるので、そのとき効いていた帯の設定(スプライト表 B・パターン表の組・MAG)が
           そのまま残る。最終面ではボス帯(表 B＋MAG)の途中で止まると、結果画面やエンディングに
           回転縮小したボスや表 B の残りが出た(実機で報告)。全画面の基準(表 A・パターン表 0x7800・MAG なし)へ戻す。 */
        vdp_wreg(5, SPR_R5_A);
        vdp_wreg(6, 0x0F);
        vdp_wreg(1, (u8)(*(volatile u8 *)0xF3E0 & 0xFE));   /* RG1SAV から MAG を落とした値 */
    }
    __asm di __endasm;
    g_ras_n = 0;
    g_ras_i = 0;
    ras_next();          /* ★待っていた分割の行を、毎フレームの合図へ張り替える */
    __asm ei __endasm;
}
