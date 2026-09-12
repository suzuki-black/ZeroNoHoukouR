;; ovlhead.s — RAMオーバレイ(banked/ovl_curtain.c)の先頭ジャンプテーブル。
;; リンク先頭に置き、OVL_ADDR(0xA000) の先頭に「1関数=3バイトの jp」を並べる。
;; 常駐ラッパ(overlay.c)は 0xA000+3*slot を呼ぶ→ここ→本体。
;; SDCCの関数配置順に依存せず単一エントリを保証する(スロット順は overlay.h の OVL_SLOT_* と一致させる)。
        .module ovlhead
        .globl  _ovl_curtain_update
        .globl  _ovl_curtain_ring
        .globl  _ovl_curtain_draw
        .globl  _ovl_curtain_collide
        .globl  _ovl_pal_update
        .globl  _ovl_pal_reset
        .globl  _ovl_curtain_volley
        .globl  _ovl_curtain_present
        .globl  _ovl_crush_bolts
        .globl  _ovl_clear_enemy_bullets
        .globl  _ovl_crush_wave_init
        .globl  _ovl_crush_wave
        .globl  _ovl_crush_wave_off
        .globl  _ovl_crush_wave_y
        .globl  _ovl_shock_build
        .area   _CODE
        jp      _ovl_curtain_update   ; slot0 (OVL_SLOT_CURTAIN_UPDATE)
        jp      _ovl_curtain_ring     ; slot1 (OVL_SLOT_CURTAIN_RING)
        jp      _ovl_curtain_draw     ; slot2 (OVL_SLOT_CURTAIN_DRAW)
        jp      _ovl_curtain_collide  ; slot3 (OVL_SLOT_CURTAIN_COLLIDE)
        jp      _ovl_pal_update       ; slot4 (OVL_SLOT_PAL_UPDATE)
        jp      _ovl_pal_reset        ; slot5 (OVL_SLOT_PAL_RESET)
        jp      _ovl_curtain_volley   ; slot6 (OVL_SLOT_CURTAIN_VOLLEY)
        jp      _ovl_curtain_present  ; slot7 (OVL_SLOT_CURTAIN_PRESENT)
        jp      _ovl_crush_bolts      ; slot8 (OVL_SLOT_CRUSH_BOLTS)
        jp      _ovl_clear_enemy_bullets ; slot9 (OVL_SLOT_CLEAR_EBUL)
        jp      _ovl_crush_wave_init  ; slot10 (OVL_SLOT_WAVE_INIT)
        jp      _ovl_crush_wave       ; slot11 (OVL_SLOT_WAVE)
        jp      _ovl_crush_wave_off   ; slot12 (OVL_SLOT_WAVE_OFF)
        jp      _ovl_crush_wave_y     ; slot13 (OVL_SLOT_WAVE_Y)
        jp      _ovl_shock_build      ; slot14 (OVL_SLOT_SHOCK_BUILD)
