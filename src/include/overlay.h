/* overlay.h — 演出コードの RAM オーバレイ(ROADMAP P0-2)。
   ★解決したい問題: 常駐コード窓(bank0-2, 24KB)が満杯(残り 1KB 弱)で、演出を足す場所が無い。
     かといってバンク(bcall)へ置くと、カートリッジ ROM 実行＝R800 で実測 3.84倍遅い
     (性能と高速化 §0-0)。毎フレーム回る演出コードには致命的。

   ★使う場所: **page2 を RAM 化している間、0xA000-0xBFFF が丸ごと遊んでいる。**
     §4-3 の page2 RAM 実行は、空きマッパーセグメント seg5(16KB)を page2 へ割り当て、
     その**下位 8KB にだけ**常駐 bank2 の複製を置いている。page2 は 0x8000-0xBFFF なので、
     上位 8KB(=0xA000-0xBFFF)は未使用のまま残っている。ここへ演出コードを載せる。
       ・追加の RAM 消費はゼロ(既に確保済みのセグメントの空き領域)
       ・RAM 実行なので R800 で全速(バンク実行の 3.84倍速)
       ・8KB まるごと使える(常駐窓の残り 1KB とは桁が違う)

   ★制約(守らないと暴走する):
     1. **ホット区間の中でしか呼べない。** 0xA000 は page2 が cart のときは ASCII8 の
        スワップ窓なので、ramx_use_ram() と ramx_use_cart() の間でのみ有効。
     2. オーバレイ側から data_read/bcall を呼ばない(page2 が RAM＝スワップ窓が無い)。
     3. 常駐関数/常駐データの参照は可(page1 は同一内容の RAM 複製＝番地はそのまま)。
     4. 読み込み(overlay_load)は page2 が cart の文脈で行う(シーン初期化など)。
     5. g_ramx2_ok=0 の機械では使えない。呼び出し側は g_ovl_ok を見ること。

   ★入口: 先頭に ovlhead.s のジャンプテーブル(1関数=3バイトの jp)を置き、
     OVL_ADDR + 3*slot を固定入口にする(hot_ram と同じ作法)。 */
#ifndef OVERLAY_H
#define OVERLAY_H

#include "types.h"

#define OVL_ADDR 0xA000   /* page2=RAM 時の seg5 上位8KB の先頭 */
#define OVL_CAP  0x2000   /* 8KB */
#define OVL_BANK 18       /* rompack が ovl.bin を置くバンク(17=hot.bin の次) */

/* スロット番号は ovlhead.s の jp の並びと一致させること。 */
#define OVL_SLOT_CURTAIN_UPDATE 0
#define OVL_SLOT_CURTAIN_RING   1
#define OVL_SLOT_CURTAIN_DRAW   2
#define OVL_SLOT_CURTAIN_COLLIDE 3
#define OVL_SLOT_PAL_UPDATE      4
#define OVL_SLOT_PAL_RESET       5
#define OVL_SLOT_CURTAIN_VOLLEY  6
#define OVL_SLOT_CURTAIN_PRESENT 7

extern u8 g_ovl_ok;       /* 1=オーバレイ読込済み(呼んでよい)。0なら呼ばないこと */

/* 演出バンクを seg5 上位8KB へ複製する。page2 が cart の文脈で呼ぶこと(シーン初期化)。
   ★この関数は 0x4000-0x5FFF に居ること: 複製元として 0x6000-0x7FFF 窓を演出バンクへ
     差し替えるため、0x6000 以降に居ると自分自身が窓ごと消えて暴走する。
     Makefile がリンク後に番地を検証する(ramexec_page2_to_ram と同じ理由・同じ守り方)。 */
void overlay_load(u8 bank);

/* ---- オーバレイ入口(ホット区間の中でのみ呼べる。g_ovl_ok も見ること) ---- */
void pal_update(void);   /* パレットエンジン(設計メモ §2-A)。毎フレーム16色を計算して差分書き */
void pal_reset(void);    /* 面開始/再開: 状態を捨て次フレームに全書き直し */
void curtain_volley(u8 active);            /* 戦艦の主砲からの弾幕斉射(active=0 で何もしない) */
void curtain_present(u8 nper, u8 line);    /* 予約slotへ帯ごとに流し込む */

#endif /* OVERLAY_H */
