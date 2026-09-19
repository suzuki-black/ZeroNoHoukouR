#!/usr/bin/env python3
"""He 111(4面の中ボス, 2機)の見下ろし。gen_fw200.py の仕組み(部位ごとに塗る→回転→1行1色＋重ねで量子化)を使い、
   機体の形と色だけ差し替える。4面は双子艦なので中ボスも2機。走査線でスプライト表(R#5)と絵の表(R#6)を切り替え、
   上の帯=1機目 / 下の帯=2機目 をそれぞれ 64x64 で描く。2機は画面の中心について点対称に動く(2機目の向き=1機目+180°)。
   ★1面(Fw 200=4発・直線翼・緑の迷彩)/2面(PBY=飛行艇)と見分けがつくよう、楕円翼・全面ガラスの丸い機首・灰色2色の分割迷彩。
   使い方: python3 tools/gen_he111.py preview <out.png>
           python3 tools/gen_he111.py bin <out.bin> <shadow.bin>   … 32方向×1024B(並びは gen_fw200.py と同じ)と影 32方向×128B。
     2機目は 向き+16 の絵を使う。
   ★1機あたり 本体＋重ね(最大2)＋影4 で 18 枚以下(スプライト表は上下の帯で別々なので、2機でも各帯 18 枚)。
     ★最初は朝霧を理由に影を省いたが「影が無い」と実機で指摘 → 影(30 ドット=2x2)を足し、重ねを 2 までに減らした。
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_fw200 as G
from PIL import Image, ImageDraw

PX = 2.65   # 全幅 22.6m ≒ 60 ドット

def he_colors(scheme='grey'):
    """(色, 多角形)。機体座標(m, 機首+y)。全長 16.4m / 全幅 22.6m。明るい灰に暗い灰の分割迷彩。"""
    A, B, BK, GL = 4, 14, 13, 15
    XB, XW = G.XB, G.XW
    nose, tail = 7.6, -8.8
    out = []
    # 楕円に近い主翼(前縁ほぼ直線、後縁が翼端へ前進して丸く閉じる)
    le = [(0, 1.6), (5.0, 1.5), (8.5, 1.0), (10.5, 0.3), (11.3, -0.6)]
    te = [(11.0, -1.5), (8.5, -2.1), (5.0, -2.7), (1.0, -3.2)]
    wing = [(x, y) for x, y in le] + te + [(-x, y) for x, y in reversed(te)] + [(-x, y) for x, y in reversed(le)]
    out.append((A, wing))
    # 分割迷彩: 大きさも角度も揃えない多角形(揃えると縞模様に見えた)
    out.append((B, [(-11.5, 0.6), (-7.5, 1.9), (-6.2, -0.6), (-8.8, -2.6), (-11.5, -1.2)]))
    out.append((B, [(-4.2, 1.9), (-1.0, 1.9), (-2.2, -3.4), (-5.6, -3.4)]))
    out.append((B, [(2.4, 1.9), (6.8, 1.9), (5.2, -0.8), (7.9, -3.4), (3.4, -3.4)]))
    out.append((B, [(9.4, 1.2), (11.5, 0.2), (11.5, -1.6), (9.8, -2.2), (8.6, -0.6)]))
    # 胴体(細い葉巻形)と全面ガラスの丸い機首
    out.append((A, [(-0.85, 5.8), (-0.6, nose - 0.3), (0.6, nose - 0.3), (0.85, 5.8), (0.9, 1.0), (0.7, -5.0), (0.3, tail), (-0.3, tail), (-0.7, -5.0), (-0.9, 1.0)]))
    out.append((B, [(-0.9, 0.5), (0.9, -1.0), (0.8, -4.0), (-0.8, -2.5)]))
    out.append((GL, [(-0.75, 6.2), (-0.5, nose - 0.3), (0.0, nose), (0.5, nose - 0.3), (0.75, 6.2)]))
    out.append((BK, [(-0.8, 6.2), (0.8, 6.2), (0.8, 5.8), (-0.8, 5.8)]))                  # ガラスの枠")
    out.append((BK, [(-0.3, -1.2), (0.3, -1.2), (0.3, -2.2), (-0.3, -2.2)]))           # 背部銃座
    # エンジン2基(環状ラジエーターの黒い前面)
    for xe in (-4.3, 4.3):
        out.append((A, [(xe - 0.6, 3.6), (xe + 0.6, 3.6), (xe + 0.75, 2.8), (xe + 0.6, -2.8), (xe - 0.6, -2.8), (xe - 0.75, 2.8)]))
        out.append((BK, [(xe - 0.6, 3.6), (xe + 0.6, 3.6), (xe + 0.75, 2.9), (xe - 0.75, 2.9)]))
    # 尾翼(楕円の水平尾翼＋中心線の垂直尾翼)
    out.append((A, [(-4.0, -6.8), (-2.5, -6.4), (2.5, -6.4), (4.0, -6.8), (4.0, -7.9), (2.5, -8.6), (-2.5, -8.6), (-4.0, -7.9)]))
    out.append((B, [(0.0, -6.4), (2.5, -6.4), (4.0, -6.8), (4.0, -7.9), (2.5, -8.6), (0.0, -8.6)]))
    out.append((BK, [(-0.2, -6.0), (0.2, -6.0), (0.2, -8.9), (-0.2, -8.9)]))
    # 国籍標識(主翼上面の両側。白縁の黒十字)
    for xc in (-8.0, 8.0):
        out.append((XW, [(xc - 1.2, -0.1), (xc + 1.2, -0.1), (xc + 1.2, -2.3), (xc - 1.2, -2.3)]))
        out.append((XB, [(xc - 0.4, -0.1), (xc + 0.4, -0.1), (xc + 0.4, -2.3), (xc - 0.4, -2.3)]))
        out.append((XB, [(xc - 1.2, -0.8), (xc + 1.2, -0.8), (xc + 1.2, -1.6), (xc - 1.2, -1.6)]))
    return out

def use_he():
    G.PX_M = PX
    G.NDIR = 32
    G.body_color = he_colors
    G.body = lambda: [p for c, p in he_colors()]

# 4面(朝霧)の基準パレット(build/stage_grade.h の4行目)
PAL4 = [(0,0,0),(3,4,5),(5,6,6),(3,4,3),(4,4,5),(4,4,4),(6,6,5),(2,3,4),(4,6,4),(4,4,3),(5,6,5),(7,1,1),(7,4,0),(0,0,1),(2,2,3),(7,7,7)]
PAL1 = [(0,0,0),(1,4,5),(2,5,6),(1,3,1),(3,3,3),(2,2,2),(6,5,3),(0,1,3),(2,5,2),(3,3,1),(4,6,4),(7,1,1),(7,4,0),(1,1,1),(4,4,5),(7,7,7)]
def rgb(pal, i): return tuple(v * 255 // 7 for v in pal[i])

def sea(pal, w, h, seed=7):
    import random
    r = random.Random(seed)
    im = Image.new('RGB', (w, h), rgb(pal, 1))
    for _ in range(w * h * 18 // 100):
        im.putpixel((r.randrange(w), r.randrange(h)), rgb(pal, 2 if r.random() < 0.57 else 7))
    return im

def draw(im, pal, q, ox, oy, scale=1):
    for y in range(G.N):
        for x in range(G.N):
            if q[y][x]:
                for dy in range(scale):
                    for dx in range(scale):
                        px, py = ox + x * scale + dx, oy + y * scale + dy
                        if 0 <= px < im.width and 0 <= py < im.height:
                            im.putpixel((px, py), rgb(pal, q[y][x]))

def preview(out):
    S = 3
    use_he()
    W = 4 * (G.N * S + 8) + 8
    sheet = Image.new('RGB', (W, G.N * S + 16 + 318 + 8), rgb(PAL4, 1))
    # 1段目: 向き4つ。左2つ=昼のパレット(本来の色) / 右2つ=4面の朝霧
    for i, (k, pal) in enumerate(((0, PAL1), (4, PAL1), (0, PAL4), (4, PAL4))):
        q, _, _ = G.quantize(G.frame_color(k, 'grey'), 6)
        bg = Image.new('RGB', (G.N * S, G.N * S), rgb(pal, 1))
        draw(bg, pal, q, 0, 0, S)
        sheet.paste(bg, (8 + i * (G.N * S + 8), 8))
    # 2段目: 実画面(256x212)を1.5倍で2枚。点対称の2機(上=表A / 下=表B)。赤い点線=その瞬間の分割線(実際の画面には出ない)
    for j, (k, a) in enumerate(((12, (64, 58)), (6, (92, 50)))):
        scr = sea(PAL4, 256, 212)
        qa, _, _ = G.quantize(G.frame_color(k, 'grey'), 6)
        qb, _, _ = G.quantize(G.frame_color((k + 16) % 32, 'grey'), 6)
        b = (256 - a[0], 212 - a[1])
        draw(scr, PAL4, qa, a[0] - 32, a[1] - 32)
        draw(scr, PAL4, qb, b[0] - 32, b[1] - 32)
        split = (a[1] + b[1]) // 2
        for x in range(0, 256):
            if (x >> 2) & 1:
                for d in (0, 1):
                    scr.putpixel((x, split + d), (220, 40, 40))
        for y in range(16):
            for x in range(16):
                scr.putpixel((120 + x, 188 + y), rgb(PAL4, 8))
        sheet.paste(scr.resize((384, 318), Image.NEAREST), (8 + j * 392, 16 + G.N * S))
    sheet.save(out)

MB_SPR_MAX = 18

def blob(d):
    use_he()
    img = G.frame_color(d, 'grey')
    nbody = sum(1 for c in range(16) if any(img[(c // 4) * 16 + y][(c % 4) * 16 + x] for y in range(16) for x in range(16)))
    chosen = G.pick_overlays(img, max(0, min(2, MB_SPR_MAX - 4 - nbody)))
    bm = om = 0
    pat_base, col_base, pat_ov, col_ov = [bytes(32)] * 16, {}, [], []
    for c in range(16):
        base, bc, ov, oc = G.split_cell(img, c, c in chosen)
        if any(any(r) for r in base):
            bm |= 1 << c
            pat_base[c] = G.cell_bytes(base); col_base[c] = bytes(bc)
        if c in chosen:
            om |= 1 << c
            pat_ov.append(G.cell_bytes(ov)); col_ov.append(bytes(oc))
    out = bytearray(bm.to_bytes(2, 'little') + om.to_bytes(2, 'little'))
    for c in range(16):
        out += pat_base[c]
    for j in range(6):
        out += pat_ov[j] if j < len(pat_ov) else bytes(32)
    for col in col_ov:
        out += col
    for c in range(16):
        if bm & (1 << c):
            out += col_base[c]
    n = bin(bm).count('1') + bin(om).count('1') + 4
    assert n <= MB_SPR_MAX and len(out) <= 1024, (d, n)
    return bytes(out + bytes(1024 - len(out))), n

if __name__ == '__main__' and sys.argv[1] == 'bin':
    data, ns = b'', []
    for d in range(32):
        b, n = blob(d); data += b; ns.append(n)
    open(sys.argv[2], 'wb').write(data)
    sh = b''
    for d in range(32):                   # 影: 半分の大きさ(30 ドット)の影絵を 2x2(マス 5,6,9,10)で
        use_he(); G.PX_M = PX / 2
        f = G.frame(d)
        for c in (5, 6, 9, 10):
            sh += G.cell_bytes([[f[(c // 4) * 16 + y][(c % 4) * 16 + x] for x in range(16)] for y in range(16)])
    open(sys.argv[3], 'wb').write(sh)
    print('he111: sprites max', max(ns), 'min', min(ns))

if __name__ == '__main__' and sys.argv[1] == 'preview':
    preview(sys.argv[2])
