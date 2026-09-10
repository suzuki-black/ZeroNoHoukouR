/* sys.c — システム起動(turboR 専用の初期化)。
   起動時に一度だけ R800(DRAMモード)へ切替え、以降 Z80命令を高速実行する。
   MSX2+ 以下(version<3)では CHGCPU を呼ばず素通り(安全側=C-BIOSに 0180h は無く暴走するため必須)。
   ★CHGCPU A: bit7=変更ビット / bits1-0=CPUモード(0=Z80,1=R800 ROM,2=R800 DRAM)。
     R800 DRAMモード(0x82)は内蔵RAM(変数/エンティティプール/スタック=0xC000〜)のアクセスが速い。
     ※本作のコードはカートリッジROM(0x4000〜)上=コードフェッチはROM律速のまま(DRAMモードでも不変)。
       コードフェッチも速くするにはホットコードをRAMへ移す別対応が要る(今回は行わない)。
     ※実効は turboR 実機/エミュでのみ検証可能(openMSX C-BIOS=Z80では素通り)。 */
#include "sys.h"
#include "msx.h"

/* R800(DRAMモード)へブースト。前作 boost_r800 を踏襲し、モードを ROM(1)→DRAM(2) へ変更。 */
static void boost_r800(void) {
    __asm
        ld   a, (0x002D)     ; MSX_VER
        cp   #3
        jr   c, 00001$       ; version<3(turboR未満)なら何もしない(C-BIOS等で 0180h を呼ばない)
        ld   a, #0x82        ; CPU=R800(DRAM) + 変更ビット(0x80|2)
        call 0x0180          ; CHGCPU
    00001$:
    __endasm;
}

void sys_init(void) {
    boost_r800();
}
