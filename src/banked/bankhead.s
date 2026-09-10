;; bankhead.s — バンクモジュールの先頭スタブ。リンク先頭に置き、0xA000 に
;; `jp _banked_entry` を確定させる(SDCCの関数配置順に依存せず単一エントリを保証)。
;; トランポリン _bcall が call 0xA000 → ここ → banked_entry → ret でトランポリンへ戻る。
        .module bankhead
        .globl  _banked_entry
        .area   _CODE
        jp      _banked_entry
