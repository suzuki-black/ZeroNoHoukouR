;; ovlhead7.s — 撃沈シーン用 RAM オーバレイ(OVL7_BANK)の先頭ジャンプテーブル。
;; ★スロット番号は ovlhead.s / ovlhead6.s と同じ番地に揃える(常駐のラッパは 0xA000+3*slot を呼ぶ)。
;;   撃沈中に呼ばれないものは空関数。
        .module ovlhead7
        .globl  _ovl_pal_update
        .globl  _ovl_pal_reset
        .globl  _ovl_sink_init
        .globl  _ovl_sink_frame
        .area   _CODE
        jp      stub_ret             ; slot0  curtain_update
        jp      stub_ret             ; slot1  curtain_ring
        jp      stub_ret             ; slot2  curtain_draw
        jp      stub_ret             ; slot3  curtain_collide
        jp      _ovl_pal_update      ; slot4
        jp      _ovl_pal_reset       ; slot5
        jp      stub_ret             ; slot6  curtain_volley
        jp      stub_ret             ; slot7  curtain_present
        jp      stub_ret             ; slot8  crush_bolts
        jp      stub_ret             ; slot9  clear_enemy_bullets
        jp      stub_ret             ; slot10 crush_wave_init
        jp      stub_ret             ; slot11 crush_wave
        jp      stub_ret             ; slot12 crush_wave_off
        jp      stub_zero            ; slot13 crush_wave_y(s16)
        jp      stub_zero            ; slot14 shock_build(u8)
        jp      stub_ret             ; slot15 rot_zoom
        jp      stub_ret             ; slot16 final_init
        jp      stub_zero            ; slot17 final_frame
        jp      stub_ret             ; slot18 final_bgbul
        jp      _ovl_sink_init       ; slot19 (OVL_SLOT_SINK_INIT)
        jp      _ovl_sink_frame      ; slot20 (OVL_SLOT_SINK_FRAME)
stub_zero:
        xor     a
        ld      d, a
        ld      e, a
        ld      l, a
        ld      h, a
stub_ret:
        ret
