# ============================================================================
#  BattleshipProtoR — turboR専用エンジン ビルド定義(唯一の真実)
#  多モジュール分割: 巨大 main.c を作らない。常駐は小関数の集合体として個別 .c に。
# ============================================================================
include config.mk

ifeq ($(PROFILE),release)
  OPT = $(OPT_RELEASE)
else
  OPT = $(OPT_DEBUG)
endif

# ── デバッグFPS表示: `make clean && make DEBUG_FPS=1` でFPS常時表示のデバッグROMを生成。
#    未指定(通常ビルド)では -DDEBUG_FPS が付かず、FPS関連コードは #ifdef で完全に消える
#    (=リリースはカウント負荷/スプライトslot予約ゼロ)。切替時は必ず make clean(フラグ変更は
#    ソース不変=makeが再コンパイルを検知しないため)。
DEFS =
ifdef DEBUG_FPS
  DEFS += -DDEBUG_FPS
endif
# ── 実機µs計測(S1990タイマ自己診断): make clean && make DEBUG_PROF=1
ifdef DEBUG_PROF
  DEFS += -DDEBUG_PROF
endif
# ── 海アニメ停止(切り分け用): make clean && make DEBUG_FPS=1 DEBUG_NOSEA=1
ifdef DEBUG_NOSEA
  DEFS += -DDEBUG_NOSEA
endif
# ── エンティティ処理停止(切り分け用): make clean && make DEBUG_FPS=1 DEBUG_NOENT=1
ifdef DEBUG_NOENT
  DEFS += -DDEBUG_NOENT
endif

BUILD  = build
SRC    = src
CORE   = $(SRC)/core
SCENES = $(SRC)/scenes
INC    = -I$(SRC)/include -I$(BUILD)
# ビルドタグ(gitの短縮ハッシュ)。CONFIG画面に表示し、どのコミットのROMか一目で判別できるようにする。
GITVER := $(shell (git rev-parse --short HEAD 2>/dev/null || echo local) | tr 'a-z' 'A-Z')
# semver(単一の真実=VERSIONファイル)。行末インラインコメントはMakeが値にスペースを含めるので別行にする
GAMEVER := $(shell cat VERSION 2>/dev/null || echo 0.0.0)

