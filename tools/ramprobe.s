;; ramprobe.s — ★実機検証: MSX から ESERAMair のカート RAM へ書けるか。
;;   書ければ「ゲームが測定値をカート RAM へ書く → PC が curl で読む」という**実機テレメトリ**が作れる。
;;   (ハイスコアの保存も同じ仕組みで開く)
;;
;;   やること:
;;     1. ASCII8 のバンクレジスタ 0x7800 に 31 を書き、**バンク31 を 0xA000 窓**へ出す
;;        (バンク31 は本編でも未使用。レジスタは 0x6000/0x6800/0x7000/0x7800 の4本)
;;     2. 0xA000 へ署名 8B を書き、読み戻して一致するか見る
;;     3. 一致なら 0xA008 の 16bit カウンタを VBLANK ごとに +1 し続ける
;;   見かた:
;;     画面   … RAM WRITE OK / FAILED
;;     PC から… curl "http://eseram.local:8080/ram?start=0x3E000&size=16" | xxd
;;              → "ZEROPROB" が見え、カウンタが読むたびに増えていれば**生きた経路**
;;   ★書けなければ 0xA000 は書込み前の値のまま読めるので FAILED になる。
;;   ★ESERAMair の DIPSW「WriteProtect」は OFF(手前)にしておくこと。

        .area   _CODE


        .db     0x41, 0x42          ; "AB"
        .dw     init                ; INIT
        .dw     0, 0, 0             ; STATEMENT / DEVICE / TEXT
        .dw     0, 0, 0             ; 予備

init:
        call    0x006C              ; INITXT (SCREEN 0)
        ld      a, #31
        ld      (0x7800), a         ; バンク31 → 0xA000-0xBFFF
        ld      hl, #sig            ; 署名を書く
        ld      de, #0xA000
        ld      bc, #8
        ldir
        ld      hl, #sig            ; 読み戻して比較
        ld      de, #0xA000
        ld      b, #8
vfy:
        ld      a, (de)
        cp      (hl)
        jr      nz, bad
        inc     hl
        inc     de
        djnz    vfy
        ld      hl, #m_ok
        call    puts
        jr      spin
bad:
        ld      hl, #m_ng
        call    puts
spin:
        ld      hl, (0xA008)        ; カウンタを回す(curl で変化を見る)
        inc     hl
        ld      (0xA008), hl
        halt                        ; 1/60 待つ
        jr      spin

puts:
        ld      a, (hl)
        or      a
        ret     z
        inc     hl
        push    hl
        call    0x00A2              ; CHPUT
        pop     hl
        jr      puts

sig:
        .ascii  "ZEROPROB"
m_ok:
        .ascii  "RAM WRITE OK - COUNTER RUNS"
        .db     0
m_ng:
        .ascii  "RAM WRITE FAILED - READBACK NG"
        .db     0
