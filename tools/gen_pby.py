#!/usr/bin/env python3
"""PBY カタリナ(2面の中ボス, 飛行艇)の見下ろし。gen_fw200.py の仕組み(部位ごとに塗る→回転→1行1色＋重ねで量子化)を使い、
   機体の形と色だけ差し替える。高度で大きさが変わる(高いほど画面に近い=大きい)。影は海面に一定の大きさで落ちる。
   ★B-25 は「1面の中ボスと何が違うの」と指摘 → 形も色も Fw 200 と全く違う飛行艇に(胴体の上に乗った一枚の長い主翼・
     船形の胴体・銃座のブリスター・海軍の青灰色)。
   使い方: python3 tools/gen_pby.py preview <out.png>
           python3 tools/gen_pby.py bin <frames.bin> <shadow.bin>
   frames.bin: 6段階の大きさ(0=自機と同じ低空 43 ドット … 5=最も高い 60 ドット)×16方向 × 1024B。添字 = 大きさ*16 + 向き。
     ★大きさ1以上(高い間)は 30 ドットの荒い元絵を最近傍で引き伸ばす(最も高い 60 ドットでちょうど 2 倍)=拡大と分かるようにドットを荒く。
     1件の並びは gen_fw200.py と同じ([0,1]本体のマス [2,3]重ねのマス [4..515]本体16マスのパターン [516..707]重ね6枠
     [708..]色表 16B×枚数(重ね→本体の順))。ただし重ねは最大2(1行2枚)、本体＋重ね＋影4 が 18 枚を超えない数まで。
   shadow.bin: 16方向 × 128B。影(海面の大きさ 32 ドット=2x2 マス)のパターン。
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_fw200 as G
from PIL import Image

PX_MAX = 1.9   # 最も高いとき: 全幅 31.7m ≒ 60 ドット
BLUE, WHITE = 107, 115   # 国籍標識(青い円/白い星)。+100 は量子化で優先する印(出力は 7 / 15)

def circle(cx, cy, r, n=10, a0=0.0, a1=2 * math.pi):
    return [(cx + r * math.cos(a0 + (a1 - a0) * i / n), cy + r * math.sin(a0 + (a1 - a0) * i / n)) for i in range(n + 1)]

def star(cx, cy, r):
    pts = []
    for i in range(10):
        a = math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.42
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    return pts

def pby_colors(scheme='navy'):
    """(色, 多角形)。機体座標(m, 機首+y)。全幅 31.7m / 全長 19.5m。海軍の青灰色。
       主翼は胴体の上に乗った一枚翼(中央は矩形、外翼はテーパー)。翼端の浮きは格納して翼端になっている。"""
    BASE, MID, DK, BK, GL = 14, 4, 5, 13, 15
    nose, tail = 9.7, -9.8
    out = []
    # 船形の胴体(翼より下にあるので先に塗る)
    out.append((MID, [(-0.9, nose - 0.4), (-0.4, nose), (0.4, nose), (0.9, nose - 0.4), (1.3, 6.5), (1.3, -2.5),
                      (0.9, -6.0), (0.35, tail), (-0.35, tail), (-0.9, -6.0), (-1.3, -2.5), (-1.3, 6.5)]))
    out.append((GL, circle(0.0, 8.7, 0.75)))                                     # 機首銃座
    out.append((BK, [(-0.9, 5.2), (0.9, 5.2), (1.0, 4.2), (-1.0, 4.2)]))        # 操縦席の窓(上からは屋根の前の暗い帯)
    for sx in (-1, 1):                                                            # 側面銃座のブリスター(胴体から張り出す)
        out.append((GL, circle(sx * 1.25, -4.6, 0.9, 8, -math.pi / 2 if sx > 0 else math.pi / 2, math.pi / 2 if sx > 0 else 3 * math.pi / 2)))
    # 尾翼(水平尾翼と、中心線に立つ垂直尾翼)
    out.append((BASE, [(-4.6, -7.2), (4.6, -7.2), (4.6, -9.0), (-4.6, -9.0)]))
    out.append((DK, [(-4.6, -8.3), (4.6, -8.3), (4.6, -9.0), (-4.6, -9.0)]))     # 昇降舵
    out.append((BK, [(-0.25, -6.0), (0.25, -6.0), (0.25, -9.9), (-0.25, -9.9)])) # 垂直尾翼
    # 主翼(胴体の上。中央部は矩形、外翼はテーパー)
    out.append((BASE, [(-15.85, 1.3), (15.85, 1.3), (15.85, -0.9), (7.0, -2.5), (-7.0, -2.5), (-15.85, -0.9)]))
    out.append((DK, [(-15.85, -0.3), (-8.5, -1.7), (-8.5, -2.3), (-15.85, -0.9)]))  # 補助翼
    out.append((DK, [(15.85, -0.3), (8.5, -1.7), (8.5, -2.3), (15.85, -0.9)]))
    out.append((MID, [(-15.85, 1.3), (-14.6, 1.3), (-14.6, -0.9), (-15.85, -0.9)]))  # 翼端(格納した浮き)
    out.append((MID, [(15.85, 1.3), (14.6, 1.3), (14.6, -0.9), (15.85, -0.9)]))
    out.append((MID, [(-0.7, 1.3), (0.7, 1.3), (0.7, -2.5), (-0.7, -2.5)]))         # 中央の歩行帯
    # エンジン2基(主翼の上、胴体の左右)
    for xe in (-3.5, 3.5):
        out.append((MID, [(xe - 0.6, 3.8), (xe + 0.6, 3.8), (xe + 0.75, 3.0), (xe + 0.6, -2.8), (xe - 0.6, -2.8), (xe - 0.75, 3.0)]))
        out.append((BK, [(xe - 0.6, 3.8), (xe + 0.6, 3.8), (xe + 0.75, 3.1), (xe - 0.75, 3.1)]))   # カウル先端
    # 国籍標識: 左主翼の上面
    out.append((BLUE, circle(-10.5, 0.0, 1.35)))
    out.append((WHITE, star(-10.5, 0.0, 1.2)))
    return out

def use_pby(px_m):
    G.PX_M = px_m
    G.body_color = pby_colors
    G.body = lambda: [p for c, p in pby_colors()]
    G.XB, G.XW = BLUE, WHITE

PAL1 = [(0,0,0),(1,4,5),(2,5,6),(1,3,1),(3,3,3),(2,2,2),(6,5,3),(0,1,3),(2,5,2),(3,3,1),(4,6,4),(7,1,1),(7,4,0),(1,1,1),(4,4,5),(7,7,7)]
# 2面(夕焼け)の基準パレット(build/stage_grade.h の2行目)
PAL2 = [(0,0,0),(2,2,4),(5,3,3),(2,3,1),(4,3,2),(3,2,1),(7,4,2),(1,1,2),(3,4,1),(4,3,1),(5,5,2),(7,1,1),(7,4,0),(2,1,1),(5,3,3),(7,7,7)]
def rgb(pal, i): return tuple(v * 255 // 7 for v in pal[i])

def sea(pal, w, h, seed=7):
    import random
    r = random.Random(seed)
    im = Image.new('RGB', (w, h), rgb(pal, 1))
    for _ in range(w * h * 18 // 100):
        im.putpixel((r.randrange(w), r.randrange(h)), rgb(pal, 2 if r.random() < 0.57 else 7))
    return im

def draw(im, pal, q, ox, oy, scale=1, col=None):
    for y in range(G.N):
        for x in range(G.N):
            if q[y][x]:
                c = rgb(pal, col if col is not None else q[y][x])
                for dy in range(scale):
                    for dx in range(scale):
                        px, py = ox + x * scale + dx, oy + y * scale + dy
                        if 0 <= px < im.width and 0 <= py < im.height:
                            im.putpixel((px, py), c)

def sprites_used(q, qo):
    nb = sum(1 for c in range(16) if any(q[(c // 4) * 16 + y][(c % 4) * 16 + x] for y in range(16) for x in range(16)))
    return nb + qo

def preview(out):
    S = 3
    W = 4 * (G.N * S + 8) + 8
    sheet = Image.new('RGB', (W, (G.N * S + 8) * 2 + 8 + 318 + 16), rgb(PAL2, 1))
    # 1段目: 高いとき(64)の向き4つ。左2つ=昼のパレット(本来の色) / 右2つ=2面の夕焼け
    for i, (k, pal) in enumerate(((0, PAL1), (4, PAL1), (0, PAL2), (4, PAL2))):
        use_pby(PX_MAX)
        q, _, no = G.quantize(G.frame_color(k, 'navy'), 4)
        bg = Image.new('RGB', (G.N * S, G.N * S), rgb(pal, 1))
        draw(bg, pal, q, 0, 0, S)
        sheet.paste(bg, (8 + i * (G.N * S + 8), 8))
        print('dir', k, 'sprites', sprites_used(q, no))
    # 2段目: 降りてくると小さくなる(夕焼け)
    for i, f in enumerate((1.0, 0.8, 0.65, 0.55)):
        use_pby(PX_MAX * f)
        q, _, no = G.quantize(G.frame_color(3, 'navy'), 4)
        draw(sheet, PAL2, q, 8 + i * (G.N * S + 8), 16 + G.N * S, S)
        print('size', int(64 * f), 'sprites', sprites_used(q, no))
    # 3段目: 実画面を 1.5倍で2枚。左=高いとき(弾は下を抜ける。影は右下へ大きく離れる) / 右=降りてきたとき(影がほぼ真下)
    use_pby(PX_MAX * 0.55)
    shadow = G.frame(3)
    for j, (f, off) in enumerate(((1.0, 44), (0.55, 6))):
        scr = sea(PAL2, 256, 212)
        draw(scr, PAL2, shadow, 96 + off, 50 + off, 1, 7)
        use_pby(PX_MAX * f)
        q, _, _ = G.quantize(G.frame_color(3, 'navy'), 4)
        draw(scr, PAL2, q, 96, 50, 1)
        for y in range(16):
            for x in range(16):
                scr.putpixel((120 + x, 184 + y), rgb(PAL2, 8))
        sheet.paste(scr.resize((384, 318), Image.NEAREST), (8 + j * 392, 24 + 2 * G.N * S))
    sheet.save(out)

NDIR16 = 16
LEVELS = [1.35, 1.46, 1.57, 1.68, 1.79, PX_MAX]   # 大きさの段階ごとの 1m のドット数。0=自機と同じ高さ(零戦 12m=16 ドットと同じ縮尺で 43 ドット)
SHADOW_PX = 1.0
MB_SPR_MAX = 18                                    # 中ボス全体(本体＋重ね＋影)のスプライト上限

COARSE_PX = PX_MAX / 2   # ★拡大の元絵(30 ドット)。高い間はこれを最近傍で引き伸ばす=ドットが荒くなって「拡大」と分かる(ユーザー要望)

def upscale(img, f):
    """64x64 の中心を基準に f 倍へ最近傍で引き伸ばす(f=2 で 1ドット=2x2)"""
    import math
    out = [[0] * G.N for _ in range(G.N)]
    h = G.N // 2
    for y in range(G.N):
        sy = h + math.floor((y - h) / f)
        for x in range(G.N):
            sx = h + math.floor((x - h) / f)
            if 0 <= sy < G.N and 0 <= sx < G.N:
                out[y][x] = img[sy][sx]
    return out

def blob(level, d):
    G.NDIR = NDIR16
    if level == 0:                       # 低空(自機と同じ高さ)は細かく描く
        use_pby(LEVELS[0])
        img = G.frame_color(d, 'navy')
    else:                                # 高い間は荒い元絵の引き伸ばし
        use_pby(COARSE_PX)
        img = upscale(G.frame_color(d, 'navy'), LEVELS[level] / COARSE_PX)
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
    assert n <= MB_SPR_MAX and len(out) <= 1024, (level, d, n, len(out))
    return bytes(out + bytes(1024 - len(out))), n

def shadow(d):
    use_pby(SHADOW_PX)
    G.NDIR = NDIR16
    f = G.frame(d)
    for c in range(16):
        if c not in (5, 6, 9, 10):
            assert not any(f[(c // 4) * 16 + y][(c % 4) * 16 + x] for y in range(16) for x in range(16)), (d, c)
    out = bytearray()
    for c in (5, 6, 9, 10):
        out += G.cell_bytes([[f[(c // 4) * 16 + y][(c % 4) * 16 + x] for x in range(16)] for y in range(16)])
    return bytes(out)

if __name__ == '__main__' and sys.argv[1] == 'bin':
    data, counts = b'', []
    for lv in range(6):
        for d in range(NDIR16):
            b, n = blob(lv, d)
            data += b; counts.append(n)
    open(sys.argv[2], 'wb').write(data)
    open(sys.argv[3], 'wb').write(b''.join(shadow(d) for d in range(NDIR16)))
    print('pby: sprites per level', [max(counts[lv * 16:(lv + 1) * 16]) for lv in range(6)])

if __name__ == '__main__' and sys.argv[1] == 'preview':
    preview(sys.argv[2])
