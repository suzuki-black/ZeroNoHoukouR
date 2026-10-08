;; hot_pcm.s — 面中の PCM を走査線割込みで 1 サンプルずつ出す(ホット区間 hot_ram の末尾に置く)。
;; ★狙い: 待ち時間に出す方式(pcm_service)では、面中の 8〜9 割の時間に出す機会が無く、スネアが
;;   「ぶっ」と崩れた。鳴っている間だけ、次のサンプルの行でも割込みを起こして出す。
;;   調査と検証は docs/PCM調査_2026-10-07.md(§4.2 が検証 ROM、§6 が設計)。
;; ★2026-10-08 から、PCM はここだけが出す(待ちループからの送出口 pcm_service は外した)。
;; ★仕組み: R#19 に張れる行は 1 つだけ。PCM が鳴っている間は、raster.c の ras_next が決めた
;;   「次の分割か合図の行」(保留)と「次のサンプルの行」を比べて、近い方を張る。
;;   ・サンプルを出す判断は**時刻**(システムタイマと _pcm_due)。
;;   ・割込みが出せない上端の 17 行(表示ライン・カウンタ 245〜261)に落ちるサンプルは、
;;     その場で時刻を待って出す(1 フレームに約 1ms)。
;; ★ここに置く理由: 常駐(bank0-2)に空きが無い。hot_ram は page3 の RAM で常に見える。
;;   撃沈演出が借りるのは 0xC600-0xD9FF だけなので、末尾(0xDA00 以降)なら演出中も壊れない
;;   (Makefile がこのモジュールの番地を検査する)。
;; ★割込みの中から呼ぶ。ei しない。壊すのは AF/BC/DE/HL。

        .module hot_pcm
        .globl  _pcm_active, _pcm_due, _pcm_feed, _pcm_now
        .globl  _g_ras_vs, _g_ras_tick, _g_ras_ttime, _ras_next, _ras_tick_wait
        .area   _CODE

RAS_TICK_LINE = 212             ; raster.c と同じ(毎フレームの合図の行)
LEAD          = 5               ; 保留の行がこれ以内に迫っていたら張らない(下の 00013$)。★3 では足りず最終面で 16ms 止まった:
                                ;   「今の行」は合図の応答の遅れと切り捨てで 1〜2 行少なめに出る
GAP_FIRST     = 245             ; 表示ライン・カウンタがこれ以上だと割込みが出せない(212 行・60Hz)

