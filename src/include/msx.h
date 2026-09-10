/* msx.h — MSX BIOS ワークエリア/BIOSコール/MSXバージョンの定数。
   ハード直叩きの“番地”はここに集約し、各モジュールのマジックナンバーを排除する。 */
#ifndef MSX_H
#define MSX_H

#include "types.h"

/* ---- BIOS ワークエリア ---- */
#define MSX_VER   (*(volatile u8  *)0x002D)  /* 0=MSX1,1=MSX2,2=MSX2+,3=turboR */
#define JIFFY     (*(volatile u16 *)0xFC9E)  /* 60Hz インクリメントのソフトタイマ */
#define SCRMOD_W  0xFCAF                      /* CHGMOD 用の画面モード保持ワーク    */
#define RG0SAV    0xF3DF                      /* R#0 のBIOS影(必要時に整合を取る)   */

/* ---- BIOS エントリ ---- */
#define BIOS_CHGMOD 0x005F   /* A=画面モード で画面切替(SCREEN n)                */
#define BIOS_CHGCPU 0x0180   /* turboR: A=CPUモード(0x80|n) で Z80/R800 切替     */
#define BIOS_RSLREG 0x0138
#define BIOS_ENASLT 0x0024

/* ---- MSX バージョン判定 ---- */
#define IS_TURBOR() (MSX_VER >= 3)

#endif /* MSX_H */
