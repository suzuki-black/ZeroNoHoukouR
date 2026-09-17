;; ovlhead8.s — 中ボス用 RAM オーバレイ(OVL8_BANK)の先頭ジャンプテーブル。
;; ★通常面のオーバレイ(ovlhead.s)から主砲の弾幕(slot0-3,6,7)を抜き、中ボス(slot22/23)を足したもの。
;;   中ボスは海の区間にしか出ない＝弾幕は撃たれないので空関数でよい。
;; ★スロット番号は ovlhead.s と同じ番地に揃える(常駐のラッパは 0xA000+3*slot を呼ぶ)。
        .module ovlhead8
        .globl  _ovl_pal_update
        .globl  _ovl_pal_reset
        .globl  _ovl_crush_bolts
        .globl  _ovl_clear_enemy_bullets
        .globl  _ovl_crush_wave_init
        .globl  _ovl_crush_wave
        .globl  _ovl_crush_wave_off
        .globl  _ovl_crush_wave_y
        .globl  _ovl_shock_build
        .globl  _ovl_rot_zoom
        .globl  _ovl_power_frame
        .globl  _ovl_mb_init
        .globl  _ovl_mb_frame
        .area   _CODE
        jp      stub_ret             ; slot0  curtain_update
        jp      stub_ret             ; slot1  curtain_ring
        jp      stub_ret             ; slot2  curtain_draw
        jp      stub_ret             ; slot3  curtain_collide
        jp      _ovl_pal_update      ; slot4
        jp      _ovl_pal_reset       ; slot5
        jp      stub_ret             ; slot6  curtain_volley
        jp      stub_ret             ; slot7  curtain_present
        jp      _ovl_crush_bolts     ; slot8
        jp      _ovl_clear_enemy_bullets ; slot9
        jp      _ovl_crush_wave_init ; slot10
        jp      _ovl_crush_wave      ; slot11
        jp      _ovl_crush_wave_off  ; slot12
        jp      _ovl_crush_wave_y    ; slot13
        jp      _ovl_shock_build     ; slot14
        jp      _ovl_rot_zoom        ; slot15
        jp      stub_ret             ; slot16 final_init
        jp      stub_zero            ; slot17 final_frame
        jp      stub_ret             ; slot18 final_bgbul
        jp      stub_ret             ; slot19 sink_init
        jp      stub_zero            ; slot20 sink_frame
        jp      _ovl_power_frame     ; slot21
        jp      _ovl_mb_init         ; slot22 (OVL_SLOT_MB_INIT)
        jp      _ovl_mb_frame        ; slot23 (OVL_SLOT_MB_FRAME)
stub_zero:
        xor     a
        ld      d, a
        ld      e, a
        ld      l, a
        ld      h, a
stub_ret:
        ret
