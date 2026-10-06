#!/usr/bin/env python3
"""gen_voice.py — turboR 内蔵 PCM で叫ばせる音声をフォルマント合成で作る。

  python3 tools/gen_voice.py build/voice_data.h build/voice_preview.wav

★素材は合成でよい(2026-10-05 ユーザー了承)。アーケードのコナミ(沙羅曼蛇・グラディウスII 等)の
  合成音声はわざと聞き取りづらくしてあったとのことで、多少不明瞭でも構わない。
  **PCM を使うこと自体が大切**という位置づけ。

★台詞と読み(2026-10-06 ユーザー確認済み。読みを間違えると ROM に焼き込まれるので必ず確認すること)
    乾坤一擲！   → けんこんいってき   (タイトルでスペースを押した瞬間)
    敵撃破！     → てきげきは         (結果画面)
    総大将撃破！ → そうだいしょうげきは (最終面の結果画面。エンディングではない)

■ 合成の方法
  日本語の音声合成を一から書くのは割に合わないので、**フォルマント合成**にする。
  母音は「声帯の振動(鋸波)を、口の共鳴(フォルマント)で濾した音」として作れる。
  F1/F2 の組で母音が決まる(日本語の代表値):

      あ F1=800 F2=1200 / い F1=300 F2=2300 / う F1=350 F2=1200
      え F1=500 F2=1900 / お F1=500 F2=900

  子音は種類ごとに作り分ける:
    ・無声破裂(k/t/p): 無音 → 短い雑音のはじけ
    ・有声破裂(g/d/b): 短い低い唸り → はじけ
    ・摩擦(s/sh/h):    帯域を持った雑音
    ・鼻音(n/m):       低いフォルマント(F1≒250)で弱く
    ・促音(っ):        無音そのもの
  これで「それらしく」は鳴る。

■ ★抑揚(ここが音の良し悪しを決める)
  最初、全部の音を同じ高さ(130Hz 固定)・同じ長さ(0.1秒前後)で並べたら
  「音は明瞭だがロボットが話しているよう」と言われた。合成音声が機械に聞こえる原因は
  **音色ではなく抑揚**で、必要なのは次の3つ。

    1) 音の高さの動き … 語頭を高く入り、語全体でゆるやかに下げ(declination)、
       語尾で一度張り上げてから落とす。これが「叫び」の形。
    2) 拍の長短       … 日本語は拍の言語だが、実際には語尾が伸び、助詞的な音は詰まる。
       一律 0.1 秒だと木魚のように聞こえる。
    3) 強さの変化     … 音量が平坦だと平板に聞こえる。語頭と語尾を強く。

  加えて、高さに**わずかな揺れ(ジッタ)**を入れる。完全に一定の高さは人間の声には無く、
  それだけで機械っぽさが出る。

■ 置き場所と標本化
  バンク(8KB)へ 1 発ずつ入れ、**RAM へ写さず窓から直接鳴らす**。1 バンクに収めるため
  8192 バイト以内。標本化は pcm.s の PCM_PERIOD(現在 64 = 3996Hz)に合わせる。
  3996Hz では 2kHz までしか出ないので、子音(特に s/sh)はかなり潰れる。
  それでも 8192B で 2 秒取れるので、尺の制約は無い。
"""
import math
import re
import struct
import sys

PCM_S = 'src/core/pcm.s'
TICK  = 3.911e-6
BANK  = 8192

def period_from_asm():
    src = open(PCM_S, encoding='utf-8').read()
    m = re.search(r'^PCM_PERIOD\s*=\s*(\d+)', src, re.M)
    if not m:
        sys.exit(f'{PCM_S} に PCM_PERIOD が見つからない')
    return int(m.group(1))

PERIOD = period_from_asm()
RATE   = int(round(1.0 / (PERIOD * TICK)))

VOWEL = {   # (F1, F2)
    'a': (800, 1200), 'i': (300, 2300), 'u': (350, 1200),
    'e': (500, 1900), 'o': (500,  900),
}

