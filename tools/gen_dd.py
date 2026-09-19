#!/usr/bin/env python3
"""3面の中ボス: フッドの護衛の駆逐艦(英 トライバル級, 全長 115m)。背景(page1 のリング)へ横向きに1回だけ描き、
   走査線の分割(R#26/R#27)でその帯ごと左右へずらして動かす(描き直し無し)。見下ろし・艦首は右。
   ★潜水艦案は「ゲームに潜水艦が出てこない」とユーザー指摘 → 登場理由の自然な護衛の駆逐艦(案B)。
   使い方: python3 tools/gen_dd.py preview <out.png>
           python3 tools/gen_dd.py bin <out.bin>        … 176x24 ドットを 4bit(左の画素が上位)で 1 行 128B(88B 使用)×24 行=3072B。
                                                          0=透明(海を残す)。1KB にちょうど 8 行(読み込みの区切りが行の途中に来ない)
           python3 tools/gen_dd.py turrets              … 砲塔と煙突の x(絵の左端から)を表示
"""
import sys, random, math
from PIL import Image, ImageDraw
from collections import Counter

SS = 8
PX = 1.3            # 1m あたりのドット数 → 全長 115m ≒ 150 ドット(左右へ動ける幅を残す)
W, H = 176, 24      # 艦首の波と艦尾の航跡を含む
L = 115.0

PAL3 = [(0,0,0),(1,2,3),(2,3,4),(1,2,1),(2,3,3),(2,2,2),(4,4,4),(0,1,1),(2,4,2),(3,3,2),(3,5,3),(7,1,1),(7,4,0),(1,1,1),(3,4,4),(7,7,7)]
PAL1 = [(0,0,0),(1,4,5),(2,5,6),(1,3,1),(3,3,3),(2,2,2),(6,5,3),(0,1,3),(2,5,2),(3,3,1),(4,6,4),(7,1,1),(7,4,0),(1,1,1),(4,4,5),(7,7,7)]
DECK_RGB = {id(PAL1): (3, 3, 3), id(PAL3): (2, 2, 3)}   # 9 番は中ボスの間だけ艦の灰に差し替える(被弾でこの色だけ白く光らせる)
def rgb(pal, i):
    c = DECK_RGB[id(pal)] if i == 9 else pal[i]
    return tuple(v * 255 // 7 for v in c)

BOW_X = W - 16       # 艦首の先端(ドット)
def m(x, y):         # 艦の座標(m, 艦首 +x, 右舷 +y, 中心原点)
    return ((BOW_X - L / 2 * PX) * SS + x * PX * SS, H * SS / 2 + y * PX * SS)

def circle(cx, cy, r, n=10):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]

def parts():
    """(色, 多角形)。黒い縁取りの船体、灰の甲板(9=中ボス専用の色にする予定=被弾でこの色だけ白く光らせる)、
       明るい上部構造、黒い煙突の頂、砲塔(砲身は自機を向くスプライトで重ねる)、白い内火艇"""
    OUT, DECK, SUPER, DARK, TUR, BOAT = 13, 9, 14, 13, 4, 15
    b = L / 2
    hull = [(b, 0), (b - 14, -4.8), (-b + 20, -5.6), (-b + 3, -4.4), (-b, -2.0), (-b, 2.0), (-b + 3, 4.4), (-b + 20, 5.6), (b - 14, 4.8)]
    out = [(OUT, [(x * 1.01, y * 1.18) for x, y in hull]), (DECK, hull)]
    out.append((5, [(b - 6, 0), (b - 18, -2.0), (b - 18, 2.0)]))                      # 艦首の錨甲板
    for tx in (b - 24, b - 35, -b + 18, -b + 29):                                      # 砲塔 A・B(前) X・Y(後)
        out.append((OUT, circle(tx, 0, 3.0)))
        out.append((TUR, circle(tx, 0, 2.5)))
        out.append((SUPER, circle(tx + 0.6, -0.6, 1.0, 6)))
    out.append((OUT, [(b - 40, -4.2), (b - 54, -4.4), (b - 54, 4.4), (b - 40, 4.2)]))   # 艦橋
    out.append((SUPER, [(b - 41, -3.6), (b - 53, -3.8), (b - 53, 3.8), (b - 41, 3.6)]))
    out.append((DARK, [(b - 42, -2.4), (b - 45, -2.4), (b - 45, 2.4), (b - 42, 2.4)]))  # 艦橋の窓
    for fx in (b - 60, b - 72):                                                         # 煙突 2本(頂は黒)
        out.append((OUT, [(fx + 3.6, -3.0), (fx - 3.6, -3.0), (fx - 3.6, 3.0), (fx + 3.6, 3.0)]))
        out.append((SUPER, [(fx + 3, -2.5), (fx - 3, -2.5), (fx - 3, 2.5), (fx + 3, 2.5)]))
        out.append((DARK, [(fx + 2.0, -1.6), (fx - 2.0, -1.6), (fx - 2.0, 1.6), (fx + 2.0, 1.6)]))
    out.append((SUPER, [(b - 79, -1.6), (b - 88, -1.6), (b - 88, 1.6), (b - 79, 1.6)]))  # 四連装魚雷発射管
    out.append((DARK, [(b - 80, -0.6), (b - 87, -0.6), (b - 87, 0.6), (b - 80, 0.6)]))
    out.append((BOAT, [(b - 62, 3.6), (b - 70, 3.6), (b - 70, 4.8), (b - 62, 4.8)]))     # 内火艇
    out.append((BOAT, [(b - 62, -3.6), (b - 70, -3.6), (b - 70, -4.8), (b - 62, -4.8)]))
    return out

