#!/usr/bin/env python3
"""gen_cloud.py — 1〜5面の雲(自機より上を流れる薄い雲)のスプライト 4 枚(32x32)を作る。

雲は自機や敵弾の手前に出るので、**網目(市松)にして隙間から下が見える**ようにする。
芯は市松の 50%、縁へ行くほど点を間引いて 25%→まばら、にして輪郭をぼかす(べた塗りは禁止)。

使い方:
  python3 tools/gen_cloud.py h build/cloud_pat.h      # ヘッダ(SPR_CLOUD0..3 の 32B×4)
  python3 tools/gen_cloud.py preview out.png          # 拡大プレビュー
"""
import sys, random, math

W = H = 32
SEED = 7

def density():
    # 3〜4 個の円を重ねた塊。値 0..1
    blobs = [(12, 18, 10), (21, 15, 11), (16, 11, 8), (25, 21, 7)]
    d = [[0.0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            v = 0.0
            for (cx, cy, r) in blobs:
                q = 1.0 - math.hypot(x + 0.5 - cx, (y + 0.5 - cy) * 1.25) / r
                v = max(v, q)
            d[y][x] = max(0.0, min(1.0, v * 1.6))
    return d

def bitmap():
    rnd = random.Random(SEED)
    d = density()
    bm = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            v = d[y][x]
            if v <= 0.02:
                continue
            chk = (x + y) & 1 == 0                  # 市松 50%
            if v > 0.55:
                on = chk
            elif v > 0.25:
                on = chk and ((x // 2 + y // 2) & 1 == 0 or rnd.random() < 0.35)   # 25〜35%
            else:
                on = chk and rnd.random() < v * 1.2   # 縁はまばら
            bm[y][x] = 1 if on else 0
    return bm

def pattern(bm, ox, oy):
    # 16x16 の 32B。★バイト順は「左半分 16 行 → 右半分 16 行」(sprites のパターン表の並び)
    out = []
    for half in (0, 1):
        for y in range(16):
            b = 0
            for x in range(8):
                if bm[oy + y][ox + half * 8 + x]:
                    b |= 0x80 >> x
            out.append(b)
    return out

def main():
    bm = bitmap()
    pats = [pattern(bm, 0, 0), pattern(bm, 16, 0), pattern(bm, 0, 16), pattern(bm, 16, 16)]
    if sys.argv[1] == 'h':
        with open(sys.argv[2], 'w') as f:
            f.write('/* 自動生成: tools/gen_cloud.py。雲の 32x32 = 左上/右上/左下/右下 */\n')
            for i, p in enumerate(pats):
                f.write('static const u8 pat_cloud%d[32] = {\n    %s\n};\n' % (i, ','.join('0x%02X' % v for v in p)))
    else:
        from PIL import Image
        im = Image.new('RGB', (W, H), (40, 100, 140))
        for y in range(H):
            for x in range(W):
                if bm[y][x]:
                    im.putpixel((x, y), (240, 240, 240))
        im.resize((W * 10, H * 10), Image.NEAREST).save(sys.argv[2])

if __name__ == '__main__':
    main()
