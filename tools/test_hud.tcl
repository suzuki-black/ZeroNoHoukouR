set b [file dirname [info script]]
after time 3  "screenshot -raw -prefix {} [file join $b .. build hud_sea.png]"
after time 9  "screenshot -raw -prefix {} [file join $b .. build hud_ship.png] ; exit"
