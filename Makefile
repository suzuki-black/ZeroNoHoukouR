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
#    ★演出(ラスタ分割・CPU弾幕・パレットエンジン)は実機確認済みのため常時オン＝フラグは持たない。
#      ここに残すのは計測/切り分け用のスイッチだけ。
DEFS =
ifdef DEBUG_FPS
  DEFS += -DDEBUG_FPS
endif
# ── page2(常駐bank2)のRAM実行を切る(実機A/B計測用): make clean && make DEBUG_PROF=1 NO_RAMX2=1
#    page1 のみRAM実行=旧挙動のROMを作り、page2 も足した版との差分を実機で測るためのスイッチ。
ifdef NO_RAMX2
  DEFS += -DNO_RAMX2
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
HDRS := $(wildcard $(SRC)/include/*.h) config.mk $(BUILD)/assets_data.h $(BUILD)/boss_frames.h $(BUILD)/stage_grade.h

# ★ops.rel(run_ops)は現在どこからも呼ばれていない(艦OPSの解釈は bank16 の ship_render 内に独自実装が
#   ある)。常駐24KBを197B無駄に食っていたのでリンクから外した。使うときはここへ戻すこと。
# ── 常駐(bank0-2, <=24KB)にリンクするソース。crt0 は先頭に別途リンク。
#    ここへ足すたびに常駐サイズが増える。冷たいものは足さず bcall バンクへ回すこと。
# ★BGTEST(背景弾の実測)のときだけ常駐へ足す。RESIDENT_RELS は rom.ihx の依存に読み込み時点で展開されるので、
#   ifdef ブロックで後から足しても依存にならない(踏んだ)。ここで足す。

RESIDENT_RELS = \
  $(BUILD)/ramexec.rel \
  $(BUILD)/raster.rel \
  $(BUILD)/curtain.rel \
  $(BUILD)/overlay.rel \
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
                --bank 18 $(BUILD)/ovl.bin \
                --bank 19 $(BUILD)/gen_planes.ihx \
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
# ★リンク後の番地検証: ramexec_page2_to_ram は複製元として 0x6000-0x7FFF 窓を一時 bank2 へ差し替えるので、
#   自分自身が 0x6000 以降に居ると窓ごと消えて暴走する。RESIDENT_RELS の並び替えで壊れないよう機械強制する
#   (ramexec.rel を先頭に置き続ければ満たされる)。
$(BUILD)/rom.ihx: $(BUILD)/crt0rom.rel $(RESIDENT_RELS)
	sdcc -m$(TARGET) --no-std-crt0 --code-loc $(CODELOC) --data-loc $(DATALOC) \
	     $(BUILD)/crt0rom.rel $(RESIDENT_RELS) -o $@
	@A=$$(awk '/^DEF _ramexec_page2_to_ram /{print $$3}' $(BUILD)/rom.noi); \
	 if [ -z "$$A" ]; then echo "ERROR: rom.noi に _ramexec_page2_to_ram が無い"; exit 2; fi; \
	 if [ $$(printf '%d' $$A) -ge $$(printf '%d' 0x6000) ]; then \
	   echo "ERROR: _ramexec_page2_to_ram=$$A が 0x6000 以降。この関数は複製中に 0x6000-0x7FFF 窓を bank2 へ差し替えるため、"; \
	   echo "       0x4000-0x5FFF に居なければ自分自身が消えて暴走します。RESIDENT_RELS の先頭付近に ramexec.rel を戻してください。"; \
	   exit 2; \
	 fi; \
	 echo "  ramexec_page2_to_ram=$$A (<0x6000 OK)"
	@A=$$(awk '/^DEF _overlay_load /{print $$3}' $(BUILD)/rom.noi); \
	 if [ -z "$$A" ]; then echo "ERROR: rom.noi に _overlay_load が無い"; exit 2; fi; \
	 if [ $$(printf '%d' $$A) -ge $$(printf '%d' 0x6000) ]; then \
	   echo "ERROR: _overlay_load=$$A が 0x6000 以降。複製中に 0x6000-0x7FFF 窓を演出バンクへ差し替えるため、"; \
	   echo "       0x4000-0x5FFF に居なければ自分自身が消えて暴走します(ramexec_page2_to_ram と同じ理由)。"; \
	   exit 2; \
	 fi; \
	 echo "  overlay_load=$$A (<0x6000 OK)"
	@for SYM in _ras_apply _ras_rearm _ras_isr _sfx_update _bgm_update _snd_isr _sound_init; do \
	   A=$$(awk -v n=$$SYM '$$2==n{print $$3}' $(BUILD)/rom.noi); \
	   if [ -z "$$A" ]; then echo "ERROR: rom.noi に $$SYM が無い"; exit 2; fi; \
	   if [ $$(printf '%d' $$A) -ge $$(printf '%d' 0x6000) ]; then \
	     echo "ERROR: 割込み文脈のコード $$SYM=$$A が 0x6000 以降。overlay_load は複製中に"; \
	     echo "       0x6000-0x7FFF 窓を演出バンクへ差し替えたまま**割込みを許可**します(8KB を di で"; \
	     echo "       囲むと 223ms 固まるため)。ISR がその窓に居ると演出バンクのバイト列を実行して暴走します。"; \
	     echo "       → ramexec/raster/sound を RESIDENT_RELS の先頭付近に置き 0x4000-0x5FFF に収めてください。"; \
	     exit 2; \
	   fi; \
	 done; \
	 echo "  割込み文脈コード: 全て <0x6000 OK (overlay_load 複製中の窓差替と非衝突)"
	@H=$$(awk '/^DEF s__HEAP /{print $$3}' $(BUILD)/rom.noi); \
	 if [ -z "$$H" ]; then echo "ERROR: rom.noi に s__HEAP が無い(常駐RAM末尾を判定できない)"; exit 2; fi; \
	 if [ $$(printf '%d' $$H) -gt $$(printf '%d' 0xE000) ]; then \
	   echo "ERROR: 常駐RAM末尾 s__HEAP=$$H が 0xE000 を超過。バンクシーンの static は --data-loc 0xE000 に置かれるため、"; \
	   echo "       常駐グローバル(g_view/g_difficulty 等)がシーン入場のたびに踏み潰されます(設定値が化けて挙動が壊れる)。"; \
	   echo "       → 常駐DATAを減らすか、大物を高位フリー帯(fb_ram 0xE900+512=0xEB00 以降)へ __at で退避してください。"; \
	   exit 2; \
	 fi; \
	 echo "  常駐RAM末尾 s__HEAP=$$H (<=0xE000 OK。バンクscene staticと非衝突)"

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
# RAMオーバレイの先頭スタブ(0xA000 に jp のジャンプテーブルを確定)
$(BUILD)/ovlhead.rel: $(SRC)/banked/ovlhead.s | $(BUILD)
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

# 冷たい手続き生成(戦闘機8方向)を bank19 へ。常駐リクレイムのため sprites.c から移設。
$(BUILD)/gen_planes.ihx: $(SRC)/banked/gen_planes.c $(HDRS) $(BUILD)/bankhead.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(INC) $(SRC)/banked/gen_planes.c -o $(BUILD)/gen_planes.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xE000 \
	     $(BUILD)/bankhead.rel $(BUILD)/gen_planes.rel $(BUILD)/resident_syms.rel -o $@
	@DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/gen_planes.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: gen_planes の static が $$((16#$$DL))B。0xE000〜0xE0FF(256B)を超えると 0xE100 の曲データを踏む"; rm -f $@; exit 3; fi

# DEBUG_PROF の冷たい側(自己診断画面・区間別µs表示)を bank29 へ(20〜23 は4面の中ボスの絵)。常駐リクレイムのため prof.c から移設。
# 通常ビルドでは一切作らない(バンクも消費しない)。
# ★-Wl-b_HOME: このバンクだけ u32 の乗除算(acc_us)を使うので SDCC の long ランタイム(_HOME 領域)が
#   リンクされる。既定では _DATA(0xE000)の後ろに置かれ、バンク窓(0xA000-0xBFFF)の外へ落ちて
#   rompack が「リンク範囲外」で弾く。窓の中の空き(0xBC00)へ明示配置し、_CODE がそこへ届いていないか
#   リンク後に検証する。
$(BUILD)/prof_bank.ihx: $(SRC)/banked/prof_bank.c $(HDRS) $(BUILD)/bankhead.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/prof_bank.c -o $(BUILD)/prof_bank.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xE000 -Wl-b_HOME=0xBC00 \
	     $(BUILD)/bankhead.rel $(BUILD)/prof_bank.rel $(BUILD)/resident_syms.rel -o $@
	@CE=$$(awk '/^_CODE  /{print $$2}' $(BUILD)/prof_bank.map | head -1); \
	 SZ=$$(awk '/^_CODE  /{print $$3}' $(BUILD)/prof_bank.map | head -1); \
	 END=$$((0x$$CE + 0x$$SZ)); \
	 if [ "$$END" -gt $$((0xBC00)) ]; then \
	   echo "ERROR: prof_bank の _CODE が 0xBC00(=_HOME の置き場)に達した。-Wl-b_HOME を上げるか中身を削れ。"; exit 3; \
	 fi

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

# ── RAMオーバレイ(演出コード) ──
# page2 を RAM 化している間だけ見える seg5 上位8KB(=0xA000-0xBFFF)へ載せる。0xA000 リンク。
# data-loc はバンクシーン(0xE000)と衝突しない高位フリー帯へ。rompack が bank OVL_BANK へ格納し、
# シーン初期化で overlay_load() が seg5 上位へ複製する。
OVL_SRCS = $(SRC)/banked/ovl_curtain.c $(SRC)/banked/ovl_palette.c $(SRC)/banked/ovl_crush.c $(SRC)/banked/ovl_shock.c $(SRC)/banked/ovl_rot.c $(SRC)/banked/ovl_power.c
# ★主砲の弾幕(ovl_curtain)は**最後**にリンクする。中ボス用オーバレイ(ovl8)は弾幕を抜いた同じ .rel を同じ順で並べるので、
#   パレット/クラッシュ/衝撃波/宙返り/増槽の static が 0xEE00〜 の同じ番地になる＝途中で入れ替えても状態がそのまま続く。
OVL_RELS = $(BUILD)/ovl_palette.rel $(BUILD)/ovl_crush.rel $(BUILD)/ovl_shock.rel $(BUILD)/ovl_rot.rel $(BUILD)/ovl_power.rel $(BUILD)/ovl_curtain.rel
$(BUILD)/ovl.ihx: $(OVL_SRCS) $(HDRS) $(BUILD)/ovlhead.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/ovl_curtain.c -o $(BUILD)/ovl_curtain.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/ovl_palette.c -o $(BUILD)/ovl_palette.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/ovl_crush.c -o $(BUILD)/ovl_crush.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/ovl_shock.c -o $(BUILD)/ovl_shock.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/ovl_rot.c -o $(BUILD)/ovl_rot.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/ovl_power.c -o $(BUILD)/ovl_power.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead.rel $(OVL_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovl.bin: $(BUILD)/ovl.ihx tools/ihx2bin.mjs $(SRC)/include/overlay.h
	@node tools/ihx2bin.mjs $(BUILD)/ovl.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 8192 ]; then \
	   echo "ERROR: ovl.bin=$${SZ}B が オーバレイ枠 8192B を超過。seg5 上位8KB に収まりません。"; exit 3; \
	 fi; \
	 echo "  ovl.bin=$${SZ}B / 8192B (残り$$((8192-SZ))B)"

# ── 最終面(巨大機 XB-19) ──
# コマは tools/gen_boss.py が三面図のシルエット(assets/xb19_mask.png)から生成する(約30秒)。
# 面ごとの時間帯・天候のパレット(パレットエンジンが持つ表)
$(BUILD)/stage_grade.h: tools/gen_grade.py | $(BUILD)
	python3 tools/gen_grade.py h $@
$(BUILD)/boss_frames.h: tools/gen_boss.py assets/xb19_mask.png | $(BUILD)
	python3 tools/gen_boss.py bin $(BUILD)/boss_vram.bin $(BUILD)/boss_vram0.bin $(BUILD)/boss_misc.bin $@
$(BUILD)/boss_vram.bin $(BUILD)/boss_vram0.bin $(BUILD)/boss_misc.bin: $(BUILD)/boss_frames.h
# 最終面のオーバレイ: パレット/宙返りは通常面と同じソースを別名で入れ、弾幕/クラッシュ/衝撃波は入れない。
# ★ovl_final.c は 0xB800〜(海の写し 2KB)を RAM として使う＝コードは 0x1800 以内。位置情報/背景弾は 0xEC00〜(弾幕の固定帯)。
# ★static は 0xEE00〜0xEEFF に収めること(0xEF00 は分割表)。リンク後に検証する。
OVL6_RELS = $(BUILD)/ovl6_palette.rel $(BUILD)/ovl6_rot.rel $(BUILD)/ovl_final.rel
$(BUILD)/ovl6.ihx: $(SRC)/banked/ovl_palette.c $(SRC)/banked/ovl_rot.c $(SRC)/banked/ovl_final.c $(HDRS) $(BUILD)/boss_frames.h $(BUILD)/ovlhead6.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) -DOVL_FINAL $(INC) $(SRC)/banked/ovl_palette.c -o $(BUILD)/ovl6_palette.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) -DOVL_FINAL $(INC) $(SRC)/banked/ovl_rot.c -o $(BUILD)/ovl6_rot.rel
	sdcc -m$(TARGET) -c $(OPT) --opt-code-size $(DEFS) $(INC) $(SRC)/banked/ovl_final.c -o $(BUILD)/ovl_final.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead6.rel $(OVL6_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovlhead6.rel: $(SRC)/banked/ovlhead6.s | $(BUILD)
	sdasz80 -o $@ $<
$(BUILD)/ovl6.bin: $(BUILD)/ovl6.ihx tools/ihx2bin.mjs
	@node tools/ihx2bin.mjs $(BUILD)/ovl6.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 6144 ]; then \
	   echo "ERROR: ovl6.bin=$${SZ}B が 6144B(0xA000-0xB7FF)を超過。0xB800〜は海の写しの RAM。"; exit 3; \
	 fi; \
	 echo "  ovl6.bin=$${SZ}B / 6144B (残り$$((6144-SZ))B)"; \
	 DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/ovl6.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: ovl6 の static が $$((16#$$DL))B。0xEE00〜0xEEFF(256B)を超えると分割表(0xEF00)を壊す"; exit 3; fi
ROMPACK_BANKS += --asset 32 $(BUILD)/boss_vram.bin --asset 40 $(BUILD)/boss_vram0.bin --bank 43 $(BUILD)/boss_misc.bin --bank 27 $(BUILD)/ovl6.bin

# 撃沈シーンのオーバレイ: 撃破の瞬間に通常面のものと入れ替える。パレット(面の天候つき)は同じソースを別名で入れる。
# ★static は 0xEE00〜0xEEFF に収めること(0xEF00 は分割表)。リンク後に検証する。
OVL7_RELS = $(BUILD)/ovl7_palette.rel $(BUILD)/ovl_sink.rel
$(BUILD)/ovl7.ihx: $(SRC)/banked/ovl_palette.c $(SRC)/banked/ovl_sink.c $(HDRS) $(BUILD)/ovlhead7.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) -DOVL_SINK $(INC) $(SRC)/banked/ovl_palette.c -o $(BUILD)/ovl7_palette.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) $(INC) $(SRC)/banked/ovl_sink.c -o $(BUILD)/ovl_sink.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead7.rel $(OVL7_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovlhead7.rel: $(SRC)/banked/ovlhead7.s | $(BUILD)
	sdasz80 -o $@ $<
$(BUILD)/ovl7.bin: $(BUILD)/ovl7.ihx tools/ihx2bin.mjs
	@node tools/ihx2bin.mjs $(BUILD)/ovl7.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 8192 ]; then \
	   echo "ERROR: ovl7.bin=$${SZ}B が オーバレイ枠 8192B を超過。"; exit 3; \
	 fi; \
	 echo "  ovl7.bin=$${SZ}B / 8192B (残り$$((8192-SZ))B)"; \
	 DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/ovl7.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: ovl7 の static が $$((16#$$DL))B。0xEE00〜0xEEFF(256B)を超えると分割表(0xEF00)を壊す"; exit 3; fi
ROMPACK_BANKS += --bank 28 $(BUILD)/ovl7.bin

# 中ボス用オーバレイ: 通常面のオーバレイから主砲の弾幕を抜き、中ボスを足す。海の区間で出現の瞬間に入れ替え、戦艦の前に戻す。
# ★共通部分は ovl.bin と同じ .rel を同じ順で(static の番地を揃えるため)。中ボス本体は最後。
# ★増槽(ovl_power)は入れない: 中ボス戦の間は戦闘機(銀の敵機)が出ず、その前の増槽は海と一緒に流れ去っている。static も持たないので番地はずれない
OVL8_RELS = $(BUILD)/ovl_palette.rel $(BUILD)/ovl_crush.rel $(BUILD)/ovl_shock.rel $(BUILD)/ovl_rot.rel $(BUILD)/ovl_midboss.rel
$(BUILD)/ovl8.ihx: $(BUILD)/ovl.ihx $(SRC)/banked/ovl_midboss.c $(HDRS) $(BUILD)/ovlhead8.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c --opt-code-size --max-allocs-per-node 9000 $(DEFS) $(INC) $(SRC)/banked/ovl_midboss.c -o $(BUILD)/ovl_midboss.rel   # ★8KB 枠に収めるため常にサイズ優先(1フレーム1回の処理で速度は効かない)
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead8.rel $(OVL8_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovlhead8.rel: $(SRC)/banked/ovlhead8.s | $(BUILD)
	sdasz80 -o $@ $<
$(BUILD)/ovl8.bin: $(BUILD)/ovl8.ihx tools/ihx2bin.mjs
	@node tools/ihx2bin.mjs $(BUILD)/ovl8.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 8192 ]; then \
	   echo "ERROR: ovl8.bin=$${SZ}B が オーバレイ枠 8192B を超過。"; exit 3; \
	 fi; \
	 echo "  ovl8.bin=$${SZ}B / 8192B (残り$$((8192-SZ))B)"; \
	 DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/ovl8.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: ovl8 の static が $$((16#$$DL))B。0xEE00〜0xEEFF(256B)を超えると分割表(0xEF00)を壊す"; exit 3; fi
# Fw 200(1面の中ボス, 64x64・迷彩＋重ね)の 32 方向×1024B(4バンク)
$(BUILD)/fw200.bin: tools/gen_fw200.py | $(BUILD)
	python3 tools/gen_fw200.py bin $@ $(BUILD)/fw200_sh.bin
$(BUILD)/fw200_sh.bin: $(BUILD)/fw200.bin
	@true
ROMPACK_BANKS += --asset 44 $(BUILD)/fw200.bin --bank 24 $(BUILD)/ovl8.bin --bank 62 $(BUILD)/bank62.bin

# 2面の中ボス(PBY カタリナ)用オーバレイ: 共通部分は ovl8 と同じ .rel を同じ順で、入口表も ovlhead8 を共用。中ボス本体だけ差し替え。
OVL9_RELS = $(BUILD)/ovl_palette.rel $(BUILD)/ovl_crush.rel $(BUILD)/ovl_shock.rel $(BUILD)/ovl_rot.rel $(BUILD)/ovl_mb_pby.rel
$(BUILD)/ovl9.ihx: $(BUILD)/ovl.ihx $(SRC)/banked/ovl_mb_pby.c $(HDRS) $(BUILD)/ovlhead8.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c --opt-code-size --max-allocs-per-node 9000 $(DEFS) $(INC) $(SRC)/banked/ovl_mb_pby.c -o $(BUILD)/ovl_mb_pby.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead8.rel $(OVL9_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovl9.bin: $(BUILD)/ovl9.ihx tools/ihx2bin.mjs
	@node tools/ihx2bin.mjs $(BUILD)/ovl9.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 8192 ]; then \
	   echo "ERROR: ovl9.bin=$${SZ}B が オーバレイ枠 8192B を超過。"; exit 3; \
	 fi; \
	 echo "  ovl9.bin=$${SZ}B / 8192B (残り$$((8192-SZ))B)"; \
	 DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/ovl9.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: ovl9 の static が $$((16#$$DL))B。0xEE00〜0xEEFF(256B)を超えると分割表(0xEF00)を壊す"; exit 3; fi
# PBY の絵(6段階×16方向×1024B=12バンク)と影(16方向×128B)
$(BUILD)/pby.bin: tools/gen_pby.py tools/gen_fw200.py | $(BUILD)
	python3 tools/gen_pby.py bin $@ $(BUILD)/pby_sh.bin
$(BUILD)/pby_sh.bin: $(BUILD)/pby.bin
	@true
# bank60 = PBY の影(4096B)＋駆逐艦の絵(3072B)
$(BUILD)/dd.bin: tools/gen_dd.py | $(BUILD)
	python3 tools/gen_dd.py bin $@
$(BUILD)/bank60.bin: $(BUILD)/pby_sh.bin $(BUILD)/dd.bin $(BUILD)/p61_a.bin
	@SZ=$$(wc -c < $(BUILD)/pby_sh.bin | tr -d ' '); if [ "$$SZ" -ne 4096 ]; then echo "ERROR: pby_sh.bin が 4096B でない($$SZ)"; exit 3; fi
	@SZ=$$(wc -c < $(BUILD)/dd.bin | tr -d ' '); if [ "$$SZ" -ne 3072 ]; then echo "ERROR: dd.bin が 3072B でない($$SZ)"; exit 3; fi
	cat $(BUILD)/pby_sh.bin $(BUILD)/dd.bin $(BUILD)/p61_a.bin > $@
ROMPACK_BANKS += --bank 25 $(BUILD)/ovl9.bin --asset 48 $(BUILD)/pby.bin --bank 60 $(BUILD)/bank60.bin

# 4面の中ボス(He 111 ×2)用オーバレイ: 共通部分は同じ順。宙返り(ovl_rot)だけ -DOVL_TWIN(下の帯の絵の表Bにも写す)。
# 入口表は ovlhead10(slot14 の衝撃波の分割表を ovl_twin_shock へ差し替え)。
# ★衝撃波(ovl_shock)は入れない(static を持たないので番地はずれない)。4面の中ボスの間は衝撃波のゆがみを出さない。
OVL10_RELS = $(BUILD)/ovl10_palette.rel $(BUILD)/ovl_crush.rel $(BUILD)/ovl10_rot.rel $(BUILD)/ovl_mb_he.rel
$(BUILD)/ovl10.ihx: $(BUILD)/ovl.ihx $(SRC)/banked/ovl_mb_he.c $(SRC)/banked/ovl_rot.c $(SRC)/banked/ovl_palette.c $(HDRS) $(BUILD)/ovlhead10.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) -DOVL_TWIN $(INC) $(SRC)/banked/ovl_palette.c -o $(BUILD)/ovl10_palette.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) -DOVL_TWIN $(INC) $(SRC)/banked/ovl_rot.c -o $(BUILD)/ovl10_rot.rel
	sdcc -m$(TARGET) -c --opt-code-size --max-allocs-per-node 9000 $(DEFS) $(INC) $(SRC)/banked/ovl_mb_he.c -o $(BUILD)/ovl_mb_he.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead10.rel $(OVL10_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovlhead10.rel: $(SRC)/banked/ovlhead10.s | $(BUILD)
	sdasz80 -o $@ $<
$(BUILD)/ovl10.bin: $(BUILD)/ovl10.ihx tools/ihx2bin.mjs
	@node tools/ihx2bin.mjs $(BUILD)/ovl10.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 7424 ]; then \
	   echo "ERROR: ovl10.bin=$${SZ}B が 7424B(0xA000-0xBCFF)を超過。0xBD00〜は2機の色の控え(HE_COLBUF)。"; exit 3; \
	 fi; \
	 echo "  ovl10.bin=$${SZ}B / 7424B (残り$$((7424-SZ))B)"; \
	 DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/ovl10.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: ovl10 の static が $$((16#$$DL))B。0xEE00〜0xEEFF(256B)を超えると分割表(0xEF00)を壊す"; exit 3; fi
# He 111 の絵(32方向×1024B=4バンク)
$(BUILD)/he111.bin: tools/gen_he111.py tools/gen_fw200.py | $(BUILD)
	python3 tools/gen_he111.py bin $@ $(BUILD)/he111_sh.bin
$(BUILD)/he111_sh.bin: $(BUILD)/he111.bin
	@true
# bank61 = He 111 の影(4096B)＋P-61 の絵4件 / bank62 = Fw 200 の影(4096B)＋P-61 の絵4件(ROM に空きバンクが無いので半分ずつ使う)
$(BUILD)/bank61.bin: $(BUILD)/he111_sh.bin $(BUILD)/p61_b.bin
	cat $^ > $@
$(BUILD)/bank62.bin: $(BUILD)/fw200_sh.bin $(BUILD)/p61_c.bin
	cat $^ > $@
ROMPACK_BANKS += --bank 26 $(BUILD)/ovl10.bin --asset 20 $(BUILD)/he111.bin --bank 61 $(BUILD)/bank61.bin

# 3面の中ボス(駆逐艦)用オーバレイ: パレット処理は -DOVL_DD(3面の1行だけ・9 番を艦の色に)。宙返りは通常版。
# 入口表は ovlhead11(slot14 を ovl_dd_split=艦の帯の横ずれと荒天のうねりへ)。衝撃波(ovl_shock)は入れない。
OVL11_RELS = $(BUILD)/ovl11_palette.rel $(BUILD)/ovl_crush.rel $(BUILD)/ovl_rot.rel $(BUILD)/ovl_mb_dd.rel
$(BUILD)/ovl11.ihx: $(BUILD)/ovl.ihx $(SRC)/banked/ovl_mb_dd.c $(SRC)/banked/ovl_palette.c $(HDRS) $(BUILD)/ovlhead11.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c $(OPT) $(DEFS) -DOVL_DD $(INC) $(SRC)/banked/ovl_palette.c -o $(BUILD)/ovl11_palette.rel
	sdcc -m$(TARGET) -c --opt-code-size --max-allocs-per-node 9000 $(DEFS) $(INC) $(SRC)/banked/ovl_mb_dd.c -o $(BUILD)/ovl_mb_dd.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead11.rel $(OVL11_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovlhead11.rel: $(SRC)/banked/ovlhead11.s | $(BUILD)
	sdasz80 -o $@ $<
$(BUILD)/ovl11.bin: $(BUILD)/ovl11.ihx tools/ihx2bin.mjs
	@node tools/ihx2bin.mjs $(BUILD)/ovl11.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 8192 ]; then \
	   echo "ERROR: ovl11.bin=$${SZ}B が オーバレイ枠 8192B を超過。"; exit 3; \
	 fi; \
	 echo "  ovl11.bin=$${SZ}B / 8192B (残り$$((8192-SZ))B)"; \
	 DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/ovl11.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: ovl11 の static が $$((16#$$DL))B。0xEE00〜0xEEFF(256B)を超えると分割表(0xEF00)を壊す"; exit 3; fi
ROMPACK_BANKS += --bank 63 $(BUILD)/ovl11.bin

# 5面の中ボス(P-61)用オーバレイ(bank3): パレットは -DOVL_P61(5面の1行だけ)、宙返りは -DOVL_MAG(半分に縮めて絵の表Bへ・枠 0..3)。
# 衝撃波(ovl_shock)は入れない(枠のため。static を持たないので番地はずれない)。入口表は ovlhead12(slot14=ovl_p61_split)。
# 0xBC00〜は背景の弾の表(48発)、0xBF00〜は海のひな形なので 7168B 以下。
# ★メガクラッシュ(ovl_crush, 2.4KB)も入れない: 津波自体が拡大(MAG)を使うので拡大中の中ボスとは相性が悪く、枠も足りない。中ボスの間はボムを使えない
OVL12_RELS = $(BUILD)/ovl12_palette.rel $(BUILD)/ovl12_rot.rel $(BUILD)/ovl_mb_p61.rel
$(BUILD)/ovl12.ihx: $(BUILD)/ovl.ihx $(SRC)/banked/ovl_mb_p61.c $(SRC)/banked/ovl_rot.c $(SRC)/banked/ovl_palette.c $(HDRS) $(BUILD)/ovlhead12.rel $(BUILD)/resident_syms.rel
	sdcc -m$(TARGET) -c --opt-code-size --max-allocs-per-node 9000 $(DEFS) -DOVL_P61 $(INC) $(SRC)/banked/ovl_palette.c -o $(BUILD)/ovl12_palette.rel
	sdcc -m$(TARGET) -c --opt-code-size --max-allocs-per-node 9000 $(DEFS) -DOVL_MAG $(INC) $(SRC)/banked/ovl_rot.c -o $(BUILD)/ovl12_rot.rel
	sdcc -m$(TARGET) -c --opt-code-size --max-allocs-per-node 9000 $(DEFS) $(INC) $(SRC)/banked/ovl_mb_p61.c -o $(BUILD)/ovl_mb_p61.rel
	sdcc -m$(TARGET) --no-std-crt0 --code-loc 0xA000 --data-loc 0xEE00 \
	     $(BUILD)/ovlhead12.rel $(OVL12_RELS) $(BUILD)/resident_syms.rel -o $@
$(BUILD)/ovlhead12.rel: $(SRC)/banked/ovlhead12.s | $(BUILD)
	sdasz80 -o $@ $<
$(BUILD)/ovl12.bin: $(BUILD)/ovl12.ihx tools/ihx2bin.mjs
	@node tools/ihx2bin.mjs $(BUILD)/ovl12.ihx 0xA000 $@; \
	 SZ=$$(wc -c < $@ | tr -d ' '); \
	 if [ "$$SZ" -gt 7168 ]; then \
	   echo "ERROR: ovl12.bin=$${SZ}B が 7168B(0xA000-0xBBFF)を超過。0xBC00〜は背景の弾の表と海のひな形。"; exit 3; \
	 fi; \
	 echo "  ovl12.bin=$${SZ}B / 7168B (残り$$((7168-SZ))B)"; \
	 DL=$$(awk '/l__DATA/{print $$1}' $(BUILD)/ovl12.map | head -1); \
	 if [ $$((16#$$DL)) -gt 256 ]; then echo "ERROR: ovl12 の static が $$((16#$$DL))B。0xEE00〜0xEEFF(256B)を超えると分割表(0xEF00)を壊す"; exit 3; fi
# P-61 の絵(9方向×1024B)を3つに分ける(bank60 の後ろ1件・bank61 の後ろ4件・bank62 の後ろ4件)
$(BUILD)/p61_a.bin: tools/gen_p61.py tools/gen_fw200.py | $(BUILD)
	python3 tools/gen_p61.py bin $@ $(BUILD)/p61_b.bin $(BUILD)/p61_c.bin
$(BUILD)/p61_b.bin $(BUILD)/p61_c.bin: $(BUILD)/p61_a.bin
	@true
ROMPACK_BANKS += --bank 3 $(BUILD)/ovl12.bin

BANK_IHX = $(BUILD)/ovl.bin $(BUILD)/ovl6.bin $(BUILD)/ovl7.bin $(BUILD)/ovl8.bin $(BUILD)/fw200.bin $(BUILD)/fw200_sh.bin $(BUILD)/ovl9.bin $(BUILD)/pby.bin $(BUILD)/pby_sh.bin $(BUILD)/ovl10.bin $(BUILD)/he111.bin $(BUILD)/he111_sh.bin $(BUILD)/ovl11.bin $(BUILD)/bank60.bin $(BUILD)/bank61.bin $(BUILD)/bank62.bin $(BUILD)/ovl12.bin $(BUILD)/boss_vram.bin $(BUILD)/gen_planes.ihx \
           $(BUILD)/scene_title.ihx \
           $(BUILD)/scene_config.ihx $(BUILD)/scene_ending.ihx $(BUILD)/ship_render.ihx $(BUILD)/hot.bin

# ── 実機検証: 走査線途中の MAG 切替テスト(起動シーンを差し替え): make clean && make MAGTEST=1
ifdef MAGTEST
  DEFS          += -DMAGTEST
  BANK_IHX      += $(BUILD)/scene_magtest.ihx
  ROMPACK_BANKS += --bank 30 $(BUILD)/scene_magtest.ihx
endif
# ── 実機検証: 走査線途中の 横スクロール(R#26/R#27) 切替テスト: make clean && make HSTEST=1
ifdef HSTEST
  DEFS          += -DHSTEST
  BANK_IHX      += $(BUILD)/scene_hstest.ihx
  ROMPACK_BANKS += --bank 31 $(BUILD)/scene_hstest.ihx
endif
# ── 実機検証: 背景に描く弾の発数と FPS: make clean && make BGTEST=1 DEBUG_FPS=1
#    ★本番の弾はホット区間(page1/2 を RAM へ差し替えた状態)で動くので、**ゲーム本体の海の区間に混ぜて**測る。
#      別シーン(ROM実行)で測ると 4 倍近く遅い別物になる。発数はスコア欄に出る(3秒ごとに +8 で自動掃引)。
ifdef BGTEST
  DEFS          += -DBGTEST
endif
ifdef DEBUG_PROF
  BANK_IHX      += $(BUILD)/prof_bank.ihx
  ROMPACK_BANKS += --bank 29 $(BUILD)/prof_bank.ihx
endif

GAME.ROM: $(BUILD)/rom.ihx $(BANK_IHX) $(BUILD)/assets.bin assets/title.yjk assets/cards.bin
	node tools/rompack.mjs --code $(BUILD)/rom.ihx --out $@ $(ROMPACK_BANKS)

# openMSX で起動 → 数秒後にスクショ → 終了(headless 検証)
run: GAME.ROM
	openmsx -machine $(MACHINE) -carta GAME.ROM -romtype ASCII8 -script tools/test_boot.tcl

MACHINE ?= C-BIOS_MSX2+_JP

clean:
	rm -rf $(BUILD) GAME.ROM
