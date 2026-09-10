# アーキテクチャ — turboR専用エンジン(BattleshipProtoR)

> 前作 `BattleshipProto` の最大の教訓＝**32KBコード窓の硬い天井**と**巨大 main()**。
> 本エンジンは初手から「**スロット/バンク割り当てを意識した小モジュールの集合体**」＋
> 「**ゲームアルゴリズム/シーン管理の汎用化**」で作り、各ステージのバンク空きに常に余裕を残す。

---

## 1. メモリ地図(Z80 64KB空間 と ASCII8 MegaROM)

```
Z80アドレス空間                         ASCII8 MegaROM(256KB = 32 bank × 8KB)
┌───────────────────────────┐          ┌──────────────────────────────────┐
│ 0x0000-0x3FFF page0 = BIOS │          │ bank0  ROM 0x00000  ┐             │
│ 0x4000-0x5FFF ← bank0      │◀────────▶│ bank1  ROM 0x02000  ├ 常駐コード   │
│ 0x6000-0x7FFF ← bank1      │  ASCII8  │ bank2  ROM 0x04000  ┘ (<=24KB)    │
│ 0x8000-0x9FFF ← bank2      │  4窓      │ bank3  ROM 0x06000  = スワップ窓予約 │
│ 0xA000-0xBFFF ← スワップ窓  │◀────────▶│ bank4..31 ROM 0x08000.. = 冷コード  │
│ 0xC000-0xFFFF page3 = RAM  │          │                        ＋データ    │
└───────────────────────────┘          └──────────────────────────────────┘
   窓の切替は 0x6000/0x6800/0x7000/0x7800 への書込。0x7800 = 0xA000窓(スワップ)。
```

- **bank0-2 = 常駐コード(0x4010-0x9FFF, 24KB上限)**: 起動時に固定で居続ける“熱い”コード。
  `rompack.mjs` が **24KB超過をビルドエラー**にし、毎ビルドで残量を表示する(硬い天井を機械強制)。
- **bank3 = スワップ窓の既定ページ**: 前作はここまでコードに使い窓を潰した=失敗。**本作は温存**。
  データ先読み/バンクコールのときだけ 0xA000 窓を別バンクへ差替え、済んだら bank3 に戻す。
- **bank4-31 = 冷たいコード＋データ(各8KB)**: シーン別演出/設定/エンディング等の“冷たい”コードと、
  艦/発砲スクリプト/BGM/文字列/スプライトパターン等のデータ。ステージを足すほどここを使う。
  ★タイトル YJK 画(SCREEN12)が bank9-15 を占有し 128KB では空きが枯れたため **256KB(32バンク)へ拡張**。

**バンク割り当ての現状**:

| バンク | 用途 |
|--------|------|
| 0–2 | **常駐コード＋定数（上限 24KB）**。超過は rompack がビルドエラー |
| 3 | ASCII8 スワップ窓（0xA000–0xBFFF）を温存するため未使用 |
| 4 | 開始カードの艦画像（`assets/cards.bin`, 5 艦連結） |
| 5–7 | 冷たいバンクシーン（title / config / ending） |
| 8 | データバンク：BGM 曲・艦体 OPS・火球ビットマップ・撃破パネル・敵機カラー表 |
| 9–15 | タイトル YJK 画像（SCREEN12, `assets/title.yjk`） |
| 16 | 冷たい艦レンダラ本体（`ship_render`） |
| 17 | ★RAM 実行コード `hot.bin`（起動時に page3 RAM `hot_ram[]` へコピー。§4-3） |

**RAM(page3 = 0xC000-0xFFFF)**:
- data-loc 以降に各モジュールの `static`。**初期値付き static は避ける**（ROM では `_INITIALIZER` の
  配置に注意が要るため、`init` 関数で実行時に書く）。大物は「小さな RAM バッファ＋`data_read`」で扱う。
- §4-3 の `hot_ram[HOT_CAP]` は常駐 _DATA 末尾に予約（0xE000 が RAM 実行枠の天井）。冷データ
  `g_card_ram`(0xE100)/ship_ram(0xE700)/fb_ram(0xE900) は高位固定へ退避して枠を確保。

**VRAM（SCREEN5 = GRAPHIC4, 128KB, 4 ページ）**:

| VRAM Y | 用途 |
|--------|------|
| page0 (0–255) | スプライトテーブル（属性 0x7600 / 色 0x7400 / パターン 0x7800, Y232–255）。Y0–231 はゲーム中は非表示＝**火球のベイク先やカード/結果の一時描画**に流用（炎ソースは y0–31） |
| page1 (256–511) | **表示リング**（256px の環状バッファ）。R#23 で縦スクロール |
| 512–527 | 海テンプレート（256×16, タイル可能な水） |
| 528–1023 | **艦バッファ B**（256×496 = 31 行。海を下地に艦を事前描画） |

---

## 2. 常駐 vs バンク — 何をどこに置くか(規律)