def render(seed=4):
    big = Image.new('L', (W * SS, H * SS), 0)
    d = ImageDraw.Draw(big)
    for c, p in parts():
        d.polygon([m(x, y) for x, y in p], fill=c)
    px = big.load()
    img = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            cnt = Counter(px[x * SS + i, y * SS + j] for i in range(SS) for j in range(SS))
            c, n = cnt.most_common(1)[0]
            img[y][x] = c if n > SS * SS // 3 else 0
    r = random.Random(seed)
    stern = int(BOW_X - L * PX)
    cy = H // 2
    for y in range(H):
        for x in range(W):
            v = img[y][x]
            if v == 9 and r.random() < 0.10:
                img[y][x] = 5                                   # 甲板のむら(べた塗りにしない)
            elif v == 9 and r.random() < 0.04:
                img[y][x] = 2                                   # 甲板を洗う波
            elif not v:
                dx = BOW_X + 2 - x
                if 0 <= dx < 26 and abs(abs(y - cy) - (dx * 2) // 5 - 1) <= 1 and r.random() < (0.9 if dx < 14 else 0.4):
                    img[y][x] = 15                              # 艦首の波
                elif x < stern + 6 and abs(y - cy) <= (stern + 6 - x) // 4 + 2 and r.random() < 0.5:
                    img[y][x] = 15 if r.random() < 0.5 else 2   # 艦尾の航跡
    return img

def sea(pal, w, h, seed=7):
    rr = random.Random(seed)
    im = Image.new('RGB', (w, h), rgb(pal, 1))
    for _ in range(w * h * 18 // 100):
        im.putpixel((rr.randrange(w), rr.randrange(h)), rgb(pal, 2 if rr.random() < 0.57 else 7))
    return im

def draw(im, pal, img, ox, oy, s=1):
    for y in range(H):
        for x in range(W):
            if img[y][x]:
                for dy in range(s):
                    for dx in range(s):
                        im.putpixel((ox + x * s + dx, oy + y * s + dy), rgb(pal, img[y][x]))

def barrel(im, pal, cx, cy, ang):
    for i in range(3, 10):
        x = cx + int(round(math.sin(ang) * i)); y = cy + int(round(math.cos(ang) * i))
        im.putpixel((x, y), rgb(pal, 13)); im.putpixel((x + 1, y), rgb(pal, 13))

def preview(out):
    S = 2
    sheet = Image.new('RGB', (W * S + 16, (H * S + 8) * 2 + 16 + 318), rgb(PAL3, 1))
    for i, pal in enumerate((PAL1, PAL3)):
        bg = sea(pal, W * S, H * S, 11 + i)
        draw(bg, pal, render(), 0, 0, S)
        sheet.paste(bg, (8, 8 + i * (H * S + 8)))
    # 実画面を 1.5倍: 艦は 32..63 行の帯(その帯ごと左右へずらして動かす)、ほかの帯は荒天のうねり(左右 ±4)。左端 8 ドットは隠す
    scr = sea(PAL3, 256, 212, 5)
    img = render()
    draw(scr, PAL3, img, 40, 36)
    tx = [BOW_X - int(24 * PX), BOW_X - int(35 * PX), int(BOW_X - L * PX + 18 * PX), int(BOW_X - L * PX + 29 * PX)]
    for t in tx:
        barrel(scr, PAL3, 40 + t, 36 + H // 2, 0.35)
    for y in range(16):
        for x in range(16):
            scr.putpixel((150 + x, 184 + y), rgb(PAL3, 8))
    wav = Image.new('RGB', (256, 212), (0, 0, 0))
    for band in range(7):
        off = int(round(4 * math.sin(band * 1.3)))
        y0, y1 = band * 32, min(212, band * 32 + 32)
        wav.paste(scr.crop((0, y0, 256, y1)), (off, y0))
    for x in range(8):
        for y in range(212):
            wav.putpixel((x, y), (0, 0, 0))
    sheet.paste(wav.resize((384, 318), Image.NEAREST), (8, 8 + 2 * (H * S + 8)))
    sheet.save(out)

TURRETS = lambda: [BOW_X - int(24 * PX), BOW_X - int(35 * PX), int(BOW_X - L * PX + 18 * PX), int(BOW_X - L * PX + 29 * PX)]
FUNNELS = lambda: [BOW_X - int(60 * PX), BOW_X - int(72 * PX)]

if __name__ == '__main__' and sys.argv[1] == 'preview':
    preview(sys.argv[2])
if __name__ == '__main__' and sys.argv[1] == 'bin':
    img = render()
    out = bytearray()
    for y in range(H):
        for x in range(0, W, 2):
            out.append((img[y][x] << 4) | img[y][x + 1])
        out += bytes(128 - W // 2)
    open(sys.argv[2], 'wb').write(out)
if __name__ == '__main__' and sys.argv[1] == 'turrets':
    print('turrets', TURRETS(), 'funnels', FUNNELS(), 'bow', BOW_X, 'stern', int(BOW_X - L * PX))