class Rng:
    def __init__(s, seed): s.v = seed
    def f(s):
        s.v = (s.v * 25173 + 13849) & 0xFFFF
        return ((s.v >> 8) & 0xFF) / 127.5 - 1.0

rng = Rng(0x2B17)

def glottal(t, f0):
    """声帯の振動。鋸波(倍音が豊富でフォルマントが乗りやすい)。"""
    p = (t * f0) % 1.0
    return 2.0 * p - 1.0

def reson(x, prev, f, bw, rate):
    """2 次共鳴フィルタ 1 段。prev=[y1,y2] を破壊的に更新して出力を返す。"""
    r = math.exp(-math.pi * bw / rate)
    c = 2.0 * r * math.cos(2.0 * math.pi * f / rate)
    y = x + c * prev[0] - r * r * prev[1]
    prev[1] = prev[0]; prev[0] = y
    return y * (1.0 - r)

_ph = [0.0]   # 声帯の位相。音節をまたいで連続させる(切れるとプチッと鳴る)

def vowel(dur, v, f0=130.0, amp=1.0, f0_end=None):
    """母音。dur 秒。f0_end を与えると音の高さが動く(語尾を下げる等)。
       ★わずかな揺れ(ジッタ)を入れる。完全に一定の高さは人間の声には無い。"""
    n = int(dur * RATE)
    p1 = [0.0, 0.0]; p2 = [0.0, 0.0]
    F1, F2 = VOWEL[v]
    out = []
    for i in range(n):
        t = i / RATE
        f = f0 if f0_end is None else f0 + (f0_end - f0) * (i / max(1, n - 1))
        f *= 1.0 + 0.012 * math.sin(2.0 * math.pi * 5.5 * t) + 0.006 * rng.f()
        g = glottal(t, f)
        y = reson(g, p1, F1, 90, RATE) + 0.6 * reson(g, p2, F2, 110, RATE)
        # 立ち上がり/立ち下がりを滑らかに(ぶつ切りはプチッと鳴る)
        e = min(1.0, i / (0.006 * RATE), (n - i) / (0.008 * RATE))
        out.append(amp * e * y)
    return out

def burst(dur, lo, hi, amp=1.0):
    """破裂/摩擦の雑音。lo..hi の帯域を持たせる(共鳴を 1 段かけて色を付ける)。"""
    n = int(dur * RATE)
    p = [0.0, 0.0]
    mid = (lo + hi) * 0.5
    out = []
    for i in range(n):
        y = reson(rng.f(), p, min(mid, RATE * 0.45), (hi - lo), RATE)
        e = math.exp(-i / (0.25 * n + 1))
        out.append(amp * e * y)
    return out

def silence(dur):
    return [0.0] * int(dur * RATE)

def nasal(dur, amp=0.7, f0=120.0):
    """鼻音(ん/な行)。低いフォルマントだけ。高さは語の抑揚に合わせる。"""
    n = int(dur * RATE)
    p = [0.0, 0.0]
    out = []
    for i in range(n):
        g = glottal(i / RATE, f0)
        y = reson(g, p, 250, 120, RATE)
        e = min(1.0, i / (0.01 * RATE), (n - i) / (0.01 * RATE))
        out.append(amp * e * y)
    return out

# ---- 音節。子音の作りを種類ごとに変える。a=高さ(始) b=高さ(終) g=強さ ----
def syl_k(v, d, a, b, g):   return silence(0.022) + burst(0.012, 1200, 3000, 0.9*g) + vowel(d, v, a, g, b)
def syl_t(v, d, a, b, g):   return silence(0.022) + burst(0.010, 2000, 3800, 0.9*g) + vowel(d, v, a, g, b)
def syl_g(v, d, a, b, g):   return nasal(0.018, 0.35*g, a) + burst(0.010, 800, 2200, 0.7*g) + vowel(d, v, a, g, b)
def syl_d(v, d, a, b, g):   return nasal(0.016, 0.35*g, a) + burst(0.009, 1200, 2600, 0.7*g) + vowel(d, v, a, g, b)
def syl_s(v, d, a, b, g, fric=0.050, famp=0.55):
    return burst(fric, 2500, 3900, famp*g) + vowel(d, v, a, g, b)
