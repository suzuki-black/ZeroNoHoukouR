#!/usr/bin/env python3
"""gen_boss.py — 最終面ボス(Douglas XB-19)のスプライトコマを生成する。

  python3 tools/gen_boss.py preview out.png   … 全コマを海の上に並べたシート(各コマの枚数と1走査線の最大枚数つき)
  python3 tools/gen_boss.py bin vram.bin misc.bin frames.h
      vram.bin  … VRAM(page2, y=528〜)へ焼くコマ。1コマ=パターン 6 行(24枚×32B)＋色表 3 行(24枚×16B)
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
ENTRY = [('N', 16, 120), ('N', 22, 95), ('N', 28, 72), ('N', 36, 52), ('N', 44, 34),
         ('N', 52, 20), ('N', 60, 9), ('M', 32, 4, 3), ('M', 40, 0, 3), ('M', 48, 0, 3),
         ('M', 56, 0, 3), ('M', 64, 0, 3), ('M', 72, 0, 3)]
BANK = [('M', 72, -10, 3), ('M', 72, 10, 3)]  # 左右移動の傾き
DEATH = [('M', 72, 25), ('M', 64, 55), ('M', 56, 90), ('M', 48, 125), ('M', 40, 160),
         ('M', 32, 195), ('N', 56, 230), ('N', 48, 265), ('N', 40, 300), ('N', 32, 335),
         ('N', 24, 370), ('N', 16, 405)]
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

def build_frame(mode, width, angle, max_rows=None):
    img = render(width, angle)
    H, W = len(img), len(img[0])
    plan = allocate(img, max_rows)
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
    return dict(mode=mode, width=width, angle=angle, W=W, H=H, sprites=sprites, img=out,
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

def write_bin(frames, vram_path, misc_path, h_path):
    vram = bytearray()
    for f in frames:
        pats = bytearray(); cols = bytearray()
        for (dx, dy, pat, col, tx, ty) in f['sprites']:
            pats += pat_bytes(pat); cols += bytes(col)
        pats += bytes(24 * 32 - len(pats)); cols += bytes(24 * 16 - len(cols))
        vram += pats + cols
    open(vram_path, 'wb').write(vram)
    open(misc_path, 'wb').write(card_image())
    ne, nb = len(ENTRY), len(BANK)
    L = []
    L.append('/* frames.h — tools/gen_boss.py が生成(手で直さない)。最終面ボス XB-19 のコマ表。 */')
    L.append(f'#define BOSS_NF {len(frames)}')
    L.append(f'#define BOSS_F_ENTRY0 0')
    L.append(f'#define BOSS_F_FULL {ne - 1}          /* 登場の最後=通常時のコマ */')
    L.append(f'#define BOSS_F_BANKL {ne}')
    L.append(f'#define BOSS_F_BANKR {ne + 1}')
    L.append(f'#define BOSS_F_DEATH0 {ne + nb}')
    first_m = next(i for i, f in enumerate(frames) if f['mode'] == 'M')
    L.append(f'#define BOSS_F_FIRSTMAG {first_m}   /* 登場でここから上の帯を MAG にする */')
    L.append(f'#define BOSS_VRAM_Y 528   /* page2。1コマ=9行(パターン6＋色表3) */')
    L.append(f'#define BOSS_VRAM_LEN {len(vram)}')
    L.append('#ifdef BOSS_FRAME_TABLES   /* 表はオーバレイ(ovl_final.c)だけが持つ。常駐は長さの定数だけ使う */')
    L.append('static const u8 boss_fmag[BOSS_NF] = { ' + ','.join('1' if f['mode'] == 'M' else '0' for f in frames) + ' };')
    L.append('static const u8 boss_fn[BOSS_NF] = { ' + ','.join(str(len(f['sprites'])) for f in frames) + ' };')
    L.append('/* 各コマのスプライト位置: 格子の原点(絵の中心からの絵ドット) ＋ 各枚の格子座標(上位4bit=列/下位4bit=段)。')
    L.append('   左上 = 中心 + (原点 + 列*16) * 倍率。MAG コマは倍率 2。 */')
    L.append('static const s8 boss_fox[BOSS_NF] = { ' + ','.join(str(f['fox']) for f in frames) + ' };')
    L.append('static const s8 boss_foy[BOSS_NF] = { ' + ','.join(str(f['foy']) for f in frames) + ' };')
    L.append('static const u8 boss_ftile[BOSS_NF][24] = {')
    for f in frames:
        tl = [str((sp[4] << 4) | sp[5]) for sp in f['sprites']] + ['0'] * (24 - len(f['sprites']))
        L.append('  { ' + ','.join(tl) + ' },')
    L.append('};')
    L.append('/* 各コマのスプライトの縦の範囲(絵ドット, 中心から)。★1走査線8枚は「スプライトの 16 行ぶん全部」で数えるので、')
    L.append('   絵の端ではなくスプライトの端が壁の行に掛かると壁が欠ける(一度そうなった)。位置のクランプはこちらで行う */')
    L.append('static const s8 boss_ftop[BOSS_NF] = { ' + ','.join(str(min((sp[1] for sp in f['sprites']), default=0)) for f in frames) + ' };')
    L.append('static const s8 boss_fbot[BOSS_NF] = { ' + ','.join(str(max((sp[1] + 16 for sp in f['sprites']), default=0)) for f in frames) + ' };')
    L.append('#endif')
    open(h_path, 'w').write('\n'.join(L) + '\n')
    print(f'vram {len(vram)}B, misc {len(card_image())}B, {len(frames)} frames', file=sys.stderr)

def main():
    if len(sys.argv) < 3: raise SystemExit(__doc__)
    frames = []
    for spec in FRAMES:
        f = build_frame(*spec); frames.append(f)
        m, w, a = spec[:3]
        print(f"{len(frames)-1:2d} {m}{w:3d} {a:4d}deg  {f['W']}x{f['H']}  sprites={f['total']:2d}  max/line={f['rowmax']}",
              file=sys.stderr)
    if sys.argv[1] == 'preview':
        preview(frames, sys.argv[2])
    elif sys.argv[1] == 'bin':
        write_bin(frames, sys.argv[2], sys.argv[3], sys.argv[4])

if __name__ == '__main__':
    main()
