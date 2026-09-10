proc dump {t} { global OUT; set s "t=$t"; foreach r {0 1 2 3 6 8 9 10} { set v 0; catch { set v [debug read "PSG regs" $r] }; append s " R$r=$v" }; puts $OUT $s; flush $OUT }
set OUT [open "build/bgm_psg.txt" w]
after time 8.0  "dump 8.0"
after time 8.2  "dump 8.2"
after time 8.5  "dump 8.5"
after time 9.0  "dump 9.0"
after time 9.6  "dump 9.6 ; close \$OUT ; exit"
