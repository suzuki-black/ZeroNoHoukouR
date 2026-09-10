set b [file dirname [info script]]
set LOG [open [file join $b .. build player.log] w]
proc rd {a} { debug read memory $a }
proc snap {t} { global LOG; puts $LOG [format "%-8s px=%d py=%d kills=%d hit=%d" $t [rd 0xC1C2] [rd 0xC1C3] [rd 0xC1C0] [rd 0xC1C1]]; flush $LOG }
# 中央で静止したまま連射(移動なし)。戦闘機が弾列/自機に重なると kills/hit が増える。
after time 6.3 "keymatrixdown 8 0x01"
foreach t {7 7.5 8 8.5 9 9.5} { after time $t "snap t=$t" }
after time 9.6 "keymatrixup 8 0x01 ; close \$LOG ; exit"
