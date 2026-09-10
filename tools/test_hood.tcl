set b [file dirname [info script]]
proc shot {f} { global b; screenshot -raw -prefix {} [file join $b .. build $f] }
set throttle off
after time 6.5 { poke 0xCCD0 2; poke 0xCCD2 1 }
after time 7.0 { keymatrixdown 8 0x01; after time 0.2 { keymatrixup 8 0x01 } }
after time 190 { shot hd_a.png }
after time 215 { shot hd_b.png }
after time 240 { shot hd_c.png }
after time 265 { shot hd_d.png; exit }
