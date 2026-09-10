#!/usr/bin/env python3
# gen_title.py — タイトルYJK画に「改」(毛筆)と「KAI」を合成する。
#   入力: assets/title_base.yjk (改/KAI 合成前の原画=基底。冪等に再実行するため)
#   既定はプレビューのみ(build/title_preview.png)。--encode 指定時だけ assets/title.yjk を再エンコード。
#   → 反復中はPNGだけ更新(SC12/YJK変換のトークン浪費を避ける)。承認後に --encode。
#   零の咆哮 の隣に大きく毛筆「改」、ローマ字 ZERO NO HOUKOU の隣に「KAI」を
#   同フォント(Arial Bold, スモールキャップス)/同色(白青)/同サイズ/同ベースラインで揃えて配置。
import glob, sys, os
from PIL import Image, ImageFont, ImageDraw

W, H = 256, 212
HERE = os.path.dirname(__file__)
BASE = os.path.join(HERE, '..', 'assets', 'title_base.yjk')
DST  = os.path.join(HERE, '..', 'assets', 'title.yjk')
PREVIEW = os.path.join(HERE, '..', 'build', 'title_preview.png')
KAI_FONT = '/System/Library/Fonts/Supplemental/Arial Bold.ttf'   # ZERO NO HOUKOU は Arial/Helvetica Bold 系
CORE = (224, 236, 252)     # コア(白青)
MIDC = (150, 186, 236)     # 下側の青み(縦グラデ近似)
OUTL = (24, 40, 78)        # 暗青縁

def weibei():
    for p in glob.glob('/System/Library/AssetsV2/**/WeibeiSC-Bold.otf', recursive=True):
        return p
    sys.exit('WeibeiSC-Bold.otf not found')

def clamp(v, lo, hi): return lo if v < lo else hi if v > hi else v

def decode(d):
    im = Image.new('RGB', (W, H)); px = im.load()
    for y in range(H):
        for gx in range(64):
            b = d[y*256+gx*4: y*256+gx*4+4]
            K = (b[0]&7)|((b[1]&7)<<3); J = (b[2]&7)|((b[3]&7)<<3)
            if K>=32: K-=64
            if J>=32: J-=64
            for i in range(4):
                Y = b[i]>>3
                px[gx*4+i, y] = (clamp(Y+J,0,31)*8, clamp(Y+K,0,31)*8, clamp((5*Y-2*J-K)>>2,0,31)*8)
    return im

def encode_group(rgb4):
    Ys=[]; Js=[]; Ks=[]
    for (R,G,B) in rgb4:
        R>>=3; G>>=3; B>>=3
        Y = clamp((4*B + 2*R + G)//8, 0, 31)
        Ys.append(Y); Js.append(R-Y); Ks.append(G-Y)
    J = clamp(round(sum(Js)/4), -32, 31); K = clamp(round(sum(Ks)/4), -32, 31)
    return bytes([(Ys[0]<<3)|(K&7), (Ys[1]<<3)|((K>>3)&7),
                  (Ys[2]<<3)|(J&7), (Ys[3]<<3)|((J>>3)&7)])

def outlined(d, xy, text, font, fill, outline, ow=1):
    x, y = xy
    for dx in range(-ow, ow+1):
        for dy in range(-ow, ow+1):
            if dx or dy: d.text((x+dx, y+dy), text, font=font, fill=outline)
    d.text((x, y), text, font=font, fill=fill)

def draw_grad_glyph(canvas, x, top, baseline, ch, font):
    """1文字を 白青の縦グラデ＋暗青縁 で描く(ZERO NO HOUKOU の質感に合わせる)。"""
    asc, _ = font.getmetrics()
    ty = baseline - asc
    d = ImageDraw.Draw(canvas)
    # 暗青縁(8方向)
    for dx in (-1,0,1):
        for dy in (-1,0,1):
            if dx or dy: d.text((x+dx, ty+dy), ch, font=font, fill=OUTL)
    # 本体を白で一旦描き、その画素だけ縦グラデ(上=CORE/下=MIDC)に置換
    mask = Image.new('L', canvas.size, 0)
    ImageDraw.Draw(mask).text((x, ty), ch, font=font, fill=255)
    bb = mask.getbbox()
    if bb:
        y0, y1 = bb[1], bb[3]
        grad = Image.new('RGB', canvas.size)
        gp = grad.load()
        for yy in range(y0, y1):
            t = (yy - y0) / max(1, (y1 - y0 - 1))
            c = tuple(round(CORE[k]*(1-t) + MIDC[k]*t) for k in range(3))
            for xx in range(bb[0], bb[2]): gp[xx, yy] = c
        canvas.paste(grad, (0,0), mask)

PLATE = (8, 16, 48)   # HOUKOU が乗る暗紺の帯

def _kai_w(text, big_px, small_px):
    dd = ImageDraw.Draw(Image.new('RGB', (1, 1)))
    return sum(dd.textlength(c, font=ImageFont.truetype(KAI_FONT, big_px if i==0 else small_px)) + 1.0
               for i, c in enumerate(text))

def smallcaps(canvas, x, baseline, text, big_px, small_px):
    w = _kai_w(text, big_px, small_px)
    # 暗紺の帯(角丸)= HOUKOU と同じ「帯に乗った文字」に
    ImageDraw.Draw(canvas).rounded_rectangle(
        [x-3, baseline-big_px, x+w+1, baseline+2], radius=2, fill=PLATE)
    cx = float(x)
    for i, ch in enumerate(text):
        f = ImageFont.truetype(KAI_FONT, big_px if i == 0 else small_px)
        draw_grad_glyph(canvas, round(cx), baseline - (big_px if i==0 else small_px), baseline, ch, f)
        cx += ImageDraw.Draw(canvas).textlength(ch, font=f) + 1.0
    return cx

def main():
    encode = '--encode' in sys.argv
    d = bytearray(open(BASE, 'rb').read())
    orig = decode(d); new = orig.copy()
    # 「改」= 毛筆(魏碑)。零の咆哮 の隣、白＋暗赤縁(1943改風)。承認済=このまま。
    outlined(ImageDraw.Draw(new), (200, 34), '改', ImageFont.truetype(weibei(), 58),
             (255,255,255), (120,0,0), ow=3)
    # 「KAI」= ZERO NO HOUKOU に揃える(Arial Bold スモールキャップス/白青/同サイズ/同ベースライン)。
    smallcaps(new, 147, 88, 'KAI', big_px=6, small_px=6)
    new.resize((W*2, H*2), Image.NEAREST).save(PREVIEW)
    print('preview:', PREVIEW)
    if encode:
        op = orig.load(); npx = new.load(); changed = 0
        for y in range(H):
            for gx in range(64):
                x0 = gx*4
                if any(npx[x0+i, y] != op[x0+i, y] for i in range(4)):
                    d[y*256+gx*4: y*256+gx*4+4] = encode_group([npx[x0+i, y] for i in range(4)])
                    changed += 1
        open(DST, 'wb').write(d)
        print(f'title.yjk updated: {changed} groups re-encoded')

if __name__ == '__main__':
    main()
