# 海イントロ共通BGM(track7)の音声を録音。タイトル→SPACE開始→カード/ファンファーレ後の海フェーズを録る。
set b [file dirname [info script]]
set wav [file join $b .. build intro_bgm.wav]
after time 7.0  { keymatrixdown 8 0x01; after time 0.2 { keymatrixup 8 0x01 } }
after time 16.0 { soundlog start $wav }
after time 40.0 { soundlog stop; exit }
