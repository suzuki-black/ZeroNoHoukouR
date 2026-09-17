#!/usr/bin/env python3
"""Fw 200 コンドル(1面の中ボス)の見下ろしシルエットを 32 方向ぶん 64x64 の 1bit に焼く。
   機体は実寸(全幅 32.85m / 全長 23.45m)を 1.88px/m で縦横比そのまま描き、8倍で回転してから面積率で2値化する。
   ★32x32(64方向)では「中ボスの迫力が無い」と実機で指摘 → 倍の 64x64(16x16 を 4x4=16枚)。
     コマが4倍になり VRAM の置き場(page0)に 64 方向は入らないので 32 方向。
   使い方: python3 tools/gen_fw200.py preview <out.png>   … 見本(8方向, 単色)
           python3 tools/gen_fw200.py color <out.png>     … 多色化の見本(1枚だけ/重ね6枚/灰/制約なし)
           python3 tools/gen_fw200.py bin <out.bin>       … ROM 用 32768B(32方向×1024B, 迷彩＋重ね6枚)
   ★多色化(実機で「1色だといかにもMSX」「旋回すると色が変わる」と指摘): 部位ごとの色(迷彩/エンジン/ガラス/国籍標識)を
     機体座標で塗って回転し、16x16 のマスごとに「本体(1行1色)」＋最大6マスだけ「重ね(1行1色)」で表す(1行に最大6枚)。
   1方向=1024B: [0,1]=本体のマス(bit c) [2,3]=重ねのマス [4..259]=本体 c=0..7 のパターン(砲身の枠)
     [260..515]=本体 c=8..15(小さい艦載機の枠) [516..707]=重ね j=0..5(中くらいの艦載機の枠)
     [708..995]=色表 16B×18(スロット順: 重ね j 昇順 → 本体 c 昇順。絵の無いマスは詰める)。
   パターン32Bは前半16B=左8列の16行、後半16B=右8列の16行(MSB=左端)。
"""
import math, sys
from PIL import Image, ImageDraw

SS = 8                 # 超解像の倍率
N = 64
NDIR = 32
PX_M = 1.88            # 1m あたりのドット数

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
    """方向 k(0=機首上, 時計回り NDIR 分割)の NxN 2値(行ごとの bit リスト)。"""
    big = Image.new('L', (N * SS, N * SS), 0)
    d = ImageDraw.Draw(big)
    for p in body():
        d.polygon([m(x, y) for x, y in p], fill=255)
    big = big.rotate(-k * 360.0 / NDIR, resample=Image.BILINEAR, center=(N * SS / 2, N * SS / 2))
    small = big.resize((N, N), Image.BOX)
    return [[1 if small.getpixel((x, y)) >= 80 else 0 for x in range(N)] for y in range(N)]

# 行別の色(画面固定の光=上から照らす): MSX パレット番号と表示用 RGB(0..7)
ROWCOL = [14] * 12 + [4] * 40 + [5] * 12
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
    # 実画面(256x212)に零戦(16x16)と並べた 2倍表示: 1周を 8 方向ぶん
    scr = sea(256, 212, 7)
    for i in range(4):
        paste(scr, frame(i * 4), 8 + i * 60, 16)
        paste(scr, frame((i + 4) * 4), 8 + i * 60, 100)
    for y in range(16):                     # 自機の大きさの目安(16x16 の緑)
        for x in range(16):
            scr.putpixel((120 + x, 184 + y), (60, 150, 60))
    scr.resize((512, 424), Image.NEAREST).save(out)

def frame_bytes(k):
    b = frame(k)
    out = bytearray()
    for qy in range(0, N, 16):
        for qx in range(0, N, 16):
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

# ===== 多色化の試作(見本のみ): 部位ごとの色で描き、スプライトの制約(1枚=1行1色 / 重ね枚数)で量子化する =====
PALRGB = {0: (0, 0, 0), 1: (1, 4, 5), 2: (2, 5, 6), 3: (1, 3, 1), 4: (3, 3, 3), 5: (2, 2, 2), 7: (0, 1, 3),
          8: (2, 5, 2), 9: (3, 3, 1), 12: (7, 4, 0), 13: (1, 1, 1), 14: (4, 4, 5), 15: (7, 7, 7)}
