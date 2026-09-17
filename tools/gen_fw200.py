#!/usr/bin/env python3
"""Fw 200 コンドル(1面の中ボス)の見下ろしシルエットを 64 方向ぶん 32x32 の 1bit に焼く。
   機体は実寸(全幅 32.85m / 全長 23.45m)を 0.94px/m で縦横比そのまま描き、8倍で回転してから面積率で2値化する。
   使い方: python3 tools/gen_fw200.py preview <out.png>   … 見本(16方向の拡大＋海の上での等倍)
           python3 tools/gen_fw200.py bin <out.bin>       … ROM 用 8192B(64コマ×128B)
   1コマ=128B は 16x16 スプライト4枚ぶんのパターン(左上/右上/左下/右下の順)。各32Bは前半16B=左8列の16行、
   後半16B=右8列の16行(MSB=左端)。スプライトパターン表の1行(128B)にそのまま載る並び。
"""
import math, sys
from PIL import Image, ImageDraw

SS = 8                 # 超解像の倍率
N = 32
PX_M = 0.94            # 1m あたりのドット数

def m(x, y):           # 機体座標(m, 機首+y, 右+x, 機体中心原点) → 超解像ピクセル(回転前・上向き)
    return (N * SS / 2 + x * PX_M * SS, N * SS / 2 - y * PX_M * SS)

def body():
    """機体中心を原点(全長の中点付近)にした多角形群。"""
    L = 23.45
    nose, tail = L * 0.48, -L * 0.52
    polys = []
    # 胴体(機首は丸み、尾部へ細る)
    polys.append([(-1.2, nose - 1.2), (-0.6, nose), (0.6, nose), (1.2, nose - 1.2),
                  (1.4, 4.0), (1.2, -3.0), (0.5, tail + 0.5), (-0.5, tail + 0.5), (-1.2, -3.0), (-1.4, 4.0)])
    # 主翼(前縁ほぼ直線、テーパー)
    le, span = 3.8, 16.4
    polys.append([(-span, le - 1.3), (-1.0, le), (1.0, le), (span, le - 1.3),
                  (span, le - 2.9), (1.0, le - 5.0), (-1.0, le - 5.0), (-span, le - 2.9)])
    # エンジンナセル 4基(前縁から前へ突き出す)
    for xe in (-9.6, -4.6, 4.6, 9.6):
        polys.append([(xe - 0.6, le + 3.2), (xe + 0.6, le + 3.2), (xe + 1.05, le + 2.0), (xe + 1.05, le - 4.0), (xe - 1.05, le - 4.0), (xe - 1.05, le + 2.0)])
    # 水平尾翼
    polys.append([(-5.4, tail + 3.6), (5.4, tail + 3.6), (5.4, tail + 1.9), (-5.4, tail + 1.9)])
    return polys

def frame(k):
    """方向 k(0=機首上, 時計回り 64 分割)の 32x32 2値(行ごとの bit リスト)。"""
    big = Image.new('L', (N * SS, N * SS), 0)
    d = ImageDraw.Draw(big)
    for p in body():
        d.polygon([m(x, y) for x, y in p], fill=255)
    big = big.rotate(-k * 360.0 / 64, resample=Image.BILINEAR, center=(N * SS / 2, N * SS / 2))
    small = big.resize((N, N), Image.BOX)
    return [[1 if small.getpixel((x, y)) >= 80 else 0 for x in range(N)] for y in range(N)]

# 行別の色(画面固定の光=上から照らす): MSX パレット番号と表示用 RGB(0..7)
ROWCOL = [14] * 6 + [4] * 20 + [5] * 6
PAL = {1: (1, 4, 5), 2: (2, 5, 6), 7: (0, 1, 3), 4: (3, 3, 3), 5: (2, 2, 2), 14: (4, 4, 5), 13: (1, 1, 1)}
def rgb(i): return tuple(v * 255 // 7 for v in PAL[i])

def sea(w, h, seed=1):
    import random
    r = random.Random(seed)
    im = Image.new('RGB', (w, h), rgb(1))
    for _ in range(w * h * 18 // 100):
        im.putpixel((r.randrange(w), r.randrange(h)), rgb(2 if r.random() < 0.57 else 7))
    return im

def paste(im, bits, ox, oy, scale=1):
    for y in range(N):
        for x in range(N):
            if bits[y][x]:
                for dy in range(scale):
                    for dx in range(scale):
                        im.putpixel((ox + x * scale + dx, oy + y * scale + dy), rgb(ROWCOL[y]))

def preview(out):
    S = 4
    cols = 8
    sheet = sea(cols * (N * S + 4) + 4, 2 * (N * S + 4) + 4 + 120)
    for i, k in enumerate(range(0, 64, 4)):
        paste(sheet, frame(k), 4 + (i % cols) * (N * S + 4), 4 + (i // cols) * (N * S + 4), S)
    # 等倍(実画面サイズ)を2倍表示で: 1周を 8 方向ぶん並べる
    strip = sea(256, 48, 7)
    for i in range(8):
        paste(strip, frame(i * 8), 4 + i * 32, 8)
    strip = strip.resize((512, 96), Image.NEAREST)
    sheet.paste(strip, (4, sheet.height - 104))
    sheet.save(out)

def frame_bytes(k):
    b = frame(k)
    out = bytearray()
    for qy, qx in ((0, 0), (0, 16), (16, 0), (16, 16)):
        for cx in (0, 8):
            for y in range(16):
                v = 0
                for x in range(8):
                    if b[qy + y][qx + cx + x]:
                        v |= 0x80 >> x
                out.append(v)
    return out

if __name__ == '__main__':
    if sys.argv[1] == 'preview':
        preview(sys.argv[2])
    elif sys.argv[1] == 'bin':
        data = b''.join(frame_bytes(k) for k in range(64))
        assert len(data) == 8192
        open(sys.argv[2], 'wb').write(data)
