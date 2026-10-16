// gen_hwdiag_sums.mjs — 計測 ROM(make HWDIAG=1)用: 常駐コード(0x4000-0x9FFF)の 2KB ごとの和を C の表にする。
//   実行中に RAM へ複製された常駐(page1 = seg4、page2 の下半分 = seg5)を、この値と比べる(ovl_mb_dd.c)。
//   空きは rompack と同じく 0xFF で埋める。
//   使い方: node tools/gen_hwdiag_sums.mjs build/rom.ihx build/hwdiag_sums.h
import { readFileSync, writeFileSync } from 'node:fs';

const [inIhx, outH] = process.argv.slice(2);
const BASE = 0x4000, SIZE = 0x6000, BLK = 0x800;
const img = Buffer.alloc(SIZE, 0xFF);
for (const l of readFileSync(inIhx, 'utf8').split(/\r?\n/)) {
  if (l[0] !== ':') continue;
  const n = parseInt(l.substr(1, 2), 16), a = parseInt(l.substr(3, 4), 16), t = parseInt(l.substr(7, 2), 16);
  if (t !== 0) continue;
  for (let i = 0; i < n; i++) {
    const r = a + i - BASE;
    if (r >= 0 && r < SIZE) img[r] = parseInt(l.substr(9 + i * 2, 2), 16);
  }
}
const sums = [];
for (let b = 0; b < SIZE; b += BLK) {
  let s = 0;
  for (let i = 0; i < BLK; i++) s = (s + img[b + i]) & 0xFFFF;
  sums.push('0x' + s.toString(16).padStart(4, '0'));
}
writeFileSync(outH,
  `/* 自動生成(tools/gen_hwdiag_sums.mjs): 常駐 0x4000-0x9FFF の 2KB ごとの和 */\n` +
  `#define DG_NBLK ${sums.length}\n` +
  `static const u16 dg_sum_exp[DG_NBLK] = { ${sums.join(', ')} };\n`);
console.log(`hwdiag_sums: ${sums.length} blocks → ${outH}`);