# ── ヘッダ依存(重要): 共有ヘッダ(特に構造体を
#    定義する entity.h 等)を変更したら全 .c を必ず再コンパイルする。これを怠ると
#    「新旧で構造体レイアウトが食い違うオブジェクトが混在→メモリ破損」という
#    stale-object バグを踏む(実際に踏んだ)。小規模なので全再コンパイルで十分。
HDRS := $(wildcard $(SRC)/include/*.h) config.mk $(BUILD)/assets_data.h

# ── 常駐(bank0-2, <=24KB)にリンクするソース。crt0 は先頭に別途リンク。
#    ここへ足すたびに常駐サイズが増える。冷たいものは足さず bcall バンクへ回すこと。
RESIDENT_RELS = \
  $(BUILD)/ramexec.rel \
  $(BUILD)/sys.rel \
  $(BUILD)/vdp.rel \
  $(BUILD)/bank.rel \
  $(BUILD)/input.rel \
  $(BUILD)/sound.rel \
  $(BUILD)/sprites.rel \
  $(BUILD)/gamestate.rel \
  $(BUILD)/entity.rel \
  $(BUILD)/player.rel \
  $(BUILD)/fire.rel \
  $(BUILD)/ops.rel \
  $(BUILD)/scroll.rel \
  $(BUILD)/ship_aag.rel \
  $(BUILD)/hotcode.rel \
  $(BUILD)/prof.rel \
  $(BUILD)/hud.rel \
  $(BUILD)/scene.rel \
  $(BUILD)/scene_stage.rel \
  $(BUILD)/main.rel

# ── 追加バンク(冷たいコード/データ)。--bank N file の形で rompack へ渡す。
#    冷たいコードは「単独コンパイル → --code-loc 0xA000 でリンク → rompack が当該バンクへ格納」。
#    被呼コードは 0xA000 が単一エントリで自己完結(ARCHITECTURE §2)。
# title.yjk(54272B の変換済みYJK)は bank9 から連続7バンク(9..15)へ跨って敷く(--asset)。
# 常駐の vdp_blit_bank_vram が bank9,10,… を 8KB窓でめくって SCREEN12 VRAM へ流す。
ROMPACK_BANKS = --bank 4 assets/cards.bin \
                --bank 5 $(BUILD)/scene_title.ihx \
                --bank 6 $(BUILD)/scene_config.ihx \
                --bank 7 $(BUILD)/scene_ending.ihx \
                --bank 8 $(BUILD)/assets.bin \
                --bank 16 $(BUILD)/ship_render.ihx \
                --bank 17 $(BUILD)/hot.bin \
                --asset 9 assets/title.yjk

.PHONY: all rom clean run
all: rom
rom: GAME.ROM

$(BUILD):
	mkdir -p $(BUILD)

# データアセット(BGM曲データ)を bin＋常駐用ヘッダへパック(gen_assets.mjs)。
# assets_data.h を先に作れば assets.bin も同時に出る(1回の実行で両方生成)。
$(BUILD)/assets_data.h: tools/gen_assets.mjs | $(BUILD)
	node tools/gen_assets.mjs 8 $(BUILD)/assets.bin $(BUILD)/assets_data.h
$(BUILD)/assets.bin: $(BUILD)/assets_data.h
	@true

# ビルドタグ用ヘッダ。毎ビルドで git ハッシュを生成し、内容が変わった時だけ更新(不要な再コンパイルを避ける)。
.PHONY: FORCE
FORCE:
$(BUILD)/version.h: FORCE | $(BUILD)
	@printf '#define BUILD_VER "%s"\n#define GAME_VERSION "%s"\n' '$(GITVER)' '$(GAMEVER)' > $@.tmp; \
	 cmp -s $@.tmp $@ 2>/dev/null || mv $@.tmp $@; rm -f $@.tmp

# C ソースは core/ と scenes/ から探す(basename は一意に保つ)
vpath %.c $(CORE) $(SCENES)

$(BUILD)/%.rel: %.c $(HDRS) | $(BUILD)
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $< -o $@

$(BUILD)/crt0rom.rel: $(SRC)/crt0rom.s | $(BUILD)
	sdasz80 -o $@ $<

# (bank4 は開始カードの事前ベイク艦画像 assets/cards.bin に転用。旧 bank_demo は撤去)

# 常駐イメージのリンク(crt0 が先頭 = _HEADER/_CODE 起点)。rom.noi に常駐シンボル番地が出る。
$(BUILD)/rom.ihx: $(BUILD)/crt0rom.rel $(RESIDENT_RELS)
	sdcc -m$(TARGET) --no-std-crt0 --code-loc $(CODELOC) --data-loc $(DATALOC) \
	     $(BUILD)/crt0rom.rel $(RESIDENT_RELS) -o $@

# ── バンクシーン(冷たいシーン)ビルド(2パス) ──
# 1) 常駐 rom.ihx → rom.noi から常駐シンボル絶対番地を .s に落とす(バンク側が常駐関数を呼ぶため)
$(BUILD)/resident_syms.rel: $(BUILD)/rom.ihx tools/gen_symdefs.mjs
	node tools/gen_symdefs.mjs $(BUILD)/rom.noi $(BUILD)/resident_syms.s
	sdasz80 -o $@ $(BUILD)/resident_syms.s
# バンク先頭スタブ(0xA000 に jp _banked_entry を確定)
$(BUILD)/bankhead.rel: $(SRC)/banked/bankhead.s | $(BUILD)
	sdasz80 -o $@ $<
# RAM実行モジュールの先頭スタブ(hot_ram 番地に jp _hot_aa_update/_hot_aa_collide のジャンプテーブルを確定)
$(BUILD)/hothead.rel: $(SRC)/banked/hothead.s | $(BUILD)
	sdasz80 -o $@ $<
# 2) バンクシーン汎用ルール(scene_<name>.c → bank .ihx)。追加は ROMPACK_BANKS に1行。
#    bankhead + scene_<name> + resident_syms を 0xA000 リンク。data-loc は各シーン共用の退避域。
$(BUILD)/scene_%.ihx: $(SCENES)/scene_%.c $(HDRS) $(BUILD)/version.h $(BUILD)/bankhead.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(INC) $< -o $(BUILD)/scene_$*.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xE000 \
	     $(BUILD)/bankhead.rel $(BUILD)/scene_$*.rel $(BUILD)/resident_syms.rel -o $@

# バンク化した冷たいコード(シーン以外)。艦の重い描画本体 banked/ship_render.c → bank16。
# 常駐の ship_aag が薄いラッパで bcall、こちらの banked_entry が g_shipargs を読んで描画。
$(BUILD)/ship_render.ihx: $(SRC)/banked/ship_render.c $(HDRS) $(BUILD)/bankhead.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(INC) $(SRC)/banked/ship_render.c -o $(BUILD)/ship_render.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xE000 \
	     $(BUILD)/bankhead.rel $(BUILD)/ship_render.rel $(BUILD)/resident_syms.rel -o $@

# ── RAM実行モジュール(hot.c) ──
# 常駐が予約した hot_ram[] の実番地(rom.noi の _hot_ram)へ --code-loc してリンク→ ihx→bin へ変換。
# rompack は .bin を bank17 先頭から配置し、起動時 hot_load() が hot_ram[] へ転写する。
$(BUILD)/hot.ihx: $(SRC)/banked/hot.c $(HDRS) $(BUILD)/hothead.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(INC) $(SRC)/banked/hot.c -o $(BUILD)/hot.rel
	@HA=$$(awk '/^DEF _hot_ram /{print $$3}' $(BUILD)/rom.noi); \
	 if [ -z "$$HA" ]; then echo "ERROR: rom.noi に _hot_ram が無い(hotcode.c を常駐にリンクせよ)"; exit 2; fi; \
	 echo "  hot.c を hot_ram=$$HA へリンク"; \
	 sdcc -m$(TARGET) --no-std-crt0 --code-loc $$HA --data-loc 0xE000 \
	     $(BUILD)/hothead.rel $(BUILD)/hot.rel $(BUILD)/resident_syms.rel -o $@
$(BUILD)/hot.bin: $(BUILD)/hot.ihx tools/ihx2bin.mjs $(SRC)/include/hotcode.h
	@HA=$$(awk '/^DEF _hot_ram /{print $$3}' $(BUILD)/rom.noi); \
	 node tools/ihx2bin.mjs $(BUILD)/hot.ihx $$HA $@; \
	 CAP=$$(awk '/^#define[ \t]+HOT_CAP/{print $$3}' $(SRC)/include/hotcode.h); \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt "$$CAP" ]; then \
	   echo "ERROR: hot.bin=$${SZ}B が HOT_CAP=$${CAP}B を超過($$((SZ-CAP))B)。hot_load のコピーが末尾を落とし常駐グローバル(g_scene/curstage)を破壊します。hotcode.h の HOT_CAP を上げてください(0xE000天井まで)。"; \
	   exit 3; \
	 fi; \
	 echo "  hot.bin=$${SZ}B / HOT_CAP=$${CAP}B (残り$$((CAP-SZ))B)"

BANK_IHX = $(BUILD)/scene_title.ihx \
           $(BUILD)/scene_config.ihx $(BUILD)/scene_ending.ihx $(BUILD)/ship_render.ihx $(BUILD)/hot.bin

GAME.ROM: $(BUILD)/rom.ihx $(BANK_IHX) $(BUILD)/assets.bin assets/title.yjk assets/cards.bin
	node tools/rompack.mjs --code $(BUILD)/rom.ihx --out $@ $(ROMPACK_BANKS)

# openMSX で起動 → 数秒後にスクショ → 終了(headless 検証)
run: GAME.ROM
	openmsx -machine $(MACHINE) -carta GAME.ROM -romtype ASCII8 -script tools/test_boot.tcl

MACHINE ?= C-BIOS_MSX2+_JP

clean:
	rm -rf $(BUILD) GAME.ROM
