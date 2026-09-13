#!/usr/bin/env python3
"""gen_boss.py — 最終面ボス(Douglas XB-19)のスプライトコマを生成する。

  python3 tools/gen_boss.py preview out.png   … 全コマを海の上に並べたシート(各コマの枚数と1走査線の最大枚数つき)
  python3 tools/gen_boss.py bin vram_a.bin vram_b.bin misc.bin frames.h
      vram_a.bin … VRAM page2/3 (y=528〜)へ焼くコマ。1コマ=パターン 6 行＋色表 3 行＋位置情報 1 行
      vram_b.bin … 入り切らない残りのコマ(page0 y=32〜。表示が page1 に切り替わってから焼く)
      misc.bin  … 開始カードの絵(64x48, 1536B)
      frames.h  … オーバレイが持つ各コマの枚数/モード/スプライト位置

★元絵: assets/xb19_mask.png(上面視シルエット 480x318)。出典は 1942-07-18 の XB-19 技術報告書の
  三面図(Howard G. Bunker, Materiel Division, Air Corps, War Department)＝米連邦政府の職務著作で PD。
  Commons: File:Douglas_XB-19_3-view_line_drawing_(technical_report).png。docs/苦労と教訓 §15 参照。

★コマの規則(実機の制約。破ると表示が欠ける):
  - スプライトは 16x16。1 枚につき 1 行 1 色。2 枚重ねて OR 色(CC ビット)にすると 1 行 3 色。
  - **1 走査線に 8 枚まで**。同じ格子行(16 行)にあるスプライトは全行で数える(絵が無い行も数える)。
  - ボスの表に使える枚数は BOSS_MAX 枚(残りは壁)。
  - mode 'N' = 等倍(表示 = 絵のドット) / 'M' = 拡大(MAG。表示は絵の 2 倍)。
  ツールは格子の位相(ox, oy)を 16x16 通り試し、制約を満たす中で色の誤差が最小の置き方を選ぶ。
★光は画面の左上から(機体が回っても光源は動かない)。ノイズは機体に貼り付いて一緒に回る。
"""
import sys, math, struct
from PIL import Image, ImageDraw

PAL = [(0,0,0),(1,4,5),(2,5,6),(1,3,1),(3,3,3),(2,2,2),(6,5,3),(0,1,3),
       (2,5,2),(3,3,1),(4,6,4),(7,1,1),(7,4,0),(1,1,1),(4,4,5),(7,7,7)]
