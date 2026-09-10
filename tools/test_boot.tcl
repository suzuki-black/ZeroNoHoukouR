# openMSX headless 起動検証: 十分走らせてから PNG スクショ→終了。
set basedir [file dirname [info script]]
set outpng  [file join $basedir ".." "build" "boot.png"]
after time 6 "screenshot -raw -prefix {} $outpng ; exit"
