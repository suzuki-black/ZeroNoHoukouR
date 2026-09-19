#!/usr/bin/env python3
"""gen_grade.py — 面ごとの時間帯・天候のパレット(色調)を作る。

  python3 tools/gen_grade.py h out.h                 … オーバレイ(ovl_palette.c)が持つ 5面×16色の表
  python3 tools/gen_grade.py preview in.png out.png  … openMSX のスクショを各面の色調で塗り直した見本

★色番号の所有者(ovl_palette.c 冒頭の調査)に沿って決める:
  1/2/7 = 海(7 は艦の影と共用) / 11,12,15 = 弾・爆発・HUD の数字(どの面でも読めるよう明るさを残す)
★基準は vdp_palette_game と同じ昼の色。各面は「式で全体の色調を作る → 海と保護色を上書き」。
"""
import sys
from PIL import Image

BASE = [(0,0,0),(1,4,5),(2,5,6),(1,3,1),(3,3,3),(2,2,2),(6,5,3),(0,1,3),
        (2,5,2),(3,3,1),(4,6,4),(7,1,1),(7,4,0),(1,1,1),(4,4,5),(7,7,7)]
KEEP = {11, 12, 15}          # 弾/爆発/数字は色調を掛けない
def cl(v): return max(0, min(7, int(round(v))))

def grade(stage):
    out = []
    for i, (r, g, b) in enumerate(BASE):
        if stage == 0 or i in KEEP or i == 0:
            out.append((r, g, b)); continue
        if stage == 1:   # 2面 空母: 夕焼け(赤みを足し、青を落とす)
            c = (r * 0.95 + 1.2, g * 0.8 + 0.2, b * 0.6)
        elif stage == 2: # 3面 フッド: 荒天(彩度を落として暗く、青灰へ)＋ときどき稲光(ovl_palette.c)
            m = (r + g + b) / 3
            c = ((r + m) / 2 * 0.8, (g + m) / 2 * 0.85, (b + m) / 2 * 0.9 + 0.3)
        elif stage == 3: # 4面 双子: 朝霧(白く持ち上げてコントラストを落とす)
            c = (r * 0.6 + 2.4, g * 0.6 + 2.6, b * 0.6 + 2.8)
        else:            # 5面 アイオワ: 夜戦(暗く青く)
            c = (r * 0.35, g * 0.4, b * 0.55 + 0.5)
        out.append(tuple(cl(v) for v in c))
    # 海(1/2/7)は式でなく手で決める
    SEA = {1: {1: (2,2,4), 2: (5,3,3), 7: (1,1,2)},
           2: {1: (1,2,3), 2: (2,3,4), 7: (0,1,1)},
           3: {1: (3,4,5), 2: (5,6,6), 7: (2,3,4)},
           4: {1: (0,1,2), 2: (1,2,3), 7: (0,0,1)}}
    for k, v in SEA.get(stage, {}).items(): out[k] = v
    # ★自機(zcol: 3,5,8,10,14)が暗い面でも読めるように、夜と荒天は緑と淡灰を持ち上げる
    if stage == 4:
        out[8] = (1, 4, 1); out[10] = (3, 5, 3); out[14] = (3, 3, 4); out[3] = (0, 2, 1)
    if stage == 2:
        out[8] = (2, 4, 2); out[10] = (3, 5, 3)
        out[9] = (3, 3, 2); out[3] = (1, 2, 1)       # 3面の敵機(スピットファイア: 9/3/10)が沈まないように
    if stage == 3:
        out[13] = (0, 0, 1); out[14] = (2, 2, 3)     # 4面の敵機(Fw190: 14/13/15)が霧の海に溶けないように暗く
    return out

NAMES = ['1 BISMARCK (day)', '2 CARRIER (sunset)', '3 HOOD (storm)', '4 TWINS (fog)', '5 IOWA (night)']

def rgb(c): return tuple(v * 255 // 7 for v in c)

def preview(inp, outp):
    im = Image.open(inp).convert('RGB')
    base_rgb = [rgb(c) for c in BASE]
    W, H = im.size
    px = im.load()
    idx = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            p = px[x, y]
            idx[y][x] = min(range(16), key=lambda k: sum((p[j] - base_rgb[k][j]) ** 2 for j in range(3)))
    from PIL import ImageDraw
    sheet = Image.new('RGB', (W * 5, H + 16), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    for s in range(5):
        pal = [rgb(c) for c in grade(s)]
        o = Image.new('RGB', (W, H)); q = o.load()
        for y in range(H):
            for x in range(W):
                q[x, y] = pal[idx[y][x]]
        sheet.paste(o, (s * W, 16)); d.text((s * W + 4, 2), NAMES[s], fill=(255, 255, 255))
    sheet.save(outp)

def header(outp):
    L = ['/* stage_grade.h — tools/gen_grade.py が生成(手で直さない)。面ごとの時間帯・天候の基準パレット [面][色][r,g,b]',
         '   PAL_ONLY_STAGE を定義すると、その面の1行だけの表になる(枠の狭いオーバレイ用。4面の中ボス)。 */',
         '#ifdef PAL_ONLY_STAGE',
         'static const u8 pal_stage[1][16][3] = {']
    for s in range(5):
        L.append('#%s PAL_ONLY_STAGE == %d' % ('if' if s == 0 else 'elif', s))
        L.append('  { ' + ', '.join('{%d,%d,%d}' % c for c in grade(s)) + ' },')
    L += ['#endif', '};', '#else', 'static const u8 pal_stage[5][16][3] = {']
    for s in range(5):
        L.append('  { ' + ', '.join('{%d,%d,%d}' % c for c in grade(s)) + ' },')
    L += ['};', '#endif']
    open(outp, 'w').write('\n'.join(L) + '\n')

if __name__ == '__main__':
    if sys.argv[1] == 'h': header(sys.argv[2])
    else: preview(sys.argv[2], sys.argv[3])
