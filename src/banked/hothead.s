;; hothead.s — RAM実行モジュール(banked/hot.c)の先頭ジャンプテーブル。リンク先頭に置き、
;; hot_ram[] の先頭に「1関数=3バイトの jp」を並べる。番地 hot_ram+3*slot が各関数の固定入口。
;; 常駐ラッパ(hotcode.c)は hot_ram+3*slot を関数として呼ぶ→ここ→本体。
;; SDCCの関数配置順に依存せず単一エントリを保証する(スロット順は hotcode.h の HOT_SLOT_* と一致させる)。
        .module hothead
        .globl  _hot_aa_update
        .globl  _hot_aa_collide
        .globl  _hot_ent_update_all
        .globl  _hot_hb_init
        .globl  _hot_hb_fan
        .globl  _hot_hb_update
        .globl  _hot_hb_clear
        .area   _CODE
        jp      _hot_aa_update            ; slot0 (HOT_SLOT_AA_UPD)
        jp      _hot_aa_collide           ; slot1 (HOT_SLOT_AA_COL)
        jp      _hot_ent_update_all       ; slot2 (HOT_SLOT_UPDATE)
        jp      _hot_hb_init              ; slot3 (HOT_SLOT_HB_INIT)   中ボスの背景弾
        jp      _hot_hb_fan               ; slot4 (HOT_SLOT_HB_ADD)
        jp      _hot_hb_update            ; slot5 (HOT_SLOT_HB_UPDATE)
        jp      _hot_hb_clear             ; slot6 (HOT_SLOT_HB_CLEAR)