RGB = [tuple(v * 255 // 7 for v in c) for c in PAL]
BOSS_MAX = 24
PER_LINE = 8

# ---- コマ列: (mode, 絵の幅px, 角度deg) ----
# 登場: 高空から旋回しながら降りてくる(等倍で大きくなる → MAG に切り替えてさらに大きく)
# ★最大は M72(表示 144x96)。画面は上から HUD 帯(〜20行)/ボス帯/壁(16行)/自機帯。ボス帯に 96 行が入る大きさ。
# ★拡大コマ(死亡以外)はスプライトの縦を 3 段(=96 ライン)までに制限する: ボス帯は HUD 帯と壁の間の 104 ライン。
def lerp(a, b, t): return a + (b - a) * t
NN, NM, ND = 13, 13, 24
ENTRY = ([('N', round(lerp(16, 60, (i / (NN - 1)) ** 0.9)), round(lerp(120, 9, i / (NN - 1)))) for i in range(NN)] +
         [('M', round(lerp(32, 72, i / (NM - 1))), round(lerp(4, 0, i / (NM - 1))), 3) for i in range(NM)])
# 左右移動の傾き(片側 3 段階。間を補間しながら替える)。並び: 左1,左2,左3,右1,右2,右3
BANK = [('M', 72, -4, 3), ('M', 72, -7, 3), ('M', 72, -10, 3), ('M', 72, 4, 3), ('M', 72, 7, 3), ('M', 72, 10, 3)]
# 撃墜: きりもみしながら縮む(前半 12 コマは MAG、後半 12 コマは等倍)
DEATH = ([('M', round(lerp(72, 32, i / 11)), round(lerp(25, 200, i / 11))) for i in range(12)] +
         [('N', round(lerp(56, 16, i / 11)), round(lerp(215, 405, i / 11))) for i in range(12)])
FRAMES = ENTRY + BANK + DEATH

# ---- 元絵(シルエット)と機体の部位 ----
MASK = Image.open('assets/xb19_mask.png').convert('L')
MW, MH = MASK.size
MP = MASK.load()
CX = (MW - 1) / 2.0
def m_at(u, v):
    x, y = int(u), int(v)
    return 0 <= x < MW and 0 <= y < MH and MP[x, y] > 0

# 部位の位置(元絵の比率。三面図の上面図から取った)
WING0, WING1 = 56 / 105, 76 / 105          # 主翼の後縁/前縁の行(比率)
NACELLE_U = [55 / 159, 69 / 159, 92 / 159, 105 / 159]
NACELLE_TIP = 86 / 105
TURRETS = [0.406, 0.749]                   # 背面銃座(胴体上)
# 胴体の半幅(行ごと): 主翼より上の胴体だけの行から測り、主翼の行は補間
fw = [None] * MH
for y in range(MH):
    c = int(CX)
    if not MP[c, y] or (WING0 * MH - 6 <= y <= NACELLE_TIP * MH + 4) or y < 24 / 105 * MH:
        continue
    a = b = c
    while a > 0 and MP[a - 1, y]: a -= 1
    while b < MW - 1 and MP[b + 1, y]: b += 1
    if b - a + 1 < MW * 0.09: fw[y] = (b - a + 1) / 2
known = [y for y in range(MH) if fw[y]]
for y in range(int(24 / 105 * MH), MH):
    if fw[y] is None:
        lo = [k for k in known if k < y]; hi = [k for k in known if k > y]
        if lo and hi:
            a, b = lo[-1], hi[0]; fw[y] = fw[a] + (fw[b] - fw[a]) * (y - a) / (b - a)
NAC_R = MW / 64.0

def hash2(a, b):
    h = (a * 73856093) ^ (b * 19349663)
    h = (h ^ (h >> 13)) * 1274126177
    return (h ^ (h >> 16)) & 0xFFFF

def part_color(u, v, lit, dark, s):
    """元絵座標(u,v)の色。lit/dark = 画面座標で光側/影側の縁か。s = 絵ドット/元絵ドット。
    ★小さく描くと細部が 1px 未満になって消えるので、部位の太さに**絵ドット単位の下限**を付ける
      (三面図どおりより特徴を誇張する＝ドット絵の作法)。"""
    px = 1.0 / s                                  # 絵の 1 ドットを元絵座標で
    vy = v / MH
    lead = WING1 * MH; trail = WING0 * MH
    # 背面銃座(2x2 以上のドーム)
    for t in TURRETS:
        d = math.hypot(u - CX, v - t * MH)
        r = max(MW / 60, 1.3 * px)
        if d < r:
            return 15 if (u < CX and v < t * MH) else (14 if d < r * 0.7 else 5)
    # 機首のガラス
    if vy > 0.955 and abs(u - CX) < max(MW * 0.03, 1.5 * px):
        return 14 if (int(v / px) & 1) else 7
    # エンジンナセル＋プロペラ(前縁より前へ誇張して突き出す)
    nr = max(NAC_R, 1.5 * px)
    tip = max(NACELLE_TIP * MH, lead + 3.5 * px)
    for nu in NACELLE_U:
        nx = nu * MW
        if abs(v - (tip + 1.0 * px)) < 0.5 * px and abs(u - nx) < max(MW * 0.045, 4.5 * px):
            return 14 if (int((u - nx) / px) & 1) else 4      # 回転ブラーの円板(点線)
        if abs(u - nx) <= nr and trail + (lead - trail) * 0.30 <= v <= tip:
            t = (u - nx) / nr
            if v > tip - 1.0 * px: return 13                    # カウル先端
            return 6 if t < -0.35 else (9 if t < 0.35 else 3)
    # 国籍マーク(機体の左翼=画面右側の翼)
    ix, iy = 0.80 * MW, (trail + (lead - trail) * 0.30)
    di = math.hypot(u - ix, v - iy)
    ri = max(MW / 40, 1.6 * px)
    if di < ri:
        return 15 if di < ri * 0.45 else 7
    # 胴体(円筒の陰影。下限 4 ドット幅)
    fy = fw[int(v)] if 0 <= int(v) < MH else None
    if fy:
        fy = max(fy, 2.0 * px)
        if abs(u - CX) <= fy:
            t = (u - CX) / fy
            if dark: return 13
            return 6 if t < -0.3 else (9 if t < 0.35 else 3)
    # 主翼/尾翼
    if dark: return 13
    if lit: return 6
    # 動翼の線(後縁から 1/4 翼弦)
    if trail - 2 * px < v < lead and abs(v - (trail + (lead - trail) * 0.22)) < 0.5 * px and abs(u - CX) > MW * 0.12:
        return 3
    n = hash2(int(u / max(3, px)), int(v / max(3, px))) % 100
    return 3 if n < 9 else (6 if n < 13 else 9)

def shape_at(u, v, px):
    """被覆: シルエット＋誇張した部位(ナセルの突き出し・プロペラ円板・胴体の下限幅)。"""
    if m_at(u, v): return True
    lead = WING1 * MH; trail = WING0 * MH
    tip = max(NACELLE_TIP * MH, lead + 3.5 * px)
    nr = max(NAC_R, 1.5 * px)
    for nu in NACELLE_U:
        nx = nu * MW
        if abs(u - nx) <= nr and trail <= v <= tip: return True
        if abs(v - (tip + 1.0 * px)) < 0.5 * px and abs(u - nx) < max(MW * 0.045, 4.5 * px): return True
    fy = fw[int(v)] if 0 <= int(v) < MH else None
    if fy and abs(u - CX) <= max(fy, 2.0 * px): return True
    return False

def render(width, angle):
    """(絵の幅px, 角度) の 1 コマを描く。戻り値: 2次元配列(None=透明)。"""
    s = width / MW
    ca, sa = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    R = math.hypot(MW, MH) * s / 2 + 2
    N = int(R * 2) + 2
    c0 = N / 2
    def local(px, py, sub=0.5):
        dx, dy = px + sub - c0, py + sub - c0
        u = (dx * ca + dy * sa) / s + CX
        v = (-dx * sa + dy * ca) / s + MH / 2
        return u, v
    cov = [[False] * N for _ in range(N)]
    for py in range(N):
        for px in range(N):
            hit = 0
            for sx in (0.25, 0.75):
                for sy in (0.25, 0.75):
                    u, v = local(px + sx - 0.5, py + sy - 0.5)
                    hit += shape_at(u, v, 1.0 / s)
            cov[py][px] = hit >= 2
    img = [[None] * N for _ in range(N)]
    def inside(x, y): return 0 <= x < N and 0 <= y < N and cov[y][x]
    for py in range(N):
        for px in range(N):
            if not cov[py][px]: continue
            lit = not inside(px - 1, py) or not inside(px, py - 1)
            dark = not inside(px + 1, py) or not inside(px, py + 1)
            u, v = local(px, py)
            img[py][px] = part_color(u, v, lit and not dark, dark, s)
    # 余白を詰める
    ys = [y for y in range(N) if any(img[y])]
    xs = [x for x in range(N) if any(img[y][x] is not None for y in range(N))]
    if not ys: return [[None]]
    out = [row[xs[0]:xs[-1] + 1] for row in img[ys[0]:ys[-1] + 1]]
    return out

def d2(a, b): return sum((PAL[a][k] - PAL[b][k]) ** 2 for k in range(3))
# ★2枚重ねの第3色(OR)が機体に無い色になる組は使わない(橙や赤の粒が散って汚くなった)。
BOSS_COLS = {3, 4, 5, 6, 7, 9, 13, 14, 15}
SINGLES = [(a,) for a in sorted(BOSS_COLS)]
PAIRS = [(a, b, a | b) for a in sorted(BOSS_COLS) for b in sorted(BOSS_COLS) if a < b and (a | b) in BOSS_COLS]
def best(cs, cands):
    bb = None
    for c in cands:
        e = sum(min(d2(v, k) for k in c) for v in cs)
        if bb is None or e < bb[0]: bb = (e, c)
    return bb

def allocate(img, max_rows=None):
    """格子の位相を試し、制約内で誤差最小の置き方。戻り値 dict。"""
    H, W = len(img), len(img[0])
    best_plan = None
    step = 1 if max_rows else 2
    for oy in range(0, 16, step):
        for ox in range(0, 16, 2):
            tiles = {}
            for y in range(H):
                for x in range(W):
                    if img[y][x] is not None:
                        tiles.setdefault(((x + ox) // 16, (y + oy) // 16), []).append((x, y))
            rows = {}
            for (tx, ty) in tiles: rows[ty] = rows.get(ty, 0) + 1
            if max(rows.values()) > PER_LINE or len(tiles) > BOSS_MAX: continue
            if max_rows and max(rows) - min(rows) + 1 > max_rows: continue
            # 各タイルの単色/2枚重ねの誤差を行ごとに
            info = {}
            for key, pix in tiles.items():
                es = ep = 0; rs = {}; rp = {}
                byrow = {}
                for (x, y) in pix: byrow.setdefault(y, []).append(img[y][x])
                for y, cs in byrow.items():
                    s1 = best(cs, SINGLES); s2 = best(cs, PAIRS)
                    es += s1[0]; ep += s2[0]; rs[y] = s1[1]; rp[y] = s2[1]
                info[key] = (es, ep, rs, rp)
            pairs = set(); total = len(tiles); rowcnt = dict(rows)
            for key in sorted(tiles, key=lambda k: info[k][1] - info[k][0]):
                gain = info[key][0] - info[key][1]
                if gain <= 0: break
                if total + 1 <= BOSS_MAX and rowcnt[key[1]] + 1 <= PER_LINE:
                    pairs.add(key); total += 1; rowcnt[key[1]] += 1
            err = sum(info[k][1] if k in pairs else info[k][0] for k in tiles)
            if best_plan is None or err < best_plan['err']:
                best_plan = dict(err=err, ox=ox, oy=oy, tiles=tiles, info=info, pairs=pairs, total=total,
                                 rowmax=max(rowcnt.values()))
    return best_plan

# ★被弾で光るコマ: 明るい灰で陰影を残す(暗い色ほど暗い灰へ)。色の組は灰だけで作り直す。
GLOW_MAP = {13: 5, 3: 4, 9: 14, 6: 15, 5: 4, 4: 14, 7: 5, 14: 15, 15: 15}
GLOW_COLS = {4, 5, 14, 15}
GSINGLES = [(a,) for a in sorted(GLOW_COLS)]
GPAIRS = [(a, b, a | b) for a in sorted(GLOW_COLS) for b in sorted(GLOW_COLS) if a < b and (a | b) in GLOW_COLS]

def recolor_plan(img, plan):
    """置き方(タイルと 2 枚重ねの位置)はそのまま、色だけ光る版の色から選び直す＝スプライト位置が同じ。"""
    info = {}
    for key, pix in plan['tiles'].items():
        rs = {}; rp = {}; byrow = {}
        for (x, y) in pix: byrow.setdefault(y, []).append(img[y][x])
        for y, cs in byrow.items():
            rs[y] = best(cs, GSINGLES)[1]; rp[y] = best(cs, GPAIRS)[1]
        info[key] = (0, 0, rs, rp)
    q = dict(plan); q['info'] = info
    return q

def build_frame(mode, width, angle, max_rows=None, glow_of=None):
    if glow_of is not None:
        base = glow_of
        img = [[None if k is None else GLOW_MAP.get(k, k) for k in row] for row in base['raw']]
        plan = recolor_plan(img, base['plan'])
    else:
        img = render(width, angle)
        plan = allocate(img, max_rows)
    H, W = len(img), len(img[0])
    if plan is None:
        raise SystemExit(f'frame {mode}{width}@{angle}: 制約内に置けない')
    sprites = []   # (dx, dy, pattern32, colors16) dx,dy は絵の中心からの絵ドット
    out = [[None] * W for _ in range(H)]
    for key in sorted(plan['tiles'], key=lambda k: (k[1], k[0])):
        tx, ty = key
        x0, y0 = tx * 16 - plan['ox'], ty * 16 - plan['oy']
        es, ep, rs, rp = plan['info'][key]
        paired = key in plan['pairs']
        pa = [0] * 16; pb = [0] * 16; ca = [0] * 16; cb = [0] * 16
        for r in range(16):
            y = y0 + r
            if not (0 <= y < H): continue
            cols = (rp if paired else rs).get(y)
            if cols is None: continue
            ca[r] = cols[0]
            if paired: cb[r] = cols[1] | 0x40   # CC=1(OR)。必ず下の番号のスプライトより後に置く
            for c in range(16):
                x = x0 + c
                if not (0 <= x < W) or img[y][x] is None: continue
                k = min(cols, key=lambda q: d2(img[y][x], q))
                out[y][x] = k
                if k == cols[0] or (paired and k == cols[2]): pa[r] |= 0x8000 >> c
                if paired and (k == cols[1] or k == cols[2]): pb[r] |= 0x8000 >> c
        dx, dy = x0 - W // 2, y0 - H // 2
        sprites.append((dx, dy, pa, ca, tx, ty))
        if paired: sprites.append((dx, dy, pb, cb, tx, ty))
    return dict(mode=mode, width=width, angle=angle, W=W, H=H, sprites=sprites, img=out, raw=img, plan=plan,
                fox=-plan['ox'] - W // 2, foy=-plan['oy'] - H // 2,
                total=plan['total'], rowmax=plan['rowmax'])

def pat_bytes(rows):
    """16x16 のパターン 32B。★並びは「左半分 16 行 → 右半分 16 行」(パターン n=左上 n+1=左下 n+2=右上 n+3=右下)。
    行ごとに左右 2B ずつ並べると絵がバラバラになる(一度そうなった)。"""
    return bytes((r >> 8) & 0xFF for r in rows) + bytes(r & 0xFF for r in rows)

def preview(frames, path):
    cell_w, cell_h = 180, 180
    cols = 7
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * cell_w, rows * cell_h), (20, 20, 20))
    dr = ImageDraw.Draw(sheet)
    import random
    rr = random.Random(5)
    for i, f in enumerate(frames):
        cx0, cy0 = (i % cols) * cell_w, (i // cols) * cell_h
        for y in range(cell_h - 14):
            for x in range(cell_w - 4):
                r = rr.random()
                sheet.putpixel((cx0 + x, cy0 + 14 + y), RGB[2] if r < 0.1 else RGB[7] if r < 0.18 else RGB[1])
        z = 2 if f['mode'] == 'M' else 1
        ox = cx0 + (cell_w - 4 - f['W'] * z) // 2
        oy = cy0 + 14 + (cell_h - 14 - f['H'] * z) // 2
        for y in range(f['H']):
            for x in range(f['W']):
                k = f['img'][y][x]
                if k is None: continue
                for a in range(z):
                    for b in range(z):
                        px, py = ox + x * z + a, oy + y * z + b
                        if cx0 <= px < cx0 + cell_w - 4 and cy0 + 14 <= py < cy0 + cell_h:
                            sheet.putpixel((px, py), RGB[k])
        dr.text((cx0 + 2, cy0 + 1), f"{i}:{f['mode']}{f['width']} {f['angle']}d  {f['total']}spr {f['rowmax']}/ln",
                fill=(255, 255, 255))
    sheet.save(path)

def card_image():
    """開始カード用 64x48(4bpp, 1行32B)。地色=1(カードの背景色)。"""
    img = render(60, 0)
    H, W = len(img), len(img[0])
    ox, oy = (64 - W) // 2, (48 - H) // 2
    out = bytearray()
    for y in range(48):
        row = []
        for x in range(64):
            k = 1
            yy, xx = y - oy, x - ox
            if 0 <= yy < H and 0 <= xx < W and img[yy][xx] is not None: k = img[yy][xx]
            elif 0 <= yy - 3 < H and 0 <= xx - 3 < W and img[yy - 3][xx - 3] is not None: k = 7   # 海に落ちる影
            row.append(k)
        for x in range(0, 64, 2): out.append((row[x] << 4) | row[x + 1])
    return bytes(out)

SHADOW_FULL = 84     # 戦闘中の影の離れ(ライン)。影の大きさ 16px ＝ 本体 144px の 1/9 の高度差
VRAM_A_FRAMES = 48   # page2/3(y=528〜1007)に入るコマ数(1コマ10行。1008〜1023 は2組目のパターン表)。残りは page0(y=32〜)へ

def shadow_patterns():
    """ボスの影(海面に落ちる小さな影 16x16)と、最終面の自機の影(遠い小さな影)。"""
    # ★本体用の render は小さいと部位を太らせる(ナセル/プロペラ)ので、16px では塊になった(実機で指摘)。
    #   影はシルエットをそのまま面積平均で縮めて、4 割以上覆う画素だけを残す。
    W = 16; H = round(W * MH / MW)
    sm = MASK.resize((W, H), Image.BOX).load()
    rows = [0] * 16
    oy = (16 - H) // 2
    for y in range(H):
        for x in range(W):
            if sm[x, y] > 60: rows[y + oy] |= 0x8000 >> x
    boss = pat_bytes(rows)
    small = ['..XX..', 'XXXXXX', '..XX..', '.XXXX.']
    rows = [0] * 16
    for y, line in enumerate(small):
        for x, ch in enumerate(line):
            if ch == 'X': rows[6 + y] |= 0x8000 >> (5 + x)
    return boss, pat_bytes(rows)

def s8b(v): return v & 0xFF

def rotated_silhouette(angle, span, cell):
    """シルエットを span ドット幅の大きさで angle 回し、cell×cell の枠の中央へ置いた 16 行のビット列。"""
    m = MASK.rotate(-angle, expand=True, resample=Image.BILINEAR)   # ★render と同じ向き(PIL は反時計回りが正)
    sc = span / MW
    w, h = max(1, round(m.width * sc)), max(1, round(m.height * sc))
    sm = m.resize((w, h), Image.BOX).load()
    rows = [0] * 16
    o = (16 - cell) // 2
    for y in range(h):
        for x in range(w):
            yy, xx = y - (h - cell) // 2 + o, x - (w - cell) // 2 + o
            if sm[x, y] > 60 and o <= yy < o + cell and o <= xx < o + cell:
                rows[yy] |= 0x8000 >> xx
    return rows

def meta_bytes(f):
    """コマの位置情報 128B(VRAM の 10 行目)。オーバレイはコマを替えるときにここだけ読む
    (位置表をオーバレイに持つと 8KB 枠に入らなかった)。
    [0]=枚数 [1]=MAG [2]=原点x [3]=原点y [4]=上端 [5]=下端 [6]=左端 [7]=右端 [8..31]=各枚の格子(列<<4|段)
    [32..63]=海面の影 16px(等倍で出す) [64..95]=同じ影の半分の解像度(拡大の帯で出すと 16px)
    [96]=影を本体から離す量(ライン)＝高度。★コマの順に一定の割合で変える(登場は海面すれすれ 0 → 戦闘の高さ、
          墜落は戦闘の高さ → 着水 0)。大きさから計算すると墜落の前半で距離がほとんど縮まらなかった(実機で指摘)。"""
    sp = f['sprites']
    m = [len(sp), 1 if f['mode'] == 'M' else 0, s8b(f['fox']), s8b(f['foy']),
         s8b(min((q[1] for q in sp), default=0)), s8b(max((q[1] + 16 for q in sp), default=0)),
         s8b(min((q[0] for q in sp), default=0)), s8b(max((q[0] + 16 for q in sp), default=0))]
    m += [(q[4] << 4) | q[5] for q in sp] + [0] * (24 - len(sp))
    m += list(pat_bytes(rotated_silhouette(f['angle'], 16, 16)))
    m += list(pat_bytes(rotated_silhouette(f['angle'], 8, 8)))
    m += [f['shadow_off']]
    return bytes(m) + bytes(128 - len(m))

def write_bin(frames, nbase, glow_src, vram_a_path, vram_b_path, misc_path, h_path):
    vram = bytearray()
    for i, f in enumerate(frames):
        pats = bytearray(); cols = bytearray()
        for (dx, dy, pat, col, tx, ty) in f['sprites']:
            pats += pat_bytes(pat); cols += bytes(col)
        pats += bytes(24 * 32 - len(pats)); cols += bytes(24 * 16 - len(cols))
        vram += pats + cols + meta_bytes(f)
    FB = 1280
    na = min(VRAM_A_FRAMES, len(frames))
    open(vram_a_path, 'wb').write(vram[:na * FB])
    open(vram_b_path, 'wb').write(vram[na * FB:])
    open(misc_path, 'wb').write(card_image())
    ne, nb = len(ENTRY), len(BANK)
    bshadow, pshadow = shadow_patterns()
    L3, R3 = frames[ne + 2], frames[ne + 5]
    L = []
    L.append('/* boss_frames.h — tools/gen_boss.py が生成(手で直さない)。最終面ボス XB-19 のコマ表。 */')
    L.append('/* 1コマ=VRAM 10行: パターン6行(24枚×32B) / 色表3行(24枚×16B) / 位置情報1行(ovl_final.c の meta) */')
    L.append(f'#define BOSS_NF {len(frames)}        /* 全コマ(光るコマを含む) */')
    L.append(f'#define BOSS_NBASE {nbase}')
    L.append(f'#define BOSS_NN {NN}         /* 登場の等倍コマ数 */')
    L.append(f'#define BOSS_NM {NM}         /* 登場の拡大コマ数 */')
    L.append(f'#define BOSS_F_FIRSTMAG {NN}')
    L.append(f'#define BOSS_F_FULL {ne - 1}          /* 登場の最後=通常時のコマ */')
    L.append(f'#define BOSS_F_BANK0 {ne}         /* 傾き: 左1,左2,左3,右1,右2,右3 */')
    L.append(f'#define BOSS_F_DEATH0 {ne + nb}')
    L.append(f'#define BOSS_ND {ND}')
    L.append(f'#define BOSS_F_GLOW0 {nbase}      /* 光るコマ: 通常,左1,左2,左3,右1,右2,右3 の順 */')
    L.append(f'#define BOSS_VRAM_A_N {na}     /* page2/3 (y=528〜) に置くコマ数。以降は page0 (y=32〜) */')
    L.append('#define BOSS_VRAM_Y 528')
    L.append('#define BOSS_VRAM0_Y 32')
    L.append(f'#define BOSS_VRAM_LEN {na * FB}')
    L.append(f'#define BOSS_VRAM0_LEN {len(vram) - na * FB}')
    L.append(f'#define BOSS_XL3 {-min(q[0] for q in L3["sprites"]) * 2}   /* 左3 のコマで左端が画面内に入る中心 X の下限 */')
    L.append(f'#define BOSS_XR3 {256 - max(q[0] + 16 for q in R3["sprites"]) * 2}   /* 右3 のコマの上限 */')
    L.append('#ifdef BOSS_FRAME_TABLES')
    L.append('/* 海面の影: ボス(16x16 のシルエット)と、最終面の自機(遠い小さな影) */')
    L.append('static const u8 player_far_shadow_pat[32] = { ' + ','.join(str(b) for b in pshadow) + ' };')
    L.append('#endif')
    open(h_path, 'w').write('\n'.join(L) + '\n')
    print(f'vram {len(vram)}B (A {na} frames / B {len(frames)-na}), {len(frames)} frames', file=sys.stderr)

def main():
    if len(sys.argv) < 3: raise SystemExit(__doc__)
    frames = []
    def log(f, tag):
        print(f"{len(frames)-1:2d} {tag:10s} {f['mode']}{f['width']:3d} {f['angle']:4d}deg  {f['W']}x{f['H']}  sprites={f['total']:2d}  max/line={f['rowmax']}",
              file=sys.stderr)
    ne_all, nb_all = len(ENTRY), len(BANK)
    for k, spec in enumerate(FRAMES):
        f = build_frame(*spec)
        if k < ne_all:            f['shadow_off'] = round(SHADOW_FULL * k / (ne_all - 1))              # 登場: 昇っていく
        elif k < ne_all + nb_all: f['shadow_off'] = SHADOW_FULL                                       # 戦闘の高さ
        else:                     f['shadow_off'] = round(SHADOW_FULL * (1 - (k - ne_all - nb_all + 1) / len(DEATH)))   # 墜落: 着水で 0
        frames.append(f); log(f, 'base')
    nbase = len(frames)
    ne = len(ENTRY)
    glow_src = [ne - 1] + list(range(ne, ne + len(BANK)))
    for gi in glow_src:
        b = frames[gi]
        f = build_frame(b['mode'], b['width'], b['angle'], glow_of=b); f['shadow_off'] = b['shadow_off']
        frames.append(f); log(f, 'glow')
    if sys.argv[1] == 'preview':
        preview(frames, sys.argv[2])
    elif sys.argv[1] == 'bin':
        write_bin(frames, nbase, glow_src, sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])

if __name__ == '__main__':
    main()
