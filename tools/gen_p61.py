#!/usr/bin/env python3
"""5面の中ボス P-61 ブラックウィドウ(米の双胴の夜間戦闘機)。gen_fw200.py の仕組み(部位ごとに塗る→回転→1行1色＋重ねで量子化)で
   64x64 の元絵を作り、中ボスの間だけスプライトの拡大(MAG)で 128x128 に見せる(ユーザー案: 中ボスだけでかく)。
   自機・弾は半分に縮めた絵を拡大で元の大きさに戻す。スコア等は背景に描く。
   使い方: python3 tools/gen_p61.py preview <out.png>
           python3 tools/gen_p61.py bin <a.bin> <b.bin> <c.bin>
     9方向(32分割の 12..20=真下±45°)×1024B。ROM に空きバンクが無いので影のバンクの後ろ半分へ分けて置く:
     a=1件(bank60 の 7KB〜) / b=4件(bank61 の 4KB〜) / c=4件(bank62 の 4KB〜)。常駐の読み込みの添字は 7,12..15,20..23。
     1件の並び: [0,1]本体のマス [2,3]重ねのマス [4..515]本体16マス [516..579]重ね2枠 [580..707]影(2x2=128B) [708..]色表
     (重ね→本体)。影を重ねの直後に置くので、常駐の mb_upload(448, 0) で本体・重ね・影を1回で絵の表へ書ける。
"""
import sys, os, math, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_fw200 as G
from PIL import Image

PX = 3.0   # 元絵 1m あたりのドット(全幅 20.1m ≒ 60 ドット。画面では 2 倍)
BLUE, WHITE = 107, 115

def circle(cx, cy, r, n=10):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
def star(cx, cy, r):
    return [(cx + (r if i % 2 == 0 else r * 0.42) * math.cos(math.pi / 2 + i * math.pi / 5),
             cy + (r if i % 2 == 0 else r * 0.42) * math.sin(math.pi / 2 + i * math.pi / 5)) for i in range(10)]

def p61_colors(scheme='night'):
    """(色, 多角形)。機体座標(m, 機首+y)。夜間用の黒い機体は夜の海に溶けるので、暗い灰(6)の地に明るい縁(14)、
       ガラス(14)、排気炎(12=橙)、レーダーの機首(4)で輪郭を浮かせる。双胴の尾部と、胴体上の遠隔銃塔。"""
    BODY, EDGE, GL, FIRE, NOSE, DARK = 6, 14, 14, 12, 4, 13
    out = []
    # 主翼(直線テーパー)。前縁に明るい縁
    out.append((BODY, [(-10.05, 1.2), (-2.0, 2.4), (2.0, 2.4), (10.05, 1.2), (10.05, -0.6), (2.0, -1.4), (-2.0, -1.4), (-10.05, -0.6)]))
    out.append((EDGE, [(-10.05, 1.2), (-2.0, 2.4), (-2.0, 1.9), (-10.05, 0.8)]))
    out.append((EDGE, [(10.05, 1.2), (2.0, 2.4), (2.0, 1.9), (10.05, 0.8)]))
    # 双胴(エンジンから尾部まで)と、それをつなぐ水平尾翼
    for xb in (-3.4, 3.4):
        out.append((BODY, [(xb - 0.7, 4.6), (xb + 0.7, 4.6), (xb + 0.8, 3.0), (xb + 0.45, -6.6), (xb - 0.45, -6.6), (xb - 0.8, 3.0)]))
        out.append((DARK, [(xb - 0.7, 4.6), (xb + 0.7, 4.6), (xb + 0.8, 3.8), (xb - 0.8, 3.8)]))       # カウル
        out.append((FIRE, [(xb + 0.8, 2.6), (xb + 1.1, 2.6), (xb + 1.1, 0.8), (xb + 0.8, 0.8)]))       # 排気炎
        out.append((DARK, [(xb - 0.25, -5.6), (xb + 0.25, -5.6), (xb + 0.25, -7.6), (xb - 0.25, -7.6)]))   # 垂直尾翼
    out.append((BODY, [(-4.2, -6.3), (4.2, -6.3), (4.2, -7.5), (-4.2, -7.5)]))
    out.append((EDGE, [(-4.2, -6.3), (4.2, -6.3), (4.2, -6.7), (-4.2, -6.7)]))
    # 中央の胴体(短い)と、丸いレーダーの機首、段になった風防、遠隔銃塔
    out.append((BODY, [(-1.1, 5.6), (-0.6, 7.5), (0.6, 7.5), (1.1, 5.6), (1.2, 0.0), (0.9, -3.4), (-0.9, -3.4), (-1.2, 0.0)]))
    out.append((NOSE, [(-0.9, 6.2), (-0.5, 7.5), (0.5, 7.5), (0.9, 6.2)]))
    out.append((GL, [(-0.8, 5.6), (0.8, 5.6), (0.85, 4.4), (-0.85, 4.4)]))
    out.append((GL, [(-0.7, 3.4), (0.7, 3.4), (0.75, 2.6), (-0.75, 2.6)]))
    out.append((DARK, circle(0.0, 1.0, 0.8)))
    out.append((GL, [(-0.6, -2.4), (0.6, -2.4), (0.6, -3.2), (-0.6, -3.2)]))                           # 後部の窓
    # 国籍標識(左主翼)
    out.append((BLUE, circle(-7.0, 0.3, 1.1)))
    out.append((WHITE, star(-7.0, 0.3, 1.0)))
    return out

