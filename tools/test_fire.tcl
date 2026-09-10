set b [file dirname [info script]]
after time 8.5 "screenshot -raw -prefix {} [file join $b .. build fire_a.png] ; exit"
