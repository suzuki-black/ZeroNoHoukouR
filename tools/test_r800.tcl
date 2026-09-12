# test_r800.tcl — openMSX で R800 固有命令を検証するときの定型。
#   openmsx -machine CBIOS_turboR -carta GAME.ROM -romtype ASCII8 -script tools/test_r800.tcl
#
# ★CBIOS_turboR は turboR ハード(R800+S1990)だが BIOS が C-BIOS なので MSX_VER(0x002D)=2 を返す。
#   こちらの boost_r800 は version<3 で CHGCPU(0x0180)を呼ばない(C-BIOS に 0x0180 は無いので正しい)
#   ため、放っておくと **Z80 のまま**で R800 命令は黙って無視される。
# ★S1990 の reg6 bit5(0x20) が 1=Z80 / 0=R800(出典: openMSX src/MSXS1990.cc)。
#   ROM が何度もモードを触る可能性があるので periodic に押し続ける。
# ★★タイミングはこのモードでは実機と合わない(ROM/RAM 比が 1.3 倍しか出ない。実機は 3.84 倍)。
#   命令の「意味」の検証にだけ使うこと。速度は実機の自己診断画面で測る。
proc force_r800 {} {
    catch { debug write "S1990 regs" 6 0x00 }
    after time 0.05 force_r800
}
after time 0.2 force_r800

# ★ヘッドレスの openMSX では puts はコンソールへ行き stdout に出ない。ファイルへ書く。
#   出力先は環境変数 R800_OUT(未設定なら ./r800_test.out)。
set ::out [expr {[info exists ::env(R800_OUT)] ? $::env(R800_OUT) : "r800_test.out"}]

# DEBUG_PROF の自己診断が置く MULUB/MULUW の結果(prof.h PROF_RAM_ADDR + 0xB0)
after time 14 {
    set f [open $::out w]
    puts $f "MULUW HL(low)=[format %02X%02X [debug read memory 0xEBB1] [debug read memory 0xEBB0]] DE(high)=[format %02X%02X [debug read memory 0xEBB3] [debug read memory 0xEBB2]]"
    puts $f "MULUB HL=[format %02X%02X [debug read memory 0xEBB5] [debug read memory 0xEBB4]] A=[format %02X [debug read memory 0xEBB6]]"
    puts $f "tick HW=[expr {[debug read memory 0xEBB9]*256+[debug read memory 0xEBB8]}] SW=[expr {[debug read memory 0xEBBB]*256+[debug read memory 0xEBBA]}]"
    puts $f "expect: MULUW 1234*5678 = 0x006A_E9BC / MULUB 200*3 = 0x0258"
    close $f
    exit
}
