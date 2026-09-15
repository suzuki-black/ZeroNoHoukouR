;; ovlhead6.s — 最終面用 RAM オーバレイ(OVL6_BANK)の先頭ジャンプテーブル。
;; ★スロット番号は通常面のオーバレイ(ovlhead.s / overlay.h の OVL_SLOT_*)と**同じ番地**に揃える。
;;   常駐のラッパは面を区別せず 0xA000+3*slot を呼ぶので、最終面で使わない機能は安全な空関数にしておく
;;   (弾幕/クラッシュ/衝撃波は最終面では呼ばれない設計だが、万一呼ばれても何もしないで戻る)。
        .module ovlhead6
        .globl  _ovl_pal_update
        .globl  _ovl_pal_reset
        .globl  _ovl_rot_zoom
        .globl  _ovl_final_init
        .globl  _ovl_final_frame
        .globl  _ovl_final_bgbul
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
        jp      _ovl_rot_zoom        ; slot15
        jp      _ovl_final_init      ; slot16 (OVL_SLOT_FINAL_INIT)
        jp      _ovl_final_frame     ; slot17 (OVL_SLOT_FINAL_FRAME)
        jp      _ovl_final_bgbul     ; slot18 (OVL_SLOT_FINAL_BGBUL)
stub_zero:
        xor     a
        ld      d, a
        ld      e, a
        ld      l, a
        ld      h, a
stub_ret:
        ret
