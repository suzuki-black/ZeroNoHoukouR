;; ovlhead.s — RAMオーバレイ(banked/ovl_curtain.c)の先頭ジャンプテーブル。
;; リンク先頭に置き、OVL_ADDR(0xA000) の先頭に「1関数=3バイトの jp」を並べる。
;; 常駐ラッパ(overlay.c)は 0xA000+3*slot を呼ぶ→ここ→本体。
;; SDCCの関数配置順に依存せず単一エントリを保証する(スロット順は overlay.h の OVL_SLOT_* と一致させる)。
        .module ovlhead
        .globl  _ovl_curtain_update
        .globl  _ovl_curtain_ring
        .globl  _ovl_curtain_draw
        .area   _CODE
        jp      _ovl_curtain_update   ; slot0 (OVL_SLOT_CURTAIN_UPDATE)
        jp      _ovl_curtain_ring     ; slot1 (OVL_SLOT_CURTAIN_RING)
        jp      _ovl_curtain_draw     ; slot2 (OVL_SLOT_CURTAIN_DRAW)
