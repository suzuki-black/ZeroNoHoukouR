;; ovlhead13.s — 面ごとの撃沈演出を置くオーバレイ(OVL13_BANK)の先頭ジャンプテーブル。
;; ★スロット番号は ovlhead.s / ovlhead7.s と同じ番地に揃える(常駐のラッパは 0xA000+3*slot を呼ぶ)。
;;   撃沈オーバレイ(ovl7)が 8KB に収まらなくなったので、2面以降の演出はこちらへ置く。
;;   ここが読まれているのは演出の数秒間だけで、他のスロットは呼ばれない。
        .module ovlhead13
        .globl  _ovl_crack
        .globl  _ovl_spin
        .area   _CODE
        jp      stub_ret             ; slot0
        jp      stub_ret             ; slot1
        jp      stub_ret             ; slot2
        jp      stub_ret             ; slot3
        jp      stub_ret             ; slot4
        jp      stub_ret             ; slot5
        jp      stub_ret             ; slot6
        jp      stub_ret             ; slot7
        jp      stub_ret             ; slot8
        jp      stub_ret             ; slot9
        jp      stub_ret             ; slot10
        jp      stub_ret             ; slot11
        jp      stub_ret             ; slot12
        jp      stub_zero            ; slot13
        jp      stub_zero            ; slot14
        jp      _ovl_crack           ; slot15 ★2面「縦に裂けて左右へ開く」(OVL_SLOT_CRACK)
        jp      stub_ret             ; slot16 (4面の演出を置くならここ)
        jp      stub_zero            ; slot17
        jp      stub_ret             ; slot18 (5面の演出を置くならここ)
        jp      stub_ret             ; slot19
        jp      stub_ret             ; slot20
        jp      stub_ret             ; slot21
        jp      _ovl_spin            ; slot22 ★3面「きりもみ急上昇」(OVL_SLOT_SPIN)
stub_zero:
        xor     a
        ld      d, a
        ld      e, a
        ld      l, a
        ld      h, a
stub_ret:
        ret
