set b [file dirname [info script]]
set LOG [open [file join $b .. build snd.log] w]
# PSG debuggable 名を記録(環境依存の確認用)
catch { puts $LOG "debuggables: [debug list]" }
proc rd {a} { debug read memory $a }
proc snap {t} {
  global LOG
  set ticks [expr {[rd 0xC00D]*256 + [rd 0xC00C]}]
  set act [rd 0xC00E]
  # PSG 音量レジスタ R8/R9/R10 を読む(名前は環境依存: 失敗しても続行)
  set v8 "-"; set v9 "-"; set v10 "-"
  catch { set v8  [debug read "PSG regs" 8] }
  catch { set v9  [debug read "PSG regs" 9] }
  catch { set v10 [debug read "PSG regs" 10] }
  if {$act > 0} { puts $LOG [format "t=%.2f ticks=%d ACTIVE=%d psgVol=%s,%s,%s" $t $ticks $act $v8 $v9 $v10] }
}
# 0.05s刻みで t=5.5..9.5 を密にサンプル(BOOM 28f/SHOT 8f を取りこぼさない)
for {set i 0} {$i < 80} {incr i} {
  set tt [expr {5.5 + $i*0.05}]
  after time $tt "snap $tt"
}
after time 9.6 "close \$LOG ; exit"