def syl_sh(v, d, a, b, g):  return burst(0.055, 1800, 3400, 0.6*g) + vowel(d, v, a, g, b)
def syl_h(v, d, a, b, g):   return burst(0.042, 900, 2400, 0.45*g) + vowel(d, v, a, g, b)
def syl_v(v, d, a, b, g):   return vowel(d, v, a, g, b)
def mora_n(d, a, g):        return nasal(d, 0.7*g, a)     # 撥音「ん」
def sokuon(d):              return silence(d)             # 促音「っ」

# ---- 台本。各音に (長さ, 高さ始, 高さ終, 強さ) を与える ----
#   ★叫びの形: 語頭を高く入り、語中はゆるやかに下げ、**語尾で一度張り上げてから落とす**。
#     語尾の母音は長く取る(0.26〜0.30秒)。ここが「叫んでいる」感じを決める。
#   ★拍の長短: 長音の後半(そ"う"、しょ"う")と「ん」は詰める。アクセント核は強く。
WORDS = {
    # 乾坤一擲(けんこんいってき)
    #   ★アクセントは **kèṅkoṅíttékí**(2026-10-06 ユーザー指定)。
    #     「けんこん」が低く、「いってき」で高く上がる。
    #     ★最初これを逆に作った(語頭をいちばん高くして下げた)。日本語として別の語に聞こえる。
    #       抑揚は「それっぽく」付けるものではなく、語ごとに決まっている。推測で付けない。
    'kenkon_itteki': (
        syl_k('e', 0.105, 128, 132, 0.92) +   # ケ(低く入る)
        mora_n(0.075, 134, 0.78) +            # ン
        syl_k('o', 0.100, 136, 139, 0.90) +   # コ
        mora_n(0.070, 141, 0.78) +            # ン
        syl_v('i', 0.090, 176, 178, 1.00) +   # イ(ここで跳ね上がる)
        sokuon(0.070) +                       # ッ(無音)
        syl_t('e', 0.100, 180, 180, 1.00) +   # テ(高いまま)
        syl_k('i', 0.290, 180, 118, 1.00)     # キ(高いまま入り、叫びの終わりとして落とす)
    ),
    # 敵撃破(てきげきは)
    #   ★アクセントは **tèkígékíhá**(2026-10-06 ユーザー指定)。最初の1拍だけ低く、あとは高いまま。
    'teki_gekiha': (
        syl_t('e', 0.105, 128, 132, 0.92) +   # テ(低く入る)
        syl_k('i', 0.090, 176, 178, 1.00) +   # キ(ここから高い)
        syl_g('e', 0.105, 177, 175, 0.96) +   # ゲ
        syl_k('i', 0.090, 174, 172, 0.92) +   # キ
        syl_h('a', 0.300, 178, 110, 1.00)     # ハ(高いまま入り、叫びの終わりとして落とす)
    ),
    # 総大将撃破(そうだいしょうげきは)
    #   ★アクセントは **sòúdáíshóúgékíhá**(2026-10-06 ユーザー指定)。
    #     最初の1拍だけ低く、あとは高いまま。長いので高いところを**ごく緩やかに**下げて単調さを避ける。
    #   ★★語頭を強く・長くしてある。理由: 「てきげきは」と**末尾の「げきは」が共通**なので、
    #     頭の「そうだいしょう」が潰れると**どちらも「…げきは」としか聞こえない**。
    #     実際ユーザーに「最終面クリアで『敵撃破』に聞こえた」と指摘された(語の選択は正しく、
    #     鳴っていたのは総大将撃破だった。実測 A4=5099)。
    #     「そ」は摩擦音で、3995Hz では高域が落ちて弱い。しかも低く入るので一番埋もれやすい。
    #     → 摩擦を長く(0.050→0.085)・強く(0.55→0.85)し、母音も長めに取って頭を立てる。
    'soudaishou_gekiha': (
        syl_s('o', 0.165, 128, 134, 1.00, fric=0.085, famp=0.85) +   # ソ(摩擦を長く強く＝頭を立てる)
        syl_v('u', 0.070, 175, 174, 0.90) +   # ウ(長音の後半。少し伸ばす)
        syl_d('a', 0.120, 176, 174, 1.00) +
        syl_v('i', 0.085, 173, 172, 0.92) +
        syl_sh('o', 0.145, 174, 172, 1.00) +
        syl_v('u', 0.065, 171, 170, 0.86) +
        syl_g('e', 0.105, 173, 171, 0.94) +
        syl_k('i', 0.090, 170, 168, 0.88) +
        syl_h('a', 0.300, 176, 108, 1.00)     # ハ(叫びの終わりとして落とす)
    ),
}