### 常駐(bank0-2, 24KB)に置いてよいもの＝“毎フレーム/割込みで必ず要る核”だけ
- crt0 + `_bcall` トランポリン(`src/crt0rom.s`)
- VDP アクセス(`vdp.c`) / バンク切替(`bank.c`) / 入力(`input.c`)
- シーンFSM ディスパッチャ(`scene.c`) / 起動初期化(`sys.c`) / エントリ(`main.c`)
- H.TIMI 60Hz 割込み音ドライバ ISR(SFX＋BGM, 実装済 `sound.c`) / エンティティプールの update・draw(実装済 `entity.c`)
- run_ops(描画IF `ops.c`) と run_fire(発砲IF `fire.c`)の**インタプリタ本体**(骨格実装済)

### バンク(bank4+)へ回すもの＝“冷たい/一度きり/データ”
- 各シーンの重い init/draw(title, 空戦イントロ setup, 撃破演出, ending, gameover, 設定メニュー)
- 艦の `ship_ops`、発砲スクリプト `FireDesc`、敵配置/パターン、BGM音符、文字列、スプライト定義
- → コードが冷たいなら **bcall**、データなら **bank_data で先読み**。

### バンクコード3原則(破ると暴走)
1. **呼び先は 0xA000 単一エントリで自己完結** — 常駐関数を直接呼ばない/データ窓(0xA000)を読まない。
2. **呼び元は必ず常駐(<0xA000)** — 切替中に 0xA000 のコードを踏まない。
3. **切替中は di、済んだら bank3 へ復元** — `_bcall` が担保(`crt0rom.s`)。

---

## 3. 汎用化した仕組み(初手から)

### 3.1 シーンFSM (`scene.h` / `scene.c`)
`Scene { init(); update()->次ID; bank }` の表 `registry[]` を1か所に集約。
`scene_run()` が「入場時 init 1回 → 毎フレーム update → 戻り値で遷移」を回す。**巨大 main() を作らない**。
- **現在のフロー**: 起動＝`SC_TITLE`(bank5, ID=0。旧 `SC_BOOT` 疎通シーンは土台実証済で撤去)。タイトルで**トリガ(SPACE/ジョイ)→ `SC_STAGE`** で即ゲーム開始。
  **設定は隠しコマンド(コナミ ↑↑↓↓←→←→ B A)で `SC_CONFIG`(bank6)** を開く(→STARTで `SC_STAGE`)。
  `SC_STAGE`(★連続面: 海→戦艦 地続き) → `SC_ENDING`(bank7) → `SC_TITLE`。ミスは面リスタート、残機尽きは
  継続ONでコンティニュー(無限)/OFFで `SC_STAGE`→`SC_TITLE`。
  ★新ルール「各面=空戦→戦艦」を**画面カット無しの1本スクロール**(SC_STAGE)で実装(HANDOFF §2)。
  空戦(戦闘機)は**戦艦が未出現の開けた海の間だけ**湧く(`cam>SC_CAM_SHIP`)。艦が出現した瞬間に
  残存する戦闘機/敵弾を一掃し(`ent_clear_enemies`)、以後は戦闘機のみ毎フレーム掃除(艦の手前にゴミが
  居残らない)。全砲台撃破で**撃破演出**(炎上スペクタクル→「TARGET DESTROYED」＋スコア→勝ちどき
  ファンファーレ `play_fanfare`)→ SC_ENDING。
  ※旧 `SC_INTRO`/`SC_BOSS`(2シーンのハードカット試作)は SC_STAGE に統合し削除済み。
- **冷たいシーンのバンク化(実装済み)**: `registry` の `bank != 0` のシーンは、`call_scene()` が
  `g_scene_phase`(0=init/1=update)をセットして `bcall_to(bank)` で当該バンクの 0xA000 エントリを実行。
  バンク側 `banked_entry` が phase を見て init/update を分岐し、update の戻り(次ID)を `g_scene_ret` に書く。
  **常駐窓(24KB)を消費せず**に冷たい画面を ROM バンクへ。ビルド機構(常駐シンボル注入)は §4.1。

### 3.2 バンクコール (`bank.h` / `crt0rom.s`) ← 新ビルドでE2E実証済み
`bcall_to(<bank>)`(= `g_bank=<bank>; bcall();`)で任意バンクの 0xA000 エントリを実行(トランポリンは常駐)。
実証: `bank_demo.c` を bank4 に格納 → `bcall_to(4)` → RAM 0xE000 に 0x5A 書込を openMSX で確認。
冷たいシーンはこの枠(0xA000 単一エントリで自己完結)に載せ、常駐窓を食わない。

### 3.3 データ駆動(骨格実装済み。難易度メカ等はこれから拡張)
- **run_ops**(描画データ駆動 `ops.c`): 艦/敵/背景を op配列で描く。現状 OPS_RECT のみ(艦=船体/艦橋/砲の矩形)。
  艦体OPSは**データバンク(bank8)**に置き `data_read` で RAM へ読んで描画(常駐に艦データを持たない)。
  将来 op を増やし(ライン/三角/艦橋段積み/パターン転送)、データは bank へ。
