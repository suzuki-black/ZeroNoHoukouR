#!/bin/sh
# 検証ROM(走査線割込み 4 行ごとで turboR PCM を出す)を作る。入口の違う 2 本:
#   sh tools/im2pcm/build.sh   → build/IM1PCM.ROM … IM1(BIOS 経由) + R#15=1 を普段の値に。★本命
#                                build/IM2PCM.ROM … IM2(表 257B を RAM に置く。ゲームには置き場所が無い)
#   どちらも 16KB、ROM タイプ Normal。
# ★スネアの素材は build/snare_data.h から取る(先に make を一度通しておくこと)。
# ★測り方と結果は docs/PCM調査_2026-10-07.md。
set -e
cd "$(dirname "$0")/../.."
OUT=build/im2pcm
mkdir -p "$OUT"
cp tools/im2pcm/im2pcm.s tools/im2pcm/im1pcm.s "$OUT/"
python3 - <<'EOF'
import re
src = open('build/snare_data.h').read()
v = [int(x) for x in re.findall(r'\b\d+\b', src.split('{', 1)[1].split('}')[0])]
buf = v + [128] * (1024 - len(v))            # スネア 128B + 無音 = 1024B(約 0.26 秒で一周)
with open('build/im2pcm/snare_buf.inc', 'w') as f:
    for i in range(0, 1024, 16):
        f.write('\t.db ' + ','.join(f'0x{b:02X}' for b in buf[i:i + 16]) + '\n')
EOF
cd "$OUT"
for M in im2pcm im1pcm; do
  sdasz80 -o $M.rel $M.s
  sdldz80 -i $M.ihx $M.rel >/dev/null
done
cd ../..
node tools/ihx2bin.mjs build/im2pcm/im2pcm.ihx 0x4000 build/IM2PCM.ROM
node tools/ihx2bin.mjs build/im2pcm/im1pcm.ihx 0x4000 build/IM1PCM.ROM
