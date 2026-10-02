#!/usr/bin/env python3
"""gen_debris.py — 撃沈演出の「破片」の絵(src/include/debart.h)を作る。

★作り方はアニメの爆発作画の定石に合わせた(2026-10-02 に調べた。苦労と教訓 §16-36):
  ・**破片はシルエットで散らす**。本体は暗い鉄の色、明るいのは縁だけ。
    (前の絵は本体が明るい橙の横棒ばかりで「ホットドッグ」に見えた＝ユーザーが却下)
  ・**厚みを持たせる**。板をひらひら回すと、途中で**真横を向いて細い棒になる**。
    この「面 → 斜め → 真横 → 斜め」の 4 コマが、回っていることを一番強く見せる。
  ・**側面にハイライトを入れてくるくる回す**。真横を向いたコマは板の側面そのものなので
    ほぼ全部が縁の色になる＝一瞬キラッと光る。
  ・**一定方向に回す**こと。往復(2コマの入れ替え)だと「震えている」ようにしか見えない。

出力: 1 種類につき 4 コマ。コマ k は
    横の縮み s[k] (1.0 → 0.55 → 0.14 → 0.55) ＋ 面内の回転 k*18°
で作る。s が 0.14 のコマが「真横(厚み)」。

絵は 16x16 の 1bpp を 2 枚重ね:
    後ろ = 本体(塗りつぶしたシルエット)
    前   = 縁(明るい側面) ＋ 質感のノイズ点
★中に穴を開けないこと(背景の海が透けて見える。ユーザー指摘)。
"""
import math

W = H = 16
KINDS = 3
POSES = 4
SQUASH = [1.00, 0.55, 0.14, -0.55]  # 横の縮み。0.14=真横(板の厚み) / 負=裏返り(1周する)
SPIN   = 45                          # コマごとに足す面内の回転(度)。一定方向に回す

# 種類ごとの「面」の多角形(16x16 の中心 (7.5,7.5) を原点にした座標)。
# ★左右非対称・輪郭を単純にしない(アニメの定石)。大きさも種類で変える。
SHAPES = {
    # 装甲板の裂片: 長い鋭角を持つ四角形(ちぎれた鉄板)
    "wedge": [(-7.0, -1.5), (-1.0, -5.0), (6.5, -2.0), (4.0, 2.5), (-2.0, 4.5), (-6.0, 2.0)],
    # L字アングル材: 太さのある L(構造材)。厚みがあるので回ると形が大きく変わる
    "angle": [(-5.5, -6.0), (-1.5, -6.0), (-1.5, 2.5), (5.5, 2.5), (5.5, 6.0), (-5.5, 6.0)],
    # 折れた配管/砲身: 太い棒の片端がちぎれている
    "pipe":  [(-7.0, -2.5), (3.0, -3.0), (6.5, -1.0), (6.0, 1.5), (2.0, 3.0), (-7.0, 2.5)],
}
ORDER = ["wedge", "angle", "pipe"]


def xform(pts, s, deg):
    """横に s 倍つぶしてから deg 度回す。"""
    r = math.radians(deg)
    c, sn = math.cos(r), math.sin(r)
    out = []
    for x, y in pts:
        x *= s
        out.append((x * c - y * sn, x * sn + y * c))
    return out


def fill(pts):
    """多角形を 16x16 の集合へ(走査線の偶奇)。中心 (7.5,7.5)。"""
    m = set()
    n = len(pts)
    for py in range(H):
        yy = py - 7.5
        xs = []
        for i in range(n):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % n]
            if (y0 <= yy < y1) or (y1 <= yy < y0):
                xs.append(x0 + (yy - y0) * (x1 - x0) / (y1 - y0))
        xs.sort()
        for i in range(0, len(xs) - 1, 2):
            a, b = xs[i], xs[i + 1]
            for px in range(W):
                if a - 0.5 <= px - 7.5 <= b + 0.5:
                    m.add((px, py))
    return m


def rim_of(body):
    """明るい縁 ＝ シルエットの**左上側**の輪郭だけ。光は左上から来る、で統一する。
    反対側(右下)を暗いまま残すと塊に見える＝シルエットが立つ。
    真横のコマは幅が 1〜2 画素なので、結果としてほぼ全部が縁になる(キラッと光る)。"""
    r = set()
    for (x, y) in body:
        if (x - 1, y) not in body or (x, y - 1) not in body:
            r.add((x, y))
    return r


def noise(body, rim, seed):
    """質感のノイズ点(穴は開けない。縁の層へ混ぜる)。決め打ちのハッシュで散らす。"""
    out = set()
    for (x, y) in body:
        if (x, y) in rim:
            continue
        if ((x * 37 + y * 61 + seed * 17) & 7) < 1:
            out.add((x, y))
    return out


def to_sprite(mask):
    """16x16 の集合 → MSX スプライト 32B(左半分16行 → 右半分16行)。"""
    b = []
    for half in (0, 8):
        for y in range(H):
            v = 0
            for x in range(8):
                if (half + x, y) in mask:
                    v |= 0x80 >> x
            b.append(v)
    return b


def hexrow(bs):
    return ",".join("0x%02X" % v for v in bs)


