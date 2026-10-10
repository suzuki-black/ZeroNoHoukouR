; pcm_prof.s — DEBUG_PROF の版だけ pcm.s の代わりにリンクする「鳴らない PCM」。
;
; ★計測の版は PCM を切ってある(coldsetup.c が g_pcm_hw=0。0xEB00 を計測が使うので素材を置けない)。
;   それでも本体は 130B 近く常駐を食い、計測を足すと常駐が 24KB に収まらなかった。
;   ここでは**入口と変数だけ**を残し、鳴らす処理は全部 ret にする。
; ★_pcm_now(システムタイマを読む)だけは本物。raster.c と hot_pcm.s が時刻を取るのに使い、
;   計測(prof.c)もこれで tick を読む。
; ★_pcm_active は 0 のまま(_pcm_start が何もしない)なので、hot_pcm.s / raster.c は送出へ入らない。

	.module	pcm
	.globl	_pcm_start, _pcm_stop, _pcm_snare, _pcm_active, _pcm_lock, _g_pcm_src, _g_pcm_len

PCM_TIMER  = 0xE6		; システムタイマ下位(1 カウント 3.911us)

	.area	_DATA
_pcm_due::	.ds	2
_pcm_active::	.ds	1
_pcm_lock::	.ds	1
_g_pcm_src::	.ds	2
_g_pcm_len::	.ds	2

	.area	_CODE
_pcm_snare::
_pcm_start::
_pcm_stop::
_pcm_feed::
	ret

; HL = システムタイマ(16bit)。A/H/L だけを壊す
_pcm_now::
	in	a, (PCM_TIMER + 1)
	ld	h, a
	in	a, (PCM_TIMER)
	ld	l, a
	in	a, (PCM_TIMER + 1)
	cp	a, h
	jr	nz, _pcm_now
	ret
