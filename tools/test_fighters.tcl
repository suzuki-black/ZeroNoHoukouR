# 海イントロ検証: タイトル→SPACE開始→ゲーム中はSPACE(発砲)+方向で弾を撮る。アドレス非依存。
set b [file dirname [info script]]
proc shot {f} { global b; screenshot -raw -prefix {} [file join $b .. build $f] }
# 起動→開始
after time 7.0  { shot fgt_title.png; keymatrixdown 8 0x01; after time 0.2 { keymatrixup 8 0x01 } }
# ゲーム中: SPACE(0x01=発砲)+LEFT(0x10=移動)を押しっぱなしにして弾を出す
after time 22   { keymatrixdown 8 0x11 }
after time 30   { shot fgt_a.png }
after time 33   { keymatrixup 8 0x10; keymatrixdown 8 0x80 }   ;# 右へ切替(発砲継続)
after time 40   { shot fgt_b.png }
after time 48   { shot fgt_c.png }
after time 56   { shot fgt_d.png; keymatrixup 8 0xFF; exit }