def to_pcm(sig):
    peak = max(abs(v) for v in sig) or 1.0
    s = 118.0 / peak
    out = [max(1, min(255, int(round(128 + v * s)))) for v in sig]
    for k in range(8):                      # 末尾は中心へ(切り際のプチッを消す)
        a = (k + 1) / 8.0
        j = len(out) - 8 + k
        out[j] = int(round(out[j] * (1 - a) + 128 * a))
    out[-1] = 128
    return out

def write_header(path, blobs, banks):
    """★バンク番号は Makefile から受け取る。ここで二重に持つと必ずずれる。"""
    lines = [
        '/* 自動生成(tools/gen_voice.py)。手で編集しない。',
        f'   叫び 3 つ: 8bit unsigned(中心 0x80) / {RATE}Hz。1 語 1 バンクに置き、',
        '   **RAM へ写さず窓(0xA000)から直接鳴らす**(voice_play)。',
        '   読み/抑揚: 乾坤一擲=kenkon itteki(kèṅkoṅíttékí) / 敵撃破=teki gekiha(tèkígékíhá)',
        '             / 総大将撃破=soudaishou gekiha(sòúdáíshóúgékíhá) */',
        '#ifndef VOICE_DATA_H', '#define VOICE_DATA_H',
    ]
    for (name, pcm), bank in zip(blobs, banks):
        u = name.upper()
        lines.append(f'#define VOICE_{u}_BANK {bank}')
        lines.append(f'#define VOICE_{u}_LEN  {len(pcm)}')
    lines += ['#endif /* VOICE_DATA_H */', '']
    open(path, 'w').write('\n'.join(lines))

def write_bins(prefix, blobs):
    for name, pcm in blobs:
        open(f'{prefix}_{name}.bin', 'wb').write(bytes(pcm))

def write_wav(path, blobs):
    data = b''
    gap = bytes([128] * int(RATE * 0.6))
    for _, pcm in blobs:
        data += bytes(pcm) + gap
    hdr = (b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
           struct.pack('<IHHIIHH', 16, 1, 1, RATE, RATE, 1, 8) +
           b'data' + struct.pack('<I', len(data)))
    open(path, 'wb').write(hdr + data)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit('usage: gen_voice.py <out.h> [preview.wav] [bin_prefix] [bank...]')
    blobs = [(k, to_pcm(v)) for k, v in WORDS.items()]
    banks = [int(a) for a in sys.argv[4:]] or list(range(64, 64 + len(blobs)))
    if len(banks) != len(blobs):
        sys.exit(f'バンク番号が {len(banks)} 個。{len(blobs)} 個必要')
    for (name, pcm), b in zip(blobs, banks):
        if len(pcm) > BANK:
            sys.exit(f'{name} が {len(pcm)}B で 1 バンク({BANK}B)に収まらない')
        print(f'  {name:20s} {len(pcm):5d}B  {len(pcm)/RATE*1000:6.0f}ms  bank{b}')
    write_header(sys.argv[1], blobs, banks)
    if len(sys.argv) > 2: write_wav(sys.argv[2], blobs)
    if len(sys.argv) > 3: write_bins(sys.argv[3], blobs)
    print(f'voice: {RATE}Hz / {len(blobs)} 語 → {sys.argv[1]}')
