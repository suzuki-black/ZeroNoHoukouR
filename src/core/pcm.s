; pcm.s — turbo R 内蔵 PCM の非同期再生(1 声)。
;
; ★狙いは「ゲームを止めずに鳴らす」。鳴り終わるまで待つのではなく、**ゲームが待っている合間に
;   1 サンプルずつ出す**。止まるのは PCM の長さではなく、待ちループを 1 周するたびの数十サイクルだけ。
;
; ポート
;   A4h 書込: サンプル値(8bit unsigned、中心 0x80)
;   A5h 書込: bit0 BUFF(1=次の刻みで D/A へ) / bit1 MUTE(★0 にすると PSG も FM も消える)
;             bit2 FILT / bit3 SEL(0=D/A)
;   E6h/E7h : システムタイマ(1 カウント 3.911us)。**時刻はここで取る**
;
; ★間隔は「前回の予定 + PCM_PERIOD」で進める(今の時刻からではない)。遅れても次で詰めて取り返すので
;   再生速度が伸びない。A4h の 2bit カウンタを見る方式は、書くとカウンタが 0 に戻るうえ 254us で
;   一周するため、出遅れた分がそのまま間隔に足されて音が伸びる。
; ★1ms 以上遅れたら取り返しを諦めて予定を今に合わせ直す(音は伸びるが、途切れたままにはならない)。
;
; ■ なぜ自己書き換えを使わないか
;   先行実装は送出口の命令を書き換えて「鳴っていないときは JR 1 命令」にしているが、あれは
;   **プログラム全体を RAM へ展開して動かしている**から成立する。本作の常駐は
;   「カート ROM」と「RAM 複製」の 2 つの姿を持ち、どちらが見えているかが場面で変わるので、
;   コードの書き換えは成立しない。状態は必ず _DATA(RAM) に置き、送出口は素直に call する。
;   ★送出口は元から時間を捨てている待ちループなので、call の数十サイクルは実質ただ。
;
; ■ 1 声でよい理由
;   面中はスネア、タイトルと結果画面は叫び。**同時に鳴る場面が無い**(クラッシュは BGM に入れない)。
;   声を増やすと足し算と休符の処理が要るが、要らないので入れない。

	.module	pcm
	.globl	_pcm_service, _pcm_start, _pcm_stop, _pcm_snare, _pcm_active, _g_pcm_src, _g_pcm_len, _g_pcm

PCM_DATA   = 0xA4
PCM_CTRL   = 0xA5
PCM_TIMER  = 0xE6		; システムタイマ下位(1 カウント 3.911us)
PCM_PERIOD = 32			; 32 x 3.911us = 125.1us = 7993Hz

	.area	_DATA
; ★状態は必ず RAM(_DATA)。crt0 が 0 クリアするので初期値は持てない(持たせない作りにしてある)
pcm_p:		.ds	2	; いま読んでいる位置
pcm_n:		.ds	2	; 残りバイト数
pcm_due:	.ds	2	; 次に出す予定の時刻(システムタイマ 16bit)
_pcm_active::	.ds	1	; 0 以外 = 再生中。鳴り終わりを待つ側が見る
_g_pcm_src::	.ds	2	; C が入れる: サンプルの先頭(RAM 番地)
_g_pcm_len::	.ds	2	; 同: 長さ(バイト)
; ★_g_pcm(1=使ってよい)は **gamestate.c** にある。既定を 1 にしたいが、crt0 は _DATA を
;   0 クリアするので、ここ(_DATA)に置くと既定 OFF になってしまう。

	.area	_CODE

