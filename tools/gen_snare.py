#!/usr/bin/env python3
"""gen_snare.py — turboR 内蔵 PCM で鳴らすスネアドラムを合成する。

  python3 tools/gen_snare.py build/snare_data.h [build/snare_preview.wav]

★標本化と長さは **src/core/pcm.s から読む**(PCM_PERIOD と PCM_SNARE_LEN)。
  二箇所に書くと片方だけ直して音程が狂うので、出どころは pcm.s だけにしてある。
  出力は 8bit unsigned・中心 0x80。
★置き場所は 0xEB00 の 256B 枠しか無い(RAM の実測地図は 苦労と教訓 §16-49)。

合成の中身(スネアの実物に合わせてある):
  ・**ノイズ**(響き線 = スナッピーの音)。これが主役で、スネアが「シャッ」と聞こえる成分。
    白色のままだと低い方が濁るので、1次の差分で軽く高域を持ち上げる。
  ・**胴の共鳴**(185Hz と 330Hz)。これが無いと「ノイズのプツッ」で太鼓に聞こえない。
    7993Hz 標本化なので 4kHz までしか出ない。胴の音は十分この中に入る。
  ・包絡は 2 本。胴は速く(約7ms)、ノイズはやや遅く(約11ms)減衰させる。
    同じ減衰にすると「ボッ」という一塊になり、打撃の鋭さが出ない。
  ・末尾 8 サンプルで必ず 0x80 へ落とす(途中で切ると D/A に段差が残ってプチッと鳴る)。

★乱数は固定種の線形合同法。ビルドのたびに音が変わると、鳴り方の違いが
  「直したせい」なのか「素材が変わったせい」なのか分からなくなる。
"""
import math
import re
import struct
import sys

PCM_S = 'src/core/pcm.s'
TICK  = 3.911e-6     # システムタイマ 1 カウント(turboR)

def from_asm(name):
    """pcm.s の 'NAME = 数' を読む。ここを唯一の出どころにする。"""
    src = open(PCM_S, encoding='utf-8').read()
    m = re.search(r'^' + name + r'\s*=\s*(\d+)', src, re.M)
    if not m:
        sys.exit(f'{PCM_S} に {name} が見つからない')
    return int(m.group(1))

PERIOD = from_asm('PCM_PERIOD')
N      = from_asm('PCM_SNARE_LEN')
RATE   = int(round(1.0 / (PERIOD * TICK)))
if N > 256:
    sys.exit(f'PCM_SNARE_LEN={N} が 0xEB00 の枠 256B を超えている')

class Rng:
    """固定種 LCG(gen_wave.py と同じ作法)。-1.0..+1.0 を返す。"""
    def __init__(self, seed): self.v = seed
    def f(self):
        self.v = (self.v * 25173 + 13849) & 0xFFFF
        return ((self.v >> 8) & 0xFF) / 127.5 - 1.0

def synth():
    rng = Rng(0x4A1F)
    prev = 0.0
    out = []
    for i in range(N):
        t = i / RATE
        # ノイズ(響き線)。差分で高域を少し持ち上げる = 1次ハイパス
        w = rng.f()
        noise = w - 0.55 * prev
        prev = w
        # 胴の共鳴 2 本
        body = (math.sin(2 * math.pi * 185.0 * t) +
                0.6 * math.sin(2 * math.pi * 330.0 * t))
        # 包絡(胴は速く、ノイズはやや遅く)
        env_b = math.exp(-t / 0.007)
        env_n = math.exp(-t / 0.011)
        # 立ち上がり(0.4ms)。瞬時に最大だと D/A に段差が出る
        atk = min(1.0, t / 0.0004)
        v = atk * (0.68 * noise * env_n + 0.42 * body * env_b)
        out.append(v)
    # 正規化(8bit の端は避けて ±118 に収める。クリップさせない)
    peak = max(abs(v) for v in out) or 1.0
    s = 118.0 / peak
    pcm = [max(1, min(255, int(round(128 + v * s)))) for v in out]
    # 末尾は必ず中心へ戻す(切り際のプチッを消す)
    for k in range(8):
        a = (k + 1) / 8.0
        j = N - 8 + k
        pcm[j] = int(round(pcm[j] * (1 - a) + 128 * a))
    pcm[-1] = 128
    return pcm

def write_header(path, pcm):
    lines = [
        '/* 自動生成(tools/gen_snare.py)。手で編集しない。',
        f'   turboR 内蔵 PCM 用スネア: 8bit unsigned(中心 0x80) / {RATE}Hz / {len(pcm)}B = '
        f'{len(pcm) * 1000 // RATE}ms。',
        '   ★冷たいバンク(coldsetup.c = bank30)に置き、起動時に 1 回だけ 0xEB00 へ写す。',
        '     常駐には 1 バイトも置かない。 */',
        '#ifndef SNARE_DATA_H', '#define SNARE_DATA_H',
        f'#define SNARE_DATA_LEN {len(pcm)}',
        'static const unsigned char snare_data[SNARE_DATA_LEN] = {',
    ]
    for i in range(0, len(pcm), 16):
        lines.append('    ' + ','.join(str(v) for v in pcm[i:i + 16]) + ',')
    lines += ['};', '#endif /* SNARE_DATA_H */', '']
    with open(path, 'w') as f:
        f.write('\n'.join(lines))

def write_wav(path, pcm):
    """試聴用。1 発では短すぎて判断できないので、8 発を 0.3 秒間隔で並べる。"""
    gap = [128] * int(RATE * 0.3)
    data = bytes((pcm + gap) * 8)
    hdr = (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
           struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE, 1, 8) +
           b'data' + struct.pack('<I', len(data)))
    with open(path, 'wb') as f:
        f.write(hdr + data)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit('usage: gen_snare.py <out.h> [preview.wav]')
    pcm = synth()
    write_header(sys.argv[1], pcm)
    if len(sys.argv) > 2:
        write_wav(sys.argv[2], pcm)
    print(f'snare: {len(pcm)}B / {RATE}Hz / {len(pcm) * 1000 // RATE}ms → {sys.argv[1]}')