def prgb(i): return tuple(v * 255 // 7 for v in PALRGB[i])

XB, XW = 113, 115   # 国籍標識の黒/白(色番号+100。量子化で優先するための印。出力では 13/15)

def cell_rows(img, c):
    """マス c の各行の (本体の色, 重ねの色 or 0) と、重ねにする価値(gain)。
       本体=その行で一番多い色。重ね=2番目に多い色。ただし国籍標識がある行は標識を優先し、白縁より黒十字を取る
       (1行2色では白縁と黒十字を両方は出せない)。標識のあるマスは価値を大きくして重ねに選ばれやすくする
       (翼が横向きだと標識の行はオリーブが多数派になり、1行1色では消えていた=実機で指摘)。"""
    from collections import Counter
    qx, qy = (c % 4) * 16, (c // 4) * 16
    rows, gain = [], 0
    for y in range(16):
        cnt = Counter(img[qy + y][qx + x] for x in range(16) if img[qy + y][qx + x])
        if not cnt:
            rows.append((0, 0)); continue
        base = cnt.most_common(1)[0][0]
        ov = 0
        for mark in (XB, XW):
            if base != mark and cnt.get(mark):
                ov = mark; gain += 40; break
        if not ov:
            mc = cnt.most_common(2)
            if len(mc) > 1:
                ov = mc[1][0]; gain += mc[1][1]
        rows.append((base, ov))
    return rows, gain

def body_color(scheme):
    """(色, 多角形) を塗る順に。機体座標(m)。scheme: 'olive'=RLM72/73風の迷彩 / 'grey'=灰の濃淡。"""
    L = 23.45; nose, tail = L * 0.48, -L * 0.52; le, span = 3.8, 16.4
    A, B, ENG, GLASS = (9, 3, 5, 14) if scheme == 'olive' else (4, 5, 13, 14)
    out = []
    wing = [(-span, le - 1.3), (-1.0, le), (1.0, le), (span, le - 1.3), (span, le - 2.9), (1.0, le - 5.0), (-1.0, le - 5.0), (-span, le - 2.9)]
    out.append((A, wing))
    # 迷彩の切れ目(翼を斜めに区切る)
    for x0 in (-13.0, -5.5, 2.0, 9.5):
        out.append((B, [(x0, le + 0.5), (x0 + 3.2, le + 0.5), (x0 + 4.6, le - 5.5), (x0 + 1.4, le - 5.5)]))
    fus = [(-1.2, nose - 1.2), (-0.6, nose), (0.6, nose), (1.2, nose - 1.2), (1.4, 4.0), (1.2, -3.0), (0.5, tail + 0.5), (-0.5, tail + 0.5), (-1.2, -3.0), (-1.4, 4.0)]
    out.append((A, fus))
    out.append((B, [(-1.4, 1.0), (1.4, -1.5), (1.3, -5.0), (-1.3, -2.5)]))
    out.append((A, [(-5.4, tail + 3.6), (5.4, tail + 3.6), (5.4, tail + 1.9), (-5.4, tail + 1.9)]))
    out.append((B, [(0.0, tail + 3.6), (5.4, tail + 3.6), (5.4, tail + 1.9), (0.0, tail + 1.9)]))
    for xe in (-9.6, -4.6, 4.6, 9.6):
        out.append((ENG, [(xe - 0.6, le + 3.2), (xe + 0.6, le + 3.2), (xe + 1.05, le + 2.0), (xe + 1.05, le - 4.0), (xe - 1.05, le - 4.0), (xe - 1.05, le + 2.0)]))
        out.append((13, [(xe - 0.6, le + 3.2), (xe + 0.6, le + 3.2), (xe + 0.6, le + 2.3), (xe - 0.6, le + 2.3)]))   # カウル先端
    out.append((GLASS, [(-0.7, nose - 0.3), (0.7, nose - 0.3), (0.9, nose - 2.6), (-0.9, nose - 2.6)]))
    for xc in (-12.0, 12.0):              # 主翼の国籍標識(白縁＋黒)
        out.append((XW, [(xc - 1.3, le - 0.9), (xc + 1.3, le - 0.9), (xc + 1.3, le - 3.5), (xc - 1.3, le - 3.5)]))
        out.append((XB, [(xc - 0.5, le - 0.9), (xc + 0.5, le - 0.9), (xc + 0.5, le - 3.5), (xc - 0.5, le - 3.5)]))
        out.append((XB, [(xc - 1.3, le - 1.7), (xc + 1.3, le - 1.7), (xc + 1.3, le - 2.7), (xc - 1.3, le - 2.7)]))
    return out

def frame_color(k, scheme):
    """方向 k の NxN 色番号(0=透明)。超解像で部位ごとに塗って回転し、各ドットは面積の多い色。"""
    from collections import Counter
    big = Image.new('L', (N * SS, N * SS), 0)
    d = ImageDraw.Draw(big)
    for col, p in body_color(scheme):
        d.polygon([m(x, y) for x, y in p], fill=col)
    big = big.rotate(-k * 360.0 / NDIR, resample=Image.NEAREST, center=(N * SS / 2, N * SS / 2))
    sil = frame(k)
    px = big.load()
    out = [[0] * N for _ in range(N)]
    for y in range(N):
        for x in range(N):
            if not sil[y][x]:
                continue
            c = Counter(px[x * SS + i, y * SS + j] for i in range(SS) for j in range(SS))
            c.pop(0, None)
            out[y][x] = c.most_common(1)[0][0] if c else 5
    return out

def pick_overlays(img, max_ov):
    from collections import Counter
    cells = [(cell_rows(img, c)[1], c) for c in range(16)]
    chosen, band = [], Counter()
    for gain, c in sorted(cells, reverse=True):
        if len(chosen) >= max_ov or gain == 0 or band[c // 4] >= 2:
            continue
        chosen.append(c); band[c // 4] += 1
    return set(chosen)

def split_cell(img, c, overlay):
    """マス c を (本体のビット, 本体の行色, 重ねのビット, 重ねの行色) へ。色は出力用(標識の印を外す)。"""
    rows, _ = cell_rows(img, c)
    qx, qy = (c % 4) * 16, (c // 4) * 16
    base = [[0] * 16 for _ in range(16)]; ov = [[0] * 16 for _ in range(16)]
    bc, oc = [0] * 16, [0] * 16
    for y in range(16):
        b, o = rows[y]
        if not overlay:
            o = 0
        bc[y], oc[y] = b % 100, o % 100
        for x in range(16):
            v = img[qy + y][qx + x]
            if not v:
                continue
            if o and v == o:
                ov[y][x] = 1
            else:
                base[y][x] = 1
    return base, bc, ov, oc

def quantize(img, max_ov, ov_per_band=2):
    """スプライトで出せる形へ(見本用)。戻り値: (出せる絵, 本体の枚数, 重ねの枚数)"""
    if max_ov == 99:
        return [[v % 100 for v in r] for r in img], 0, 0
    res = [[0] * N for _ in range(N)]
    chosen = pick_overlays(img, max_ov) if max_ov else set()
    for c in range(16):
        base, bc, ov, oc = split_cell(img, c, c in chosen)
        qx, qy = (c % 4) * 16, (c // 4) * 16
        for y in range(16):
            for x in range(16):
                if ov[y][x]:
                    res[qy + y][qx + x] = oc[y]
                elif base[y][x]:
                    res[qy + y][qx + x] = bc[y]
    return res, 0, len(chosen)

def color_preview(out):
    S = 3
    kinds = [('olive', 6, ''), ('olive', 99, '')]
    dirs = [0, 3, 6, 8, 11, 16]
    W = len(dirs) * (N * S + 6) + 6
    sheet = Image.new('RGB', (W, len(kinds) * (N * S + 6) + 6), prgb(1))
    for row, (sch, ov, _) in enumerate(kinds):
        for i, k in enumerate(dirs):
            q, nb, no = quantize(frame_color(k, sch), ov)
            ox, oy = 6 + i * (N * S + 6), 6 + row * (N * S + 6)
            for y in range(N):
                for x in range(N):
                    if q[y][x]:
                        for dy in range(S):
                            for dx in range(S):
                                sheet.putpixel((ox + x * S + dx, oy + y * S + dy), prgb(q[y][x]))
    sheet.save(out)

MAX_OV = 6

def cell_bytes(bits16):
    out = bytearray()
    for cx in (0, 8):
        for y in range(16):
            v = 0
            for x in range(8):
                if bits16[y][cx + x]:
                    v |= 0x80 >> x
            out.append(v)
    return out

def dir_blob(k):
    img = frame_color(k, 'olive')
    chosen = pick_overlays(img, MAX_OV)
    bm = om = 0
    pat_base, col_base, pat_ov, col_ov = [bytes(32)] * 16, {}, [], []
    for c in range(16):
        base, bc, ov, oc = split_cell(img, c, c in chosen)
        if any(any(r) for r in base):
            bm |= 1 << c
            pat_base[c] = cell_bytes(base); col_base[c] = bytes(bc)
        if c in chosen:
            om |= 1 << c
            pat_ov.append(cell_bytes(ov)); col_ov.append(bytes(oc))
    blob = bytearray(bm.to_bytes(2, 'little') + om.to_bytes(2, 'little'))
    for c in range(16):
        blob += pat_base[c]
    for j in range(MAX_OV):
        blob += pat_ov[j] if j < len(pat_ov) else bytes(32)
    for col in col_ov:
        blob += col
    for c in range(16):
        if bm & (1 << c):
            blob += col_base[c]
    assert len(blob) <= 1024
    return bytes(blob + bytes(1024 - len(blob)))

if __name__ == '__main__' and sys.argv[1] == 'color':
    color_preview(sys.argv[2])
if __name__ == '__main__' and sys.argv[1] == 'bin':
    open(sys.argv[2], 'wb').write(b''.join(dir_blob(k) for k in range(NDIR)))