def emit():
    names = {"wedge": "装甲板の裂片", "angle": "L字アングル材", "pipe": "折れた配管"}
    pose_name = ["面", "斜め", "真横(厚み)", "斜め(裏)"]
    rims, bodies, notes = [], [], []
    for ki, key in enumerate(ORDER):
        for p in range(POSES):
            pts = xform(SHAPES[key], SQUASH[p], p * SPIN)
            body = fill(pts)
            if not body:                      # つぶれ過ぎたら中央に 1 列だけ残す
                body = {(7, y) for y in range(5, 11)}
            rim = rim_of(body)
            # ★質感のノイズ点は**入れない**。決め打ちのハッシュだと斜めの点線に見え、
            #   破片に「ジッパー」が付いたようになった(プレビューで確認)。シルエットと縁だけで出す。
            front = rim
            bodies.append(to_sprite(body))
            rims.append(to_sprite(front))
            notes.append("%s %s" % (names[key], pose_name[p]))
    return rims, bodies, notes


HEAD = '''/* debart.h — 撃沈演出の「破片」の絵と色。4面(ovl_part.c)と2面(ovl_crack.c)で共有する。
   ★このファイルは tools/gen_debris.py が作る。手で直さないこと。
   ★バンクをまたいで共有はできないので、include した側それぞれに実体が入る。

   ★アニメの爆発作画の定石に合わせてある(2026-10-02。苦労と教訓 §16-36):
     ・破片は**シルエット**で散らす。本体は暗い鉄、明るいのは縁だけ。
     ・板には**厚み**がある。回ると「面 → 斜め → **真横** → 斜め」と形が変わる。
       真横のコマは板の側面そのものなので、ほぼ全部が縁の色＝一瞬キラッと光る。
     ・**一定方向に回す**こと。2コマの往復では「震えている」ようにしか見えない。
   ★スプライト 2 枚重ね: 後ろ = 塗りつぶした本体 / 前 = 明るい縁 ＋ 質感のノイズ点。
   ★**中に穴を開けない**こと。抜きを入れると背景(海)が透けて「一番だめ」になる(ユーザー指摘)。
   ★パターン番号は 16x16 なので 4 の倍数。絵 k(=種類*DEB_POSE+コマ) → 縁 k*4 / 本体 DEB_PATB+k*4。 */
#ifndef DEBART_H
#define DEBART_H

#include "types.h"

#define DEB_KIND 3                       /* 種類 */
#define DEB_POSE 4                       /* 1 種類あたりのコマ(面→斜め→真横→斜め) */
#define DEB_IMG  (DEB_KIND * DEB_POSE)
#define DEB_PATB 48                      /* 本体の層のパターン番号の起点(縁は 0..47) */
'''

TAIL = '''
/* 行ごとの色。前(縁)＝焼けた金属の光、後(本体)＝鉄の陰影。透明色(0)は使わない。
   ★上から 白 → 橙 → 赤 → 黒。破片の上側が光り、下へいくほど冷えて影になる。
     絵は 16 行の真ん中あたり(3〜12 行)に入るので、そこが橙〜赤の帯になるように置いている。 */
static const u8 col_rim[DEB_KIND][16] = {
    { 15,15,15,15,15,12,12,12, 12,12,11,11,11,13,13,13 },   /* 裂片: 白熱した縁 */
    { 15,15,15,14,14,12,12,12, 12,11,11,11,13,13,13,13 },   /* L字: やや鈍い金属の照り */
    { 15,15,15,15,12,12,12,12, 12,11,11,11,13,13,13,13 },   /* 配管: 熱を持った管 */
};
static const u8 col_body[DEB_KIND][16] = {
    {  4, 4, 5, 5, 5, 5, 5, 5, 13,13,13,13,13,13,13,13 },   /* 裂片: 鋼の面(上が少し明るい) */
    {  4, 5, 5, 5, 5, 5,13,13, 13,13,13,13,13,13,13,13 },   /* L字: 影に落ちた構造材 */
    {  4, 4, 5, 5, 5,13,13,13, 13,13,13,13,13,13,13,13 },   /* 配管: 丸みを上の明るさで */
};

#endif /* DEBART_H */
'''


def main():
    rims, bodies, notes = emit()
    out = [HEAD, "\nstatic const u8 deb_rim[DEB_IMG][32] = {"]
    for i, b in enumerate(rims):
        out.append("    {   /* %s: 縁とノイズ */" % notes[i])
        out.append("        " + hexrow(b[:16]) + ",")
        out.append("        " + hexrow(b[16:]) + " },")
    out.append("};")
    out.append("static const u8 deb_body[DEB_IMG][32] = {")
    for i, b in enumerate(bodies):
        out.append("    {   /* %s: 本体 */" % notes[i])
        out.append("        " + hexrow(b[:16]) + ",")
        out.append("        " + hexrow(b[16:]) + " },")
    out.append("};")
    out.append(TAIL)
    open("src/include/debart.h", "w", encoding="utf-8").write("\n".join(out))
    print("src/include/debart.h: %d 枚 (%d 種 × %d コマ)" % (len(rims), KINDS, POSES))
    for i, n in enumerate(notes):
        px = sum(bin(v).count("1") for v in bodies[i])
        print("   %2d %-20s 本体 %3d 画素" % (i, n, px))


if __name__ == "__main__":
    main()
