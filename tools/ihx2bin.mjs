// ihx2bin.mjs — Intel HEX(SDCC .ihx) を「linkBase 起点の生バイナリ」に展開する。
//   rompack の --bank は .ihx を 0xA000 起点前提で展開するため、0xA000 以外に --code-loc した
//   RAM実行モジュール(hot.c=hot_ram番地)はそのままでは載らない。ここで linkBase から詰めた .bin に
//   変換し、rompack へは .bin として渡す(先頭からバンクに配置される)。
//   使い方: node tools/ihx2bin.mjs <in.ihx> <linkBase(hex,例 0xD500)> <out.bin>
import { readFileSync, writeFileSync } from 'node:fs';

const [, , inIhx, baseStr, outBin] = process.argv;
if (!inIhx || !baseStr || !outBin) {
  console.error('usage: node tools/ihx2bin.mjs <in.ihx> <linkBase(hex)> <out.bin>');
  process.exit(2);
}
const base = parseInt(baseStr, 16);
if (!Number.isInteger(base)) { console.error(`ERROR: linkBase 不正: ${baseStr}`); process.exit(2); }

let max = -1;
const bytes = [];
for (const l of readFileSync(inIhx, 'utf8').split(/\r?\n/)) {
  if (l[0] !== ':') continue;
  const n = parseInt(l.substr(1, 2), 16);
  const a = parseInt(l.substr(3, 4), 16);
  const t = parseInt(l.substr(7, 2), 16);
  if (t !== 0) continue;                    // データレコードのみ
  for (let i = 0; i < n; i++) {
    const rel = a + i - base;
    if (rel < 0) { console.error(`ERROR: addr 0x${(a + i).toString(16)} が linkBase 0x${base.toString(16)} 未満`); process.exit(2); }
    bytes[rel] = parseInt(l.substr(9 + i * 2, 2), 16);
    if (rel > max) max = rel;
  }
}
const buf = Buffer.alloc(max + 1, 0x00);
for (let i = 0; i <= max; i++) if (bytes[i] !== undefined) buf[i] = bytes[i];
writeFileSync(outBin, buf);
console.log(`ihx2bin: ${inIhx} (base 0x${base.toString(16)}) → ${outBin}  ${buf.length}B`);
