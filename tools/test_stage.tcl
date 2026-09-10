set b [file dirname [info script]]
after time 7   "screenshot -raw -prefix {} [file join $b .. build intro_a.png]"
after time 7.5 "screenshot -raw -prefix {} [file join $b .. build intro_b.png]"
after time 13  "screenshot -raw -prefix {} [file join $b .. build boss_a.png] ; exit"