; ---------------------------------------------------------------------------
; void pcm_start(void)   g_pcm_src / g_pcm_len に入れてから呼ぶ
;   ★引数を取らないのは、スタック越しの受け渡しが常駐では高くつくため
;   前の音が鳴っていても、そのまま新しい音へ差し替える(スネアの連打はこれで切り替わる)
; ---------------------------------------------------------------------------
; ---------------------------------------------------------------------------
; void pcm_snare(void) — 面中のスネア専用。番地と長さを**ここで持つ**。
;   ★ISR(bgm_drum)から呼ぶので、呼ぶ側を 1 命令にしたい。g_pcm_src/g_pcm_len を
;     毎回積むと常駐が 20B ほど増えるため、固定のものは asm 側に持たせる。
;   ★番地は常駐 RAM の高位フリー帯(pcm.h の PCM_SNARE_ADDR と合わせること)。
; ---------------------------------------------------------------------------
PCM_SNARE_ADDR = 0xF000
PCM_SNARE_LEN  = 640		; 80ms @ 7993Hz
_pcm_snare::
	ld	hl, #PCM_SNARE_LEN
	ld	(_g_pcm_len), hl
	ld	hl, #PCM_SNARE_ADDR
	ld	(_g_pcm_src), hl
	; ↓ そのまま再生開始へ

_pcm_start::
	ld	a, (_g_pcm)		; 使えない機械(検出で落ちた/設定で OFF)では何もしない
	or	a, a
	ret	z
	ld	hl, (_g_pcm_len)
	ld	a, h
	or	a, l
	ret	z
	di
	ld	(pcm_n), hl
	ld	hl, (_g_pcm_src)
	ld	(pcm_p), hl
	ld	a, #0x03		; BUFF=1, MUTE 解除(bit1=1), D/A
	out	(PCM_CTRL), a
	call	pcm_now			; 最初の 1 つはすぐ出す
	ld	(pcm_due), hl
	ld	a, #1
	ld	(_pcm_active), a
	ei
	jr	pcm_feed

; ---- void pcm_stop(void) ----
_pcm_stop::
	xor	a, a
	ld	(_pcm_active), a
	ld	a, #0x80		; 無音(中心)へ戻す
	out	(PCM_DATA), a
	ret

; 今の時刻(16bit)を HL へ。下位(E6h)から読むこと
pcm_now:
	in	a, (PCM_TIMER)
	ld	l, a
	in	a, (PCM_TIMER + 1)
	ld	h, a
	ret

; ---------------------------------------------------------------------------
; void pcm_service(void) — 送出口。ゲームの待ちループから呼ぶ。
;   予定の時刻を過ぎていたら次の 1 バイトを出す。
;   ★壊すのは A とフラグだけ(HL/DE は中で退避する)。鳴っていなければ 3 命令で戻る。
; ---------------------------------------------------------------------------
_pcm_service::
	ld	a, (_pcm_active)
	or	a, a
	ret	z
	push	hl
	push	de
	call	pcm_now			; HL = 今
	ld	de, (pcm_due)
	or	a, a
	sbc	hl, de			; HL = 今 - 予定
	jr	z, pcm_svc_go
	jp	m, pcm_svc_no		; まだ予定より前
	ld	a, h			; 1ms(256 カウント)以上の遅れ: 予定を今に合わせ直す
	or	a, a
	jr	z, pcm_svc_go
	call	pcm_now
	ld	(pcm_due), hl
pcm_svc_go:
	pop	de
	pop	hl
	jr	pcm_feed
pcm_svc_no:
	pop	de
	pop	hl
	ret

; ---- 次の 1 バイトを出す(時刻の判定は呼ぶ側が済ませている)。壊すのは A とフラグだけ ----
pcm_feed:
	push	hl
	ld	hl, (pcm_p)
	ld	a, (hl)
	out	(PCM_DATA), a		; BUFF=1 なので次の 15.7kHz の刻みで D/A へ
	inc	hl
	ld	(pcm_p), hl
	ld	hl, (pcm_due)		; 次の予定は「今」ではなく「前の予定 + 間隔」
	ld	a, l			; (遅れた分を次で詰めるので再生速度が伸びない)
	add	a, #PCM_PERIOD
	ld	l, a
	jr	nc, pcm_due_ok
	inc	h
pcm_due_ok:
	ld	(pcm_due), hl
	ld	hl, (pcm_n)
	dec	hl
	ld	(pcm_n), hl
	ld	a, h
	or	a, l
	jr	z, pcm_last
	pop	hl
	ret
pcm_last:				; 鳴り終わった(全部守る。描画の途中から呼ばれる)
	push	bc
	push	de
	call	_pcm_stop
	pop	de
	pop	bc
	pop	hl
	ret
