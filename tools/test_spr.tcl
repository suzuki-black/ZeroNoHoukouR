set b [file dirname [info script]]
after time 8  "screenshot -raw -prefix {} [file join $b .. build spr_a.png]"
after time 11 "screenshot -raw -prefix {} [file join $b .. build spr_b.png] ; exit"
