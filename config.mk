# ビルド可変値の正本(唯一の真実)。Makefile と IDE 設定はここを読む。
# ── turboR 専用エンジン(MSX2+ 互換は捨てる)。ただし SDCC の生成命令は Z80 命令。
#    turboR では起動時に sys_init() が R800(ROMモード)へ切替えて高速実行する。
#    r800 バックエンド(-mr800)は SDCC 4.6 に存在するが実験的(MULUB等)。当面 z80 で通す。

TARGET   = z80          # z80(既定/実績) | r800(実験的。切替は sys_init の boost で行う)
CODELOC  = 0x4010       # "AB"+INIT ヘッダ(0x4000-0x400F=16B)直後にコードを置く
DATALOC  = 0xC000       # RAM(page3)先頭。BSS/初期化データはここから上へ

PROFILE  = debug        # debug | release
OPT_DEBUG   = --max-allocs-per-node 3000
OPT_RELEASE = --opt-code-size --max-allocs-per-node 9000

# ── MegaROM(ASCII8, 128KB=16bank×8KB)レイアウト(rompack が強制) ──
#   bank0-2 : 常駐コード(0x4010-0x9FFF, 上限 24KB)          ← rompack が超過をビルドエラーに
#   bank3   : 0xA000-0xBFFF スワップ窓(データ先読み/バンクコール用。ROMは既定 0xFF)
#   bank4-15: 冷たいコード(bcall)＋データ(艦/発砲/BGM/文字列/スプライト)
ROM_SIZE   = 0x20000    # 128KB