pv:     .ds     1               ; 保留中の行(R#19 に書く値。分割または合図)
pk:     .ds     1               ; 保留中の種類(0=分割 / 1=合図)
tn:     .ds     2               ; 今の行(合図からの行数)

;; ---- 次の行を張る(raster.c の ras_next から。PCM が鳴っている間だけ) ----
;;   B = 保留する行(R#19 の値)、C = その種類(0=分割 / 1=合図)
;;   ★サンプルの行の割込みでも、呼び手(ras_isr)は ras_next を通してからここへ来る。保留の行をここで
;;     覚えて張り直すと、その間に分割の行が過ぎていたとき 1 フレーム滑る(最初そう書いて合図が 30 秒に
;;     75 回抜けた)。分割と合図の判断(過ぎた分割はすぐ当てる・遅れた合図)は常駐の raster.c に任せる。
_hot_pcm_arm::
        ld      a, b
        ld      (pv), a
        ld      a, c
        ld      (pk), a
00001$:
        ;; --- 出す時刻を過ぎたサンプルを出す ---
        ld      a, (_pcm_active)
        or      a
        jp      z, _ras_next            ; 鳴り終わった → ras_next に張らせる。★保留の行をそのまま張ると、近すぎる
                                        ;   分割を張って 1 フレーム滑った(最後のサンプルを出した直後。1 面で 16ms 止まった)
        call    _pcm_now
        ld      de, (_pcm_due)
        or      a
        sbc     hl, de                  ; 今 - 予定
        bit     7, h
        jr      nz, 00010$              ; まだ予定の前
        ld      a, h
        or      a
        jr      z, 00002$               ; 遅れ 1ms 未満
        call    _pcm_now                ; 1ms 以上遅れ: 予定を今へ合わせ直す
        ld      (_pcm_due), hl
00002$:
        call    _pcm_feed               ; 1 サンプル出して予定を進める(鳴り終わりなら止まる)
        jr      00001$
00010$:
        ;; --- HL = 今 - 予定(負)。あと 1 行(16 カウント)を切っていたら、ここで時刻まで待って出す ---
        ld      de, #16
        add     hl, de
        jr      c, 00001$
        ;; --- HL = 16 - 待ち(負)。待ちの行数 - 1 ≒ (待ち - 16) / 16 ---
        xor     a
        sub     l
        ld      l, a
        sbc     a, a
        sub     h
        ld      h, a
        call    shr4
        ld      a, l
        cp      #3
        jr      nc, 00017$
        ld      l, #3                   ; ★最低 4 行先(下で +1)。合図の割込みが少し遅れて入ると「今の行」を
                                        ;   少なく見積もるので、2 行では張った行がもう過ぎていることがあった。R#19 を書いてから BIOS が S#1 を読むまで約 0.8 行。
                                        ;   近すぎると立ったばかりの FH を落として割込みが消える。
                                        ;   3 行でも 1 面で取りこぼした(実際の余裕は 1 行余り。240 秒で 16ms の止まりが数回)。
00017$:
        push    hl
        ;; --- 今の行 = 合図からの経過 el / 16.25 ≒ (el - el/64) / 16 ---
        call    _pcm_now
        ld      de, (_g_ras_ttime)
        or      a
        sbc     hl, de                  ; el
        push    hl
        ld      b, #6
        call    shr                     ; HL = el / 64
        ex      de, hl
        pop     hl
        or      a
        sbc     hl, de
        call    shr4                    ; HL = 今の行(合図の行=0)
        ld      (tn), hl
        ;; --- 次に張るサンプルの行 = 今 + 待ちの行数(最低 2) ---
        ;;   ★1 回の割込みで 1 サンプル。まとめて出すと、出力はダブルバッファ(15.7kHz の刻みに 1 個)なので
        ;;     同じ刻みの中で後から書いたものに上書きされて捨てられる(最初は 5 行先に固定して 3〜4 個ずつ出していた)
        pop     de
        add     hl, de
        inc     hl                      ; HL = サンプルの行
        ;; --- 保留の行(合図からの行数)と比べる ---
        push    hl
        ld      a, (pk)
        or      a
        ld      hl, #262                ; 合図: 次のフレームの合図
        jr      nz, 00013$
        ld      a, (_g_ras_vs)
        ld      c, a
        ld      a, (pv)
        sub     c                       ; 分割の表示ライン・カウンタ(0〜211)
        ld      l, a
        ld      h, #0
        ld      de, #262 - RAS_TICK_LINE
        add     hl, de                  ; 合図の行からの行数
        ;; ★保留の行が LEAD 行以内に迫っていたら(サンプルを待つ間に近づいた)、張らずにその場で処理させる。
        ;;   そのまま張ると間に合わず(張った直後に BIOS が S#1 を読んで FH を落とす)、1 フレーム滑る。
        ;;   ・分割: ras_next へ戻して当てさせる(最初これが無く、合図が抜けた)
        ;;   ・合図: その時刻まで待って戻る。割込みの中なら ras_isr の遅れの判定がその場で合図の仕事をする
        ;;     (最初は分割だけにしていて、最終面で 212 行目の 2〜3 行手前のサンプルから合図を張ると 17ms 止まった)
00013$:
        push    hl
        ld      de, (tn)
        or      a
        sbc     hl, de
        ld      de, #LEAD
        sbc     hl, de                  ; (前の sbc で借りが出ていれば、どのみち負=迫っている)
        pop     hl
        jr      nc, 00014$
        pop     hl                      ; サンプルの行を捨てる
        ld      a, (pk)
        or      a
        jp      z, _ras_next
        call    _ras_tick_wait
        jr      00090$
00014$:
        ex      de, hl                  ; DE = 保留の行
        pop     hl                      ; HL = サンプルの行
        push    hl
        or      a
        sbc     hl, de
        pop     hl
        jr      nc, 00090$              ; 保留の方が近い(同じ) → 保留を張る
        ;; --- サンプルの行を表示ライン・カウンタへ ---
        ld      de, #RAS_TICK_LINE
        add     hl, de                  ; 212 + 行数
        ld      de, #GAP_FIRST
        or      a
        sbc     hl, de
        jr      c, 00020$               ; 245 未満 → そのまま張れる(下で戻す)
        ld      de, #262 - GAP_FIRST
        or      a
        sbc     hl, de
        jp      c, 00001$               ; 245〜261: 割込みが出せない → その場で時刻を待って出す
        jr      00021$                  ; 262 以上 → 次のフレームの 0〜
00020$:
        ld      de, #GAP_FIRST
        add     hl, de                  ; 元へ(212〜244)
00021$:
        ld      a, #2
        ld      (_g_ras_tick), a        ; 2 = PCM のサンプル
        ld      a, (_g_ras_vs)
        add     a, l
        jr      00091$
00090$:
        ld      a, (pk)
        ld      (_g_ras_tick), a
        ld      a, (pv)
00091$:
        out     (0x99), a
        ld      a, #0x80+19
        out     (0x99), a
        ret

shr4:   ld      b, #4
shr:    srl     h                       ; HL >>= B
        rr      l
        djnz    shr
        ret

