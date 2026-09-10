/* bank.h — ASCII8 MegaROM バンク切替 & 汎用バンクコールの常駐API。
   規律(重要):
     - スワップ窓 = 0xA000-0xBFFF。切替は 0x7800 への書込のみ(他窓に書くと暴走)。
     - データを読む/バンクを呼ぶ関数と、その呼び先は必ず 0xA000 未満(bank0-2 常駐)に置く。
     - 切替中は di。読み終え/呼び終えたら既定(bank3)へ復元する。
   詳細は docs/ARCHITECTURE.md「常駐 vs バンク」を参照。 */
#ifndef BANK_H
#define BANK_H

#include "types.h"

#define BANK_SWAP_WIN ((volatile u8 *)0xA000)  /* スワップ窓の先頭        */
#define BANK_DEFAULT  3                          /* 既定でここが窓に居る    */

/* スワップ窓(0xA000)に ROM バンク n を出す(データ先読み用)。呼び元は常駐であること。 */
void bank_data(u8 n);

/* スワップ窓を既定(bank3)へ戻す。 */
void bank_restore(void);

/* データバンク bank の off から len バイトを dst(RAM)へ di 保護コピー(窓差替え→復元)。
   ★常駐からのみ呼ぶ(バンクシーン内から呼ぶと窓復元で自シーンを追い出す)。 */
void data_read(u8 bank, u16 off, u8 *dst, u16 len);

/* --- 汎用バンクコール ---
   g_bank に呼ぶバンク番号を入れて bcall() を呼ぶ。トランポリンは crt0(_bcall)。
   被呼コードは当該バンクの 0xA000 が単一エントリで自己完結(常駐関数/データ窓に触れない)。 */
extern u8 g_bank;
void bcall(void);        /* crt0rom.s の _bcall を呼ぶ(低レベル) */
void bcall_to(u8 bank);  /* g_bank=bank; bcall(); の定型(シーン等から使う) */

#endif /* BANK_H */
