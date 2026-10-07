; im2pcm.s — 検証ROM: IM2 + 走査線割込み(4行ごと)で turboR PCM を 1 サンプルずつ出す。
;   ・R800 / SCREEN5(212行) / R#15 は普段 1(S#1)に固定 / VBLANK 割込み(IE0)は切る
;   ・割込みが出せない上端の区間(カウンタ 245〜261 相当)は、直前の割込みの中で
;     システムタイマ(E6h)で 4 行ぶん(65 カウント)ずつ待って出す
;   ・メインは VDP コマンド(HMMM)を回し続け、合間に CPU 仕事をしてカウンタを進める
;   ・mode(0xE000): 0=割込み無し / 1=4行ごとの割込みで PCM を出す(Tcl から書き換える)
	.module	im2pcm
	.area	ROMA (ABS)

MODE	= 0xE000
CURMODE	= 0xE001
CURL	= 0xE002
PTR	= 0xE004
CNT	= 0xE010	; 32bit: CPU 仕事の回数
NINT	= 0xE014	; 16bit: 割込み回数
TABLE	= 0xE100	; IM2 表 257B (値 0xE2)
VEC	= 0xE2E2	; jp isr
BUF	= 0x5000	; 1024B (スネア128 + 無音)

	.org	0x4000
	.db	0x41, 0x42
	.dw	init
	.dw	0, 0, 0, 0, 0, 0

	.org	0x4010
init:
	di
	ld	a, #0x81		; R800 ROM モード
	call	0x0180			; CHGCPU
	ld	a, #5
	call	0x005F			; CHGMOD(SCREEN5)
	di
	xor	a
	ld	(MODE), a
	ld	(CURMODE), a
	ld	(CURL), a
	ld	hl, #0
	ld	(CNT), hl
	ld	(CNT+2), hl
	ld	(NINT), hl
	ld	hl, #BUF
	ld	(PTR), hl
	; IM2 表
	ld	hl, #TABLE
	ld	(hl), #0xE2
	ld	de, #TABLE+1
	ld	bc, #256
	ldir
	ld	a, #0xC3
	ld	(VEC), a
	ld	hl, #isr
	ld	(VEC+1), hl
	ld	a, #0xE1
	ld	i, a
	im	2
	; VDP: R#15=1 / R#19=0 / R#23=0 / IE0 off
	ld	a, #1
	out	(0x99), a
	ld	a, #0x8F
	out	(0x99), a
	xor	a
	out	(0x99), a
	ld	a, #0x80+19
	out	(0x99), a
	xor	a
	out	(0x99), a
	ld	a, #0x80+23
	out	(0x99), a
	ld	a, (0xF3E0)		; RG1SAV
	and	#0xDF			; IE0=0
	ld	(0xF3E0), a
	out	(0x99), a
	ld	a, #0x81
	out	(0x99), a
	; PCM: A4h=80h / A5h=03h(ダブルバッファ + 音あり)
	ld	a, #0x80
	out	(0xA4), a
	ld	a, #0x03
	out	(0xA5), a
	ld	hl, #0xA55A		; 目印(初期化完了)
	ld	(0xE020), hl
	ei

main:
	; mode が変わったら IE1 を切替
	ld	a, (MODE)
	ld	hl, #CURMODE
	cp	(hl)
	call	nz, apply_mode
	; HMMM (0,0)-(256x64) → (0,128)
	di
	ld	a, #32
	out	(0x99), a
	ld	a, #0x80+17
	out	(0x99), a
	ei
	ld	hl, #hmmm
	ld	c, #0x9B
	ld	b, #15
	otir
poll:
	ld	b, #50			; CPU 仕事(固定量)
cw:	ld	hl, (CNT)
	inc	hl
	ld	(CNT), hl
	ld	a, h
	or	l
	jr	nz, cw1
	ld	hl, (CNT+2)
	inc	hl
	ld	(CNT+2), hl
cw1:	djnz	cw
	; CE を見る(R#15 を一瞬 2 に)
	di
	ld	a, #2
	out	(0x99), a
	ld	a, #0x8F
	out	(0x99), a
	in	a, (0x99)
	ld	e, a
	ld	a, #1
	out	(0x99), a
	ld	a, #0x8F
	out	(0x99), a
	ei
	bit	0, e
	jr	nz, poll
	jr	main

apply_mode:
	ld	(hl), a
	di
	ld	b, a
	xor	a
	ld	(CURL), a
	out	(0x99), a		; R#19 = 0
	ld	a, #0x80+19
	out	(0x99), a
	ld	a, (0xF3DF)		; RG0SAV
	and	#0xEF
	bit	0, b
	jr	z, am1
	or	#0x10			; IE1=1
am1:	ld	(0xF3DF), a
	out	(0x99), a
	ld	a, #0x80
	out	(0x99), a
	in	a, (0x99)		; FH を一度クリア
	ei
	ret

hmmm:	.dw	0, 0, 0, 128, 256, 64
	.db	0, 0, 0xD0

; ---- 1 サンプル出す(HL と B を守る) ----
emit:
	push	hl
	ld	hl, (PTR)
	ld	a, (hl)
	out	(0xA4), a
	inc	hl
	ld	a, h
	cp	#>(BUF+1024)
	jr	nz, em1
	ld	h, #>BUF
em1:	ld	(PTR), hl
	pop	hl
	ret

; ---- 割込み(IM2) ----
isr:
	push	af
	push	bc
	push	de
	push	hl
	in	a, (0x99)		; S#1(R#15=1)。読むと FH が落ちる
	rrca
	jr	nc, isr_done
	call	emit
	ld	hl, (NINT)
	inc	hl
	ld	(NINT), hl
	ld	a, (CURL)
	add	a, #4
	ld	c, a
	cp	#245
	jr	c, setl
	; 上端の割込みが出せない区間: 4 行(65 カウント)ずつ待って出す
	in	a, (0xE6)
	ld	b, a
	ld	h, #0
	ld	l, c
burst:
	in	a, (0xE6)
	sub	b
	cp	#65
	jr	c, burst
	ld	a, b
	add	a, #65
	ld	b, a
	call	emit
	ld	de, #4
	add	hl, de
	ld	de, #262
	or	a
	sbc	hl, de
	jr	nc, wrapped
	add	hl, de
	jr	burst
wrapped:
	ld	c, l
setl:
	ld	a, c
	ld	(CURL), a
	out	(0x99), a
	ld	a, #0x80+19
	out	(0x99), a
isr_done:
	pop	hl
	pop	de
	pop	bc
	pop	af
	ei
	reti

	.org	BUF
	.include "snare_buf.inc"

	.org	0x7FFF
	.db	0
