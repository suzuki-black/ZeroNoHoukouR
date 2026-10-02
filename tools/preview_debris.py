#!/usr/bin/env python3
"""preview_debris.py — src/include/debart.h の破片を、実際の面パレットで絵にして確かめる。
   エミュレータを回さずに色と形を詰めるためのもの(出力はスクラッチへ)。
   使い方: python3 tools/preview_debris.py [出力PNG]
"""
import re, sys

# 1面(昼)の基準パレット。tools/gen_grade.py と vdp_palette_game() と同じ値(0-7 の RGB)
PAL = [(0,0,0),(1,4,5),(2,5,6),(1,3,1),(3,3,3),(2,2,2),(6,5,3),(0,1,3),
       (2,5,2),(3,3,1),(4,6,4),(7,1,1),(7,4,0),(1,1,1),(4,4,5),(7,7,7)]
SEA = 1


def parse(path="src/include/debart.h"):
    s = open(path, encoding="utf-8").read()
    def arr2(name):
        m = re.search(name + r"\[DEB_IMG\]\[32\] = \{(.*?)\n\};", s, re.S)
        out = []
        for note, hexs in re.findall(r"\{\s*/\*(.*?)\*/\s*((?:0x[0-9A-F]{2},?\s*)+)\}", m.group(1)):
            out.append((note.strip(), [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", hexs)]))
        return out
    def col(name):
        m = re.search(name + r"\[DEB_KIND\]\[16\] = \{(.*?)\n\};", s, re.S)
        return [[int(v) for v in re.findall(r"\d+", row)]
                for row in re.findall(r"\{([^}]*)\}", m.group(1))]
    return arr2("deb_rim"), arr2("deb_body"), col("col_rim"), col("col_body")


def render(img, rim_b, body_b, crim, cbody, scale=8):
    """1 枚を 16x16 の色番号へ。前(縁)が手前、後ろ(本体)がその下、残りは海。"""
    px = [[SEA] * 16 for _ in range(16)]
    for y in range(16):
        for half in (0, 8):
            bb = body_b[(0 if half == 0 else 16) + y]
            rr = rim_b[(0 if half == 0 else 16) + y]
            for x in range(8):
                m = 0x80 >> x
                if bb & m:  px[y][half + x] = cbody[y]
                if rr & m:  px[y][half + x] = crim[y]
    return px


def main():
    from PIL import Image
    rim, body, crim, cbody = parse()
    n = len(rim)
    kinds = len(crim)
    poses = n // kinds
    S = 10
    pad = 6
    W = poses * (16 * S + pad) + pad
    H = kinds * (16 * S + pad) + pad
    im = Image.new("RGB", (W, H), (20, 20, 20))
    for i in range(n):
        k, p = i // poses, i % poses
        px = render(i, rim[i][1], body[i][1], crim[k], cbody[k])
        ox = pad + p * (16 * S + pad)
        oy = pad + k * (16 * S + pad)
        for y in range(16):
            for x in range(16):
                c = PAL[px[y][x]]
                col = (c[0] * 36, c[1] * 36, c[2] * 36)
                for dy in range(S):
                    for dx in range(S):
                        im.putpixel((ox + x * S + dx, oy + y * S + dy), col)
    out = sys.argv[1] if len(sys.argv) > 1 else "debris_preview.png"
    im.save(out)
    print(out, f"({kinds} 種 × {poses} コマ)")


if __name__ == "__main__":
    main()