- **run_fire**(発砲スクリプト `fire.c`): `FireDesc{interval, suppress, [op,a,kind,spd].., 0}`。op: FIXED/RING/**AIMED/AIMFAN**。
  方向は**32分割**(11.25°)。将来 予告/レイジ/固定弾安置も**データで**追加(ヘッダに足す)。原典: 前作 `docs/fire-script-spec.md`。
  - **AIMED**(自機狙い＋散らし): 狙い方向に**一様乱数±a ステップの円錐**を足す(「不正確さの円錐」)。a=0で厳密狙い。
  - **AIMFAN**(自機狙い n-way): 自機中心に a発を2ステップ間隔で扇状＋扇全体を乱数微回転。**偶数aは自機直線上に隙間**。
  - ★**ゼロ距離抑え込み**(`suppress`=半径px, 0=無効): 自機がこの半径内に居ると**その砲は発射スキップ**。
    危険砲に肉薄すると撃たせず、近距離で安全に連射→**最速撃破**。実効半径は難易度で増減(`supp_adj`: EASY広い/HARD狭い)。
    高速棄却(|dx|,|dy|>r)→円内のみ dx²+dy² を u16 で比較(オーバフロー回避)。1面ビスマルクが教える中心メカ(HANDOFF §7)。
  - 設計意図: 前作「狙いすぎ」反省 → 狙い弾に**ブレ/スプレッド**を混ぜ公平化。出典: dev.to "Simple Bullet Spread for AI"(aim+uniform offset)、Sparen's Danmaku Design(aimed patternの inconsistency)。難易度で散らし量を可変にできる(将来)。
- **emit**(弾生成プリミティブ `fire.c`): `emit(x,y,dir,kind,spd)`。32分割方向×弾速で ET_BULLET を1発生成。`aim_dir()` で自機への最近傍方向。

### 3.3.1 連続縦スクロール地形 (`scroll.c`) ★設計の要
**海と戦艦バトルは地続き(画面カット無し)の1本の縦スクロール**。前作は戦艦のど真ん中から開始→
本作は手前の海から始めてスクロールで戦艦の船首が入ってくるだけ。前作cportの実証手法を移植:
- 表示=**page1 を256pxリングバッファ**、`R#23=cam&0xFF` で縦スクロール。スプライト表は page0(非スクロール)。
- 戦艦は上位VRAM(バッファB=page2/3)へ **run_ops で一度だけ事前描画**。OPS_RECTのyはu8なので
  255超の艦は `SC_SHIPBUF_Y` と `+256` の**2パス**で描く。
- 露出した16px世界行だけ `draw_row`=**LMMM 1本**で page1 の該当スロットへ流す(高速)。海行は青+波を直接塗り。
- **フェーズ**: 海=蛇行なし直進(前進) → 船尾が見えたら地続きに交戦へ → 戦艦=**船首↔船尾の往復**(縦cam往復)
  ＋**横揺れ weaveX**(R#26/27, `g_meander`で砲塔スプライトも追従)。前作の戦艦戦スクロールと同じ。

### 3.4 スクロール (`vdp.c`)
- **縦スクロール R#23**(空戦イントロの海): VRAM全体を縦シフト=**スプライトにも効く**。`vdp_set_vscroll` が量を保持し
  `vdp_sprite_pos` が Y に加算 → 自機/敵を画面固定に見せる。海は256行を波線付きで seamless に。
  - **ゴミ対策(重要)**: スプライトテーブルは page0 末尾(0x7400-0x7FFF=ライン232-255)。縦スクロールでこの帯が
    可視域に回り込むと画面中央にゴミが出る。→ **海を page1(0x8000+, VDPコマンドは DY に +256)へ描き page1 を表示**。
    スプライトテーブルは page0 のまま=表示されない。`vdp_set_display_page(1)`。ボスは page0(R#23=0で無縁)。
- **横スクロール R#26/27**(ボスの蛇行): スプライト非影響。艦体(VRAM)を左右に揺らす。
  ※プロト簡略化: 弾(スプライト)の発射原点は揺れに追従しない。本番で砲塔追従を入れる。

### 3.5 自機・当たり判定 (`player.c` / `entity.c`)
- **自機**(ET_PLAYER, `player.c`): `g_input` で移動＋クランプ、トリガでクールダウン付き上方発砲(TEAM_PLAYER弾)。
  現在位置を `g_player_x/y` に公開(AIMED/UIが参照)。
- **当たり判定**(`ent_resolve_collisions`): 自機弾×敵戦闘機→両消滅・`g_kills`++・`g_score`+=10、
  自機弾×砲台→hp減算・0で撃破・`g_gun_kills`++・`g_score`+=50、敵弾/戦闘機×自機→被弾。
  16x16 AABB(甘めマージン)。弾の帰属は `Entity.team`(emit=TEAM_ENEMY / 自機発砲=TEAM_PLAYER)。
- **耐久/ミス/残機/コンティニュー**(HANDOFF §1): 被弾は `g_pinv`(無敵)=0 かつ 無敵設定OFF の時のみ有効。
  耐久HP `g_php`>1 なら HP減＋短時間無敵点滅(生存)。0で**撃墜**=`g_miss` を立てる。シーン(`stage_update`)が
  `g_miss` を見て **残機 `g_lives`-- → 残っていれば面最初から全砲台復活でリスタート(`stage_setup`)**。
  残機0なら **継続ON(`g_continue`)でコンティニュー(残機を初期値へ戻し再挑戦=無限) / OFFでタイトル**。
  耐久/残機の初期値は config(`g_durability` 1-9 / `g_lives_idx`→2/3/5)。無敵設定 `g_invinc` は被弾を完全無効。
- **[解決済み] スクロール中のHUD 1px縦振動**: 画面最上部のHUDは最もラスタ競合しやすい。重い `ent_draw_all`
  (敵/砲塔の色表を毎フレーム書く)の後に `hud_draw` を呼ぶと、ラスタが既に上端を通過してから属性を書く→R#23と
  ズレて1px上下に揺れていた。→ **`hud_draw` を R#23 設定(scroll)直後・`ent_draw_all` より前**に移動し VBLANK 中に確定。

### 3.6 スプライトHUD (`hud.c`) — スコア/残機
- 面表示は **page1(スクロールする環状バッファ)** なので `vdp_text`(page0直書き)は流れて使えない。
  → スコア/残機を**スプライト**で画面上端に固定表示。スプライトは縦スクロール補正(§3.4)済みなので Y=画面座標でよい。
- **数字パターン**: BIOS 8x8 フォント('0'..'9', CGTABL 経由)を 16x16 スプライトの左上へ写して生成(`hud_init`)。
- **スロット確保**: HUD が先頭 `HUD_SLOTS`(=6, スコア5桁+残機1桁)を占有。`ent_draw_all` は `g_spr_base` から詰める。
  V9938 sprite mode2 は **1走査線8枚**まで表示可なので上端6枚+敵少数でも欠けにくい(MSX1の4枚制限ではない)。

> **[解決済み] スクロール時のスプライトテーブル露出ゴミ**: 縦スクロールで page0末尾(ライン232-255)の
> スプライトテーブルが可視域に出る問題。→ 海を page1 に描き page1 を表示(§3.4)。イントロで実機(openMSX)確認済み。
> **[解決済み] 画面外の世界アンカー砲塔が海上に幽霊表示**: 砲塔は `e->y=ay-cam`(世界アンカー)なので海フェーズ中は
> 画面上方=負のy。スプライトYは u8 なので `(u8)(-132)=124` と折り返して海の上に砲塔だけ出ていた。→ `ent_draw_all` で
> **y が縦範囲(-16..212)外なら描画しない**ガードを追加(上から侵入する戦闘機の一瞬の折り返しも同時に解消)。openMSX確認済み。
> **[解決済み・重要] スプライト停止マーカが効かず古い残像が残りまくる**: `vdp_sprite_hide_from` が Y=**208**(0xD0)を書いていたが、
> SCREEN5 は**212ライン**表示で終端値は **216**(0xD8)。208 は終端にならず、マーカ以降の**前フレームの古いスプライトが
> そのまま表示され続けて画面中ゴミだらけに**なっていた(結果画面でスプライトが消えない件も同根)。→ **216 に修正**
> (`vdp_sprite_init` は元から216で整合)。`ent_draw_all` が毎フレーム使う要のマーカで、これで残像ゴミが根治。
> **[解決済み] 左端を抜けた弾が右端に出る**: スプライトXも u8。x<0 の弾が `(u8)x` で右端(241-255)へ折り返していた。
> → `ent_draw_all` の可視判定に **x が 0..255 の範囲外なら描画しない**を追加(縦ガードと同様の横版)。
- **エンティティ・プール**(実装済み骨格 `entity.c`): 自機/敵機/弾/砲/エフェクトを固定長プール＋
  behavior(type別 update)の関数ポインタ表で回す。空戦の敵機も戦艦の砲も同じ枠。
  描画は**ハードウェアスプライト(mode2, 16x16)**。active を先頭スロットへ詰めて属性/色を書き、
  残りは停止マーカ(Y=216=212ライン用終端)で隠す(ハード合成なので消去不要)。大きな艦体は run_ops(将来)で描く。
- ★**スプライト割当と1走査線8枚対策(弾幕対応)** `ent_draw_all`:
  - HUD=固定 slot 0..g_spr_base-1 / **自機=固定 slot g_spr_base(最優先, 絶対に欠けさせない)**。
  - 残り(弾/敵/砲塔/エフェクト)は毎フレーム**描画対象集合の中で開始位置を回転(rot)**して割当。
    V9938 mode2 は1走査線8枚まで(9枚目以降は欠落)なので、回転で「常に同じ弾が消える」を避け
    **フレーム毎に入れ替わるちらつき**へ均等分散。総数は32枚で頭打ち。
  - 画面外(y が -16..212 外)は割当しない(u8 折り返しの海上幽霊防止)。openMSXで12発同一行→回転を確認。

### 3.7 サウンド＋BGM (`sound.c`) とデータバンク運用 (`bank.c` / `gen_assets.mjs`)
- **ISR(H.TIMI 60Hz)**: `snd_isr`(`__naked`, 全レジスタ退避)が毎フレーム `sfx_update`→`bgm_update`。ゲーム負荷非依存。
- **PSG割当**: melody=tone A(**効果音から死守＝絶対に譲らない**), bass=tone B(自機発射音SFX_SHOT/被弾音PHITが
  一時占有＝**ベース側が譲る**), noise C=SFX命中/破壊/敵発砲＋drum。**SFX優先はベース(B)/ノイズ(C)のみ**:
  `sfx_update`が tone B使用中フラグ `sfx_busy_b`(noiseは `sfx_busy_c`)を立て、`bgm_update`はその間 bass/drum を
  譲る（メロディAはSFXに一切譲らない＝どのステージでも撃つ/被弾でメロディが途切れない。鳴り終えると即復帰）。
- **ファンファーレ** `play_fanfare`: ループBGMとは別に、`bgmOn=0` にして前景で mel(A)+har(B) を直接鳴らし
  `vdp_wait_frame` で尺を取る**同期(ブロッキング)再生**。撃破演出の勝ちどきで使用(終了まで戻らない)。
- **BGM曲データ**: `[nMel,nBas,basStep, melPeak,melSus,melVib, basPeak,basSus, drumOn, melNote, melLen, basNote]`。
  音符=音階index(0=C2..47=B5)/255=休符、長さ=フレーム数。melody=可変長/bass=固定basStep/drum=標準マーチ(noise)。
  各パート独立ループ。周期表 `bgm_notetp[48]`。エンベロープ=発音開始 peak→毎フレーム-1→sustain、末尾2f無音、
  vibで伸ばし音に三角ビブラート(旧cportドライバを移植)。現状タイトル/1面マーチを旧 `bgm_tracks.h` から移植済み。
- **★データバンク運用(土台)**: **BGM曲＋艦体OPS**を**データバンク(bank8)**に置き、`data_read()` で必要時に RAM へ読む。
  艦体は**面ごと**にバンクへ(`ship_top_off/len[stage]`, `ship_bot_off/len[stage]`)。`prerender_ship` が
  `data_read(ASSET_BANK, ship_*_off[curstage], ship_ram, ..)`→`run_ops`(常駐文脈)。=実ゲームデータをバンク化した例。
  **多面化**: `STAGE_COUNT` 面。クリア(全砲台撃破→撃破演出)で `curstage++`→次艦を setup(スコア/残機持ち越し)、
  最終面で SC_ENDING。config の**ステージ選択** `g_stage_sel` で開始面を選べる。5艦・敵配置もこの型で載せる。
- BGM: 曲データは bank8。`bgm_play(track)`が `data_read()` で
  現曲だけ RAM(`bgm_ram`)へコピー→以後 ISR は **RAM のみ**参照(割込み中にバンク窓を触らない)。
  `data_read(bank,off,dst,len)`(`bank.c`)= di下で 0xA000窓を bank へ差替え→コピー→既定(bank3)へ復元。
  **★呼び元は必ず常駐**(バンクシーン内から呼ぶと窓復元で自シーンを追い出す)→ BGM切替は `scene.c` の
  `scene_bgm[]` 表で**シーン入場時(常駐文脈, bcall前)**に `bgm_play`/`bgm_stop`。
- **アセットパッカ** `tools/gen_assets.mjs`: 曲を手書き(音名)→ `build/assets.bin`(bank8内容) と
  `build/bgm_data.h`(常駐用: notetp/曲オフセット/長さ)を生成。Makefileが自動実行し rompack が bank8 へ配置。
  ※旧版 `bgm_tracks.h` の実曲(タイトル等)移植はこのパイプラインに曲データを足すだけ。
- **面別ドラム/ベース(設計拡張)**: 曲ヘッダの `drumStyle`(1=標準マーチ/2=重い/3=激しい)で `bgm_drmPat*` と
  テンポ・音量を切替(BB=標準・空母=重い・フッド/アイオワ=激しい)。`bassSweep` は chB をロックマン風
  シンセドラムに(立上り1oct 上→基音へ急降下＋速い減衰)。
- **2段構成**: 海イントロは全面共通のスロー曲、敵艦が見えたら面別の戦闘曲へ切替(`bgm_play`)。勝ちどきは
  13 音のファンファーレ。

### 3.8 破壊・炎上システム (`scene_stage.c` / `entity.c` / bank8火球)
火点＝**主砲 4 基＋対空砲 23 基**。すべて破壊可能で、全滅(`ent_live_turrets()==0 && aa_alive()==0`)でクリア。
- **主砲(`ET_TURRET`)**: スプライト実体。8方向の可動砲身(`SPR_BARREL0`＋`dir8`/`step_dir` で自機を狙って
  段階回転、行別カラー `barrel_col` で金属シェード)。命中で白フラッシュ。撃破後も `active` 維持のまま
  `hidden` にし、scene が炎上残骸を焼き付ける。
- **対空砲(array 方式)**: 実体を持たず `aa_hp[]`/`aa_dead[]` 配列で管理(大型HP2/小型HP1)。`aa_collide` が
  自機弾を座標判定で当てる＝**スプライト枠を消費せず 23 基を破壊対応**(可視区間のみ走査＝§性能)。
- **常時炎上(BG 火球)**: 火球は `gen_assets` がビルド時に丸くベイクし bank8 へ。破壊時に `burn_bake` が
  艦バッファ B へ透過焼き込み(以後スクロールで炎ごと流れる)＋表示リングへ即1回重ねる。スプライト枠を
  使わないので全火点が同時に燃えても破綻しない。★炎ソースタイルは page0 y0–31(隠し保管域)。
- **撃破スペクタクル**: `defeat_update` が艦全体へ火球を escalating に撒いて炎まみれにし、「撃破!!」
  パネル→スコア→勝ちどきファンファーレ→SC_ENDING。

### 3.9 §4-3 ホットコードの RAM 実行（R800 の ROMフェッチ律速対策）★性能の要
R800 は DRAM モードでも**カートリッジ ROM のコードフェッチが内蔵 RAM 実行の約3.8倍遅い**(実機 S1990
タイマ実測)。毎フレームのホットパスを RAM で実行して律速を外す。詳細は [性能と高速化.md](性能と高速化.md)。
- **hot_ram + ジャンプテーブル**(`hotcode.c` / `banked/hot.c` / `hothead.s`): behavior 群・AA(aa_update/
  aa_collide)・エンティティ更新を bank17(`hot.bin`)に置き、起動時に `hot_load()` が page3 RAM の
  `hot_ram[HOT_CAP]` へコピー。常駐は `__naked` asm の `jp hot_ram+3*slot` ラッパで呼ぶ。番地二重管理を
  避けるため Makefile が `rom.noi` の `_hot_ram` を hot.c の `--code-loc` に供給。
  **★HOT_CAP は hot.bin 実サイズ以上に保つこと**(不足すると転写が末尾を落とし常駐グローバルを破壊＝
  暴走。Makefile が超過をビルド時検出。詳細は [苦労と教訓.md](苦労と教訓.md))。
- **page1 動的 RAM 実行**(`ramexec.c`): ゲームループのホット区間だけ page1(0x4000-0x7FFF, 常駐ホットコード)を
  マッパー RAM スロットへ切替える(`page1_use_ram`/`page1_use_cart`)。切替 blob は page3 RAM へ退避して
  実行(足元を切替えるため)。バンキング(0x6000-0x7800)・被弾・BGM 切替の直前は cart へ戻す。空きセグメントの
  RAM プローブに失敗したら**何もしない=ROM のまま**(`g_ramx_ok=0`)で安全側。C-BIOS でも同経路で動き
  openMSX で正当性検証可能(速度差は turboR のみ)。

### 3.10 敵戦闘機の8方向スプライト（手続き生成 `sprites.c`）
海イントロの敵戦闘機と空母艦載機は、**移動方向に機首を向ける 8 方向 × 3 サイズ(小/中/大)** のスプライトを
`build_plane`/`load_planes` が手続き生成し面別に VRAM へ投入(`SPR_PLANE_S/M/L=160/192/224`)。胴体/主翼/尾翼/
エンジンを線分で描き、国別の雰囲気は翼幅デルタ(`pl_wingd`)で作り分ける。★斜め方向で胴体が縞々に途切れる
旧版の罠は「斜め時の隙間埋めピクセル」で回避。空戦機は `dir8(vx,vy)` で向きを、空母停泊機は自機方向
`dir8(player-self)` で追尾しつつ発艦後に小→中→大へ成長する浮上アニメを見せる(`bh_fighter`/`bh_pursuer`)。
海イントロ機は出現を倍増し各面の固有挙動(急降下/直進/蛇行)に蛇行を50%混在させて8方向を活かす。

### 3.11 固定タイムステップ（30fps）と“ジュース”
- **フレームレート依存の罠**: ゲーム速度(敵速/弾速/スクロール/spawn/アニメ)がすべてフレーム数指定だったため、
  §4-1/§4-3 で 60fps 相当に達した瞬間に全部が 2 倍速になった。→ `scene.c` の SC_STAGE で `vdp_wait_frame()`
  を **2 回**待つ固定タイムステップにし**安定 30fps**を担保(★2 VBLANK 目=30fps 固定)。
- **ジュース(余力の使い道)**: 破壊点数ポップアップ(`entity.c` の scorepop。撃破位置の真上に加算点を数字
  スプライトで約1.5秒表示)、敵機の8方向スプライト化(§3.10)、海機の倍増など。

---

## 4. ビルド系(`Makefile` / `config.mk` / `tools/rompack.mjs`)

- 各常駐モジュールは**個別 .c → 個別 .rel**。`Makefile` の `RESIDENT_RELS` に列挙してリンク。
  → SDCC のレジスタ割当破綻/ビルド遅延の温床(巨大単一TU)を避ける。追加は1行。
- **[重要な罠] ヘッダ依存**: 各 `.rel` は `$(HDRS)`(全 include ヘッダ)に依存させている。
  これを怠ると、共有ヘッダ(特に `Entity` 等の**構造体**)を変更しても一部モジュールが再コンパイルされず、
  **新旧で構造体レイアウト(フィールドのオフセット)が食い違うオブジェクトが混在**→フィールド書込が
  隣接データを破壊(メモリ破損)→ゴミ関数ポインタへジャンプ→ハング、という stale-object バグを踏む。
  構造体を変えたら必ず全再コンパイル(小規模なので `$(HDRS)` 依存で十分)。実際に踏んで丸1回分溶かした。
- 冷たいコード: 単独コンパイル(`--code-loc 0xA000`)→ `--bank N build/xxx.ihx` で rompack がバンクへ格納。
- データ: `--bank N file.bin` で任意バンクへ。

### 4.1 バンクシーンのビルド(冷たいシーンを bank へ・常駐関数を呼べる)
自己完結の `bank_demo` と違い、**シーンは常駐関数(vdp/ent/ops…)を呼びたい**。SDCCの別コンパイルでは
常駐シンボルの番地が不明なので、**常駐を先にリンク→番地を注入する2パス**にする:
1. 常駐 `rom.ihx` をリンク(副産物 `rom.noi` に全globalの番地 `DEF _sym 0xADDR`)。
2. `tools/gen_symdefs.mjs` が `rom.noi` から**スワップ窓(0xA000-0xBFFF)以外**の番地を `resident_syms.s`(絶対EQU)へ。
3. バンクシーンを `bankhead.rel + scene_xxx.rel + resident_syms.rel` で `--code-loc 0xA000` リンク。
   - `bankhead.s` = 先頭 `jp _banked_entry`(0xA000 に単一エントリを確定。SDCCの配置順に非依存)。
   - シーンは `#include "vdp.h"` 等で普通に常駐APIを呼べる(番地は resident_syms が解決)。
   - シーン自前の `const`(絵/文字列)は自バンク(0xA000+)に載り、実行中は窓が自分なので読める。
   - 禁止: `bank_data`(データ窓切替=自分を窓から追い出す)。RAM globalは data-loc(例 0xE000)。gsinit無しなので
     初期化付き変数は避け、`init` で実行時に書く。
実例: `scene_title.c`(bank5)/`scene_config.c`(bank6)/`scene_ending.c`(bank7)。**追加は汎用パターンルール**
`$(BUILD)/scene_%.ihx` と `ROMPACK_BANKS` に1行。効果: **title/config/ending のコードは常駐24KBを1バイトも食わない**。
※data-loc(0xE000)は各バンクシーンで共用(同時にアクティブなのは1つ=衝突しない)。gsinit無しなので初期化付き変数は
避け、RAM変数は `init` で書く。
- `rompack` 出力例(空き容量の可視化。現状 v0.2.0 付近):
  ```
  常駐コード(bank0-2): 21049B / 24576B  残り 3527B (3.4KB)
  bank16          : 7372B / 8192B  残り 820B
  bank17          : 5199B / 8192B  残り 2993B
  空きバンク: 14 / 28  (= 112.0KB 未使用)
  ```

### turboR 専用の前提
- 起動時 `sys_init()` が **R800(ROMモード)へブースト**(version<3 では自動スキップ=安全)。
- SDCC の生成命令は Z80(実績)。`-mr800` は実験扱いで当面不採用。CPU速度/RAM増の恩恵は
  「弾/敵の大幅増(真の弾幕)」「誘導/曲線弾など重い計算」「プール/バッファ拡大」で受ける。
- **MSX2+ 互換は捨てる**(割込み/速度前提を専用化)。

---

## 5. 実機/エミュ検証

- ビルド: `make rom` → `GAME.ROM`(256KB ASCII8 MegaROM)。バージョンは `VERSION` ファイルが単一の真実で
  `build/version.h`(GAME_VERSION/BUILD_VER)を自動生成。デバッグ ROM は `make DEBUG_FPS=1`/`DEBUG_PROF=1`。
- 起動検証: `make run`(openMSX headless, 既定 `C-BIOS_MSX2+_JP`)→ `build/boot.png`。
  - turboR実機ROM(`Panasonic_FS-A1GT`)は著作物のため未同梱 → **R800経路/ラスタ/バンク切替の
    タイミングは最終的に実機 or turboR ROM で要確認**(前作の引き継ぎ通り)。
- 途中で踏んだ罠を土台に記録済み:
  - **塗りは LMMV(0x80, ピクセル単位)**。HMMV(0xC0)はバイト単位で G4 では縞になる。
  - **`vdp_cmd_wait` は di 保護必須**。S#2選択中に割込が入ると ISR が割込フラグを消せずハングする。
  - **スプライトテーブルは SCREEN5 の BIOS 既定(属性0x7600/色0x7400/パターン0x7800)を使う**。
    高位VRAMへの相対再配置(R#5/6/11で0xF780等)は本環境(C-BIOS/openMSX)で描画に反映されなかった。
  - **直接VRAM書込(`vdp_text`)の前に `vdp_cmd_wait` 必須**。LMMV(`vdp_fill`)は非同期実行なので、
    走行中に直接書込すると上段の文字が塗り潰され欠ける。`vdp_text` は冒頭で完了待ちする。
  - **文字は BIOS フォント**(`CGTABL`=0x0004 が指す ROM の 8x8 を SCREEN5 page0 へ直接描画)。C-BIOSでも有効。

---

## 6. モジュール一覧(現在)

| 区分 | ファイル | 役割 |
|------|----------|------|
| 起動 | `src/crt0rom.s` | "AB"ヘッダ / page2有効化 / ASCII8窓初期化 / `_bcall` |
| 常駐 | `src/core/main.c` | エントリ(初期化→FSM委譲のみ) |
| 常駐 | `src/core/sys.c` | R800ブースト等の起動初期化 |
| 常駐 | `src/core/vdp.c` | VDPレジスタ/パレット/VRAM/LMMV/文字(BIOSフォント)/スプライト/スクロール |
| 常駐 | `src/core/gamestate.c` | 共有ゲーム状態(g_difficulty/g_lives_idx/g_score/g_lives)。configが設定 |
| 常駐 | `src/core/bank.c` | バンク切替 / `g_bank` / bcall glue / `data_read`(バンク→RAM先読み) |
| 常駐 | `src/core/input.c` | 入力: キーボード(row8カーソル/SPACE, row4のM) ＋ ジョイスティックport1(PSG R#14, di保護)を論理和 |
| 常駐 | `src/core/sound.c` | PSG効果音＋★BGM再生＋H.TIMI 60Hz割込みISR。BGMはbank8→RAMコピーで再生 |
| 常駐 | `src/core/entity.c` | 汎用エンティティプール＋behavior＋スプライト描画＋当たり判定(team) |
| 常駐 | `src/core/player.c` | 自機(ET_PLAYER): 入力で移動＋発砲、位置を公開(AIMED/UI用) |
| 常駐 | `src/core/fire.c` | emit＋run_fire(FIXED/RING/AIMED/AIMFAN, 32分割, 狙い散らし, ★ゼロ距離抑え込み) |
| 常駐 | `src/core/ops.c` | データ駆動描画 run_ops(艦体等)。OPS_RECTのx/yはu8(255まで) |
| 常駐 | `src/core/scroll.c` | ★連続縦スクロール地形(page1リング+R#23、艦をBから流し込み、SEA13海アニメ) |
| 常駐 | `src/core/hud.c` | ★スプライトHUD(スコア5桁＋残機)。数字はBIOSフォントを16x16へ写す。slot0-5確保 |
| 常駐 | `src/core/ramexec.c` | ★§4-3: page1(0x4000-0x7FFF)を区間限定でマッパーRAMへ動的切替(page1_use_ram/cart) |
| 常駐 | `src/core/hotcode.c` | ★§4-3: hot_ram[HOT_CAP]予約＋起動時コピー(hot_load)＋RAM実行スロットへのラッパ |
| 常駐 | `src/core/prof.c` | µs計測(DEBUG_PROF時のみ)。凍結表示はy32以降(炎ソースタイルy0-31を触らない) |
| シーン | `src/scenes/scene_stage.c` | ★1本の連続面(空戦→戦艦 往復蛇行)。艦体＋ゼロ距離抑え込み＋撃破演出(炎上/スコア/ファンファーレ)＋30fps固定 |
| 常駐 | `src/core/sprites.c` | 常駐色表(zcol/barrel_col)＋パターン投入ラッパ＋★戦闘機8方向の手続き生成(build_plane/load_planes) |
| 常駐 | `src/core/scene.c` | シーンFSM ディスパッチャ＋registry(SC_STAGEは2 VBLANK待ち=30fps固定) |
| バンク | `src/banked/hot.c` | ★RAM実行される毎フレームのホットパス(behavior群/AA/エンティティ更新)。bank17→hot_ram |
| バンク | `src/banked/ship_render.c` | 冷たい艦レンダラ本体(bank16, mode4 bcall)。静的スプライトパターンもここで投入 |
| 常駐 | `src/core/ship_aag.c` | 艦レンダラの薄いラッパ(引数をg_shipargsへ退避しbcall)＋毎フレーム参照の対空砲座標表 |
| バンク | `src/banked/bankhead.s` / `hothead.s` | バンク/RAM実行の先頭スタブ(jp _banked_entry / jpジャンプテーブル) |
| シーン(bank) | `src/scenes/scene_title.c` | ★タイトル(bank5)。常駐APIを注入番地で呼ぶ |
| シーン(bank) | `src/scenes/scene_config.c` | ★隠し設定(bank6, コナミで開く)。難易度/残機/耐久/ステージ/継続/無敵。行単位再描画 |
| シーン(bank) | `src/scenes/scene_ending.c` | ★エンディング(bank7)。静かなED曲(track2)＋スタッフロール(クレジットのページ送り→THE END) |
| ツール | `tools/gen_symdefs.mjs` | rom.noi→常駐シンボル絶対番地(.s)。バンクシーン/RAM実行のリンク用 |
| ツール | `tools/ihx2bin.mjs` | hot.ihx→hot.bin(hot_ram番地起点の生バイト列。rompackがbank17へ) |
| ツール | `tools/rompack.mjs` | .ihx＋バンク → 256KB MegaROM。常駐24KB超過とバンク溢れをエラー、空き表示 |
| ツール | `tools/gen_assets.mjs` | BGM曲(音名手書き)＋艦体OPS→ `build/assets.bin`(bank8)＋`build/assets_data.h`(offset/len定数) |
| ツール | `tools/test_{boot,sound,bank,spr,stage,hud}.tcl` | openMSX headless 検証(起動/音/バンク/スプライト/連続面/HUD) |
