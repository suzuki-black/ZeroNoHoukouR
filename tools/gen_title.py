#!/usr/bin/env python3
"""gen_title.py — タイトル画(PNG)を SCREEN12(YJK 自然画, 256x212)へ変換する。

入力: assets/title_src.png(元画像を 4:3 に切り抜いて縮めたもの。元は Copilot で作ったタイトル画 1536x1024)
出力: assets/title.yjk(54272B = 256x212, 1 画素 1 バイト)。"PRESS SPACE KEY" などの文字も絵に焼き込まれている。

YJK は 4 画素ごとに色(J,K)を 1 組だけ持ち、明るさ Y(5bit)だけが画素ごと。
  R = Y + J,  G = Y + K,  B = (5Y - 2J - K) / 4      (J,K は -32..31)
4 画素ぶんの色を平均した J,K の周り ±2 を探し、各画素の Y を最適に選んで RGB の誤差が最小になる組を採る
(平均だけだと、色の境目で滲みが強く出る)。

使い方:
  python3 tools/gen_title.py src <元画像.png>   # 切り抜き(4:3)＋縮小して assets/title_src.png を作る
  python3 tools/gen_title.py preview           # build/title_preview.png(変換→復号した見た目を 2 倍で)
  python3 tools/gen_title.py encode            # assets/title.yjk を書き出す(プレビューも更新)
"""
import os, sys
from PIL import Image

W, H = 256, 212
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'assets', 'title_src.png')
DST = os.path.join(HERE, '..', 'assets', 'title.yjk')
PREVIEW = os.path.join(HERE, '..', 'build', 'title_preview.png')
# 元画像 1536x1024(3:2)から 4:3 を切り抜く。★右と上を詰めると、右上の「Made with AI」が枠の外に出て、
#   タイトル文字がちょうど中央に来る。
CROP = (0, 40, 1290, 1008)

def clamp(v, lo, hi): return lo if v < lo else hi if v > hi else v

def dec(Y, J, K):
    return (clamp(Y + J, 0, 31), clamp(Y + K, 0, 31), clamp((5 * Y - 2 * J - K) >> 2, 0, 31))

def best_y(r, g, b, J, K):
    # この画素に最も近い Y(0..31)。誤差は RGB の二乗和(緑を少し重く)
    y0 = clamp(int(round((2 * r + g + 4 * b) / 8)), 0, 31)
    best = None
    for Y in range(max(0, y0 - 3), min(31, y0 + 3) + 1):
        R, G, B = dec(Y, J, K)
        e = 2 * (R - r) ** 2 + 3 * (G - g) ** 2 + (B - b) ** 2
        if best is None or e < best[0]: best = (e, Y)
    return best

def encode_group(px4):
    p = [(r / 255 * 31, g / 255 * 31, b / 255 * 31) for (r, g, b) in px4]
    js = ks = 0.0
    for (r, g, b) in p:
        y = (2 * r + g + 4 * b) / 8
        js += r - y; ks += g - y
    J0 = clamp(int(round(js / 4)), -32, 31); K0 = clamp(int(round(ks / 4)), -32, 31)
    best = None
    for J in range(max(-32, J0 - 2), min(31, J0 + 2) + 1):
        for K in range(max(-32, K0 - 2), min(31, K0 + 2) + 1):
            tot = 0; ys = []
            for (r, g, b) in p:
                e, Y = best_y(r, g, b, J, K); tot += e; ys.append(Y)
            if best is None or tot < best[0]: best = (tot, J, K, ys)
    _, J, K, Ys = best
    return bytes([(Ys[0] << 3) | (K & 7), (Ys[1] << 3) | ((K >> 3) & 7),
                  (Ys[2] << 3) | (J & 7), (Ys[3] << 3) | ((J >> 3) & 7)])

def decode(d):
    im = Image.new('RGB', (W, H)); px = im.load()
    for y in range(H):
        for gx in range(64):
            b = d[y * 256 + gx * 4: y * 256 + gx * 4 + 4]
            K = (b[0] & 7) | ((b[1] & 7) << 3); J = (b[2] & 7) | ((b[3] & 7) << 3)
            if K >= 32: K -= 64
            if J >= 32: J -= 64
            for i in range(4):
                R, G, B = dec(b[i] >> 3, J, K)
                px[gx * 4 + i, y] = (R * 255 // 31, G * 255 // 31, B * 255 // 31)
    return im

def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else 'preview'
    if mode == 'src':
        im = Image.open(sys.argv[2]).convert('RGB').crop(CROP)
        im.resize((640, 480), Image.LANCZOS).save(SRC)
        print('src:', SRC); return
    src = Image.open(SRC).convert('RGB').resize((W, H), Image.LANCZOS)
    sp = src.load()
    out = bytearray()
    for y in range(H):
        for gx in range(64):
            out += encode_group([sp[gx * 4 + i, y] for i in range(4)])
    os.makedirs(os.path.dirname(PREVIEW), exist_ok=True)
    decode(out).resize((W * 2, H * 2), Image.NEAREST).save(PREVIEW)
    print('preview:', PREVIEW)
    if mode == 'encode':
        open(DST, 'wb').write(out)
        print('title.yjk:', len(out), 'B')

if __name__ == '__main__':
    main()
