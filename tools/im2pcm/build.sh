#!/bin/sh
# 検証ROM(IM2 + 走査線割込み 4 行ごとで turboR PCM を出す)を作る。
#   sh tools/im2pcm/build.sh   → build/IM2PCM.ROM(16KB, ROM タイプ Normal)
# ★スネアの素材は build/snare_data.h から取る(先に make を一度通しておくこと)。
# ★測り方と結果は docs/PCM調査_2026-10-07.md。
set -e
cd "$(dirname "$0")/../.."
OUT=build/im2pcm
mkdir -p "$OUT"
cp tools/im2pcm/im2pcm.s "$OUT/"
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
sdasz80 -o im2pcm.rel im2pcm.s
sdldz80 -i im2pcm.ihx im2pcm.rel >/dev/null
cd ../..
node tools/ihx2bin.mjs build/im2pcm/im2pcm.ihx 0x4000 build/IM2PCM.ROM
