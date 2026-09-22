#!/usr/bin/env python3
"""gen_title_text.py — 文字の無いタイトル画(assets/title_base.png)に、タイトル文字を描き足す。

  漢字「眞 零の咆哮」 … Zen Antique(明治〜昭和初期の金属活字が元の明朝)を白一色で。「真」は戦中の活字らしく旧字体「眞」
  ローマ字「SHIN ZERO NO HOUKOU」(銀と青) / 「PRESS SPACE KEY」(金) / 「© 2026 suzuki-black」(白) … Exo 2

★フォントは SIL Open Font License 1.1 のものだけを使う(Mac 内蔵のフォントは商用・配布の権利が無いので使わない)。
  フォントのファイルはリポジトリに入れない。Google Fonts の公式リポジトリ(github.com/google/fonts)の
  ofl/zenantique/ZenAntique-Regular.ttf と ofl/exo2/Exo2-Italic[wght].ttf, Exo2[wght].ttf を <fontdir> に
  ZenAntique-Regular.ttf / Exo2-Italic.ttf / Exo2.ttf の名前で置いて実行する。
★元の Copilot の絵は漢字が正しくなかった(「咆哮」が書けていない)ので、Copilot に文字の無い絵を作り直してもらい、
  文字はここで描く。文字の位置は MSX で見える範囲(gen_title.py の CROP)の中央に合わせる
  (元の絵は右上の「Made with AI」を切り落とす切り抜きのせいで、文字が画面の右に寄っていた)。
★経緯: 筆文字(Yuji Boku)＋炎のグラデーションも試したが「気に入らない」とユーザー判断で、活字体の白に決まった。

使い方: python3 tools/gen_title_text.py <fontdir> <出力.png>
  → 出力を tools/gen_title.py src <出力.png> で切り抜き・縮小して assets/title_src.png にし、encode する。
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, '..', 'assets', 'title_base.png')
CX = 735          # 文字の中心 x(gen_title.py の CROP の中央に合わせる)
KANJI_FONT = 'ZenAntique-Regular.ttf'
KANJI_TEXT = '眞 零の咆哮'   # 「真」は旧字体(戦中の活字の雰囲気。ユーザーが4案から選んだ)
KANJI_SIZE = 190

def vgrad(size, stops):
    """縦のグラデーション。stops=[(位置0..1, (r,g,b)), ...]"""
    w, h = size
    col = Image.new('RGB', (1, h))
    for y in range(h):
        t = y / max(1, h - 1)
        for i in range(len(stops) - 1):
            (t0, c0), (t1, c1) = stops[i], stops[i + 1]
            if t0 <= t <= t1:
                u = (t - t0) / max(1e-6, t1 - t0)
                col.putpixel((0, y), tuple(int(c0[k] + (c1[k] - c0[k]) * u) for k in range(3)))
                break
    return col.resize((w, h))

def text_mask(txt, font, size, pos, shear=0.0, grow=0):
    m = Image.new('L', size, 0)
    ImageDraw.Draw(m).text(pos, txt, font=font, fill=255)
    if shear:
        m = m.transform(size, Image.AFFINE, (1, shear, -shear * pos[1], 0, 1, 0), Image.BICUBIC)
    if grow:
        m = m.filter(ImageFilter.MaxFilter(grow))
    return m

def stamp(canvas, mask, fill, outline=(20, 6, 2), ow=11, glow=None, glow_r=18, shadow=(6, 8)):
    """文字を1つ押す: 影 → 光彩 → 黒い縁 → 塗り"""
    edge = mask.filter(ImageFilter.MaxFilter(ow))
    if shadow:
        sh = Image.new('L', mask.size, 0); sh.paste(edge, shadow)
        canvas.paste((0, 0, 0), mask=sh.filter(ImageFilter.GaussianBlur(5)).point(lambda v: int(v * 0.7)))
    if glow:
        g = edge.filter(ImageFilter.GaussianBlur(glow_r)).point(lambda v: min(255, int(v * 1.3)))
        canvas.paste(glow, mask=g)
    canvas.paste(outline, mask=edge)
    canvas.paste(fill, mask=mask)

def main():
    fontdir, out = sys.argv[1], sys.argv[2]
    im = Image.open(BASE).convert('RGB')
    W, H = im.size
    def exo(s, wght, italic=True):
        f = ImageFont.truetype(os.path.join(fontdir, 'Exo2-Italic.ttf' if italic else 'Exo2.ttf'), s)
        f.set_variation_by_axes([wght])
        return f

    # ---- 漢字: 戦中の活字の雰囲気(明朝の活字体)を白一色で。グラデーション・筆の払いは付けない ----
    kf = ImageFont.truetype(os.path.join(fontdir, KANJI_FONT), KANJI_SIZE)
    t = KANJI_TEXT
    tw = kf.getlength(t)
    m = text_mask(t, kf, (W, H), (CX - tw / 2, 70))
    stamp(im, m, (250, 250, 245), outline=(12, 8, 6), ow=11, glow=(0, 0, 0), glow_r=16, shadow=(6, 8))
    kbot = m.getbbox()[3]

    # ---- ローマ字 ----
    f = exo(64, 800)
    t = 'SHIN ZERO NO HOUKOU'
    tw = f.getlength(t)
    ry = kbot + 4 - f.getbbox(t)[1]                               # 漢字の下端のすぐ下(重ねると「真」の脚が隠れた)
    m = text_mask(t, f, (W, H), (CX - tw / 2, ry))
    rt, rb = (ry + f.getbbox(t)[1]) / H, (ry + f.getbbox(t)[3]) / H
    roma = vgrad((W, H), [(0, (255, 255, 255)), (rt, (240, 246, 255)), ((rt + rb) / 2, (150, 185, 235)), (rb, (70, 100, 190)), (1, (60, 80, 160))])
    stamp(im, m, roma, outline=(8, 12, 40), ow=9, glow=(60, 110, 255), glow_r=14, shadow=(4, 5))

    # ---- PRESS SPACE KEY ----
    f = exo(78, 850)
    t = 'PRESS SPACE KEY'
    tw = f.getlength(t)
    m = text_mask(t, f, (W, H), (CX - tw / 2, 780))
    gold = vgrad((W, H), [(0, (255, 255, 255)), (0.765, (255, 250, 200)), (0.79, (255, 215, 90)), (0.83, (240, 150, 20)), (1, (160, 70, 0))])
    stamp(im, m, gold, outline=(25, 12, 0), ow=9, glow=(255, 140, 0), glow_r=12, shadow=(4, 6))

    # ---- © 2026 suzuki-black ----
    f = exo(46, 600, italic=False)
    t = '© 2026 suzuki-black'
    tw = f.getlength(t)
    m = text_mask(t, f, (W, H), (CX - tw / 2, 895))
    stamp(im, m, (245, 245, 245), outline=(10, 10, 16), ow=7, glow=None, shadow=(3, 4))

    im.save(out)

if __name__ == '__main__':
    main()
