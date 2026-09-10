set b [file dirname [info script]]
set LOG [open [file join $b .. build bank.log] w]
proc rd {a} { debug read memory $a }
proc snap {t} { global LOG; puts $LOG [format "t=%.1f proof@E000=0x%02X" $t [rd 0xE000]]; flush $LOG }
foreach t {5 6 7 8 9 10 11} { after time $t "snap $t" }
after time 11.5 "screenshot -raw -prefix {} [file join $b .. build bank.png] ; close \$LOG ; exit"