def use_p61(px=PX):
    G.PX_M = px
    G.NDIR = 32
    G.body_color = p61_colors
    G.body = lambda: [p for c, p in p61_colors()]
    G.XB, G.XW = BLUE, WHITE

# 5面(夜戦)の基準パレット(build/stage_grade.h の5行目)
PAL5 = [(0,0,0),(0,1,2),(1,2,3),(0,2,1),(1,1,2),(1,1,2),(2,2,2),(0,0,1),(1,4,1),(1,1,1),(3,5,3),(7,1,1),(7,4,0),(0,0,1),(3,3,4),(7,7,7)]
PAL1 = [(0,0,0),(1,4,5),(2,5,6),(1,3,1),(3,3,3),(2,2,2),(6,5,3),(0,1,3),(2,5,2),(3,3,1),(4,6,4),(7,1,1),(7,4,0),(1,1,1),(4,4,5),(7,7,7)]
def rgb(pal, i): return tuple(v * 255 // 7 for v in pal[i])

def sea(pal, w, h, seed=7):
    r = random.Random(seed)
    im = Image.new('RGB', (w, h), rgb(pal, 1))
    for _ in range(w * h * 18 // 100):
        im.putpixel((r.randrange(w), r.randrange(h)), rgb(pal, 2 if r.random() < 0.57 else 7))
    return im

def draw(im, pal, q, ox, oy, s=1, col=None):
    for y in range(len(q)):
        for x in range(len(q[0])):
            if q[y][x]:
                for dy in range(s):
                    for dx in range(s):
                        px, py = ox + x * s + dx, oy + y * s + dy
                        if 0 <= px < im.width and 0 <= py < im.height:
                            im.putpixel((px, py), rgb(pal, col if col is not None else q[y][x]))

ZERO = ["0000000110000000", "0000001111000000", "0000001111000000", "0000001111000000",
        "0000000110000000", "1111111111111111", "1111111111111111", "0111111111111110",
        "0000000110000000", "0000000110000000", "0000000110000000", "0000000110000000",
        "0000011111100000", "0000011111100000", "0000000110000000", "0000000000000000"]

def preview(out):
    use_p61()
    S = 3
    sheet = Image.new('RGB', (4 * (64 * S + 8) + 8, 64 * S + 16 + 318 + 8), rgb(PAL5, 1))
    for i, (k, pal) in enumerate(((16, PAL1), (13, PAL1), (16, PAL5), (19, PAL5))):   # 真下を向く(自機へ)・少し傾く
        q, _, _ = G.quantize(G.frame_color(k, 'night'), 2)
        bg = sea(pal, 64 * S, 64 * S, 3 + i)
        draw(bg, pal, q, 0, 0, S)
        sheet.paste(bg, (8 + i * (64 * S + 8), 8))
    # 実画面(256x212)を 1.5倍: 中ボスは 2倍拡大で 128x128。自機は半分の絵(8x8)を 2倍=16x16(荒い)。スコアは背景の字(ふつうの大きさ)
    scr = sea(PAL5, 256, 212, 5)
    q, _, _ = G.quantize(G.frame_color(16, 'night'), 2)
    use_p61(PX / 2)
    shq = G.frame(16)
    draw(scr, PAL5, shq, 64 + 10, 10 + 14, 2, 13)                 # 影(月明かり。2倍で)
    draw(scr, PAL5, q, 64, 6, 2)
    half = [[ZERO[y * 2][x * 2] == '1' for x in range(8)] for y in range(8)]
    for y in range(8):
        for x in range(8):
            if half[y][x]:
                for d in range(4):
                    scr.putpixel((120 + x * 2 + (d & 1), 180 + y * 2 + (d >> 1)), rgb(PAL5, 8))
    rr = random.Random(2)
    for i in range(18):                                             # 背景の弾(曳光弾=明滅する色)
        a = i * 0.7; rad = 30 + i * 4
        bx, by = int(128 + math.cos(a) * rad), int(110 + math.sin(a) * rad * 0.6)
        for d in range(4):
            scr.putpixel((bx + (d & 1), by + (d >> 1)), rgb(PAL5, 12))
    for i, ch in enumerate("00340"):                                # 背景に描くスコア(ふつうの 8x8)
        for y in range(7):
            for x in range(6):
                if (x + y + i) % 5 == 0 or x in (0, 5) and y in range(1, 6) or y in (0, 6) and x in range(1, 5):
                    scr.putpixel((16 + i * 8 + x, 4 + y), rgb(PAL5, 15))
    sheet.paste(scr.resize((384, 318), Image.NEAREST), (8, 16 + 64 * S))
    sheet.save(out)

MB_SPR_MAX = 22   # 本体16＋重ね2＋影4(HUD を背景へ回すので枠に余裕がある)

def blob(d):
    use_p61()
    img = G.frame_color(d, 'night')
    chosen = G.pick_overlays(img, 2)
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
    for j in range(2):
        out += pat_ov[j] if j < len(pat_ov) else bytes(32)
    use_p61(PX / 2)                                  # 影: 半分の大きさの影絵を 2x2(マス 5,6,9,10)
    f = G.frame(d)
    for c in range(16):
        if c not in (5, 6, 9, 10):
            assert not any(f[(c // 4) * 16 + y][(c % 4) * 16 + x] for y in range(16) for x in range(16)), (d, c)
    for c in (5, 6, 9, 10):
        out += G.cell_bytes([[f[(c // 4) * 16 + y][(c % 4) * 16 + x] for x in range(16)] for y in range(16)])
    assert len(out) == 708
    for col in col_ov:
        out += col
    for c in range(16):
        if bm & (1 << c):
            out += col_base[c]
    n = bin(bm).count('1') + bin(om).count('1') + 4
    assert n <= MB_SPR_MAX and len(out) <= 1024, (d, n, len(out))
    return bytes(out + bytes(1024 - len(out))), n

if __name__ == '__main__' and sys.argv[1] == 'bin':
    bl = [blob(d) for d in range(12, 21)]
    open(sys.argv[2], 'wb').write(bl[0][0])
    open(sys.argv[3], 'wb').write(b''.join(b for b, n in bl[1:5]))
    open(sys.argv[4], 'wb').write(b''.join(b for b, n in bl[5:9]))
    print('p61: sprites', [n for b, n in bl])

if __name__ == '__main__' and sys.argv[1] == 'preview':
    preview(sys.argv[2])
