#!/usr/bin/env python3
# gen_alert.py — 「敵艦発見」「敵大将発見」の警報パネルを毛筆(魏碑=Weibei SC Bold)で 1bpp にベイクし
#   src/include/panel_alert.h を再生成する。撃破!! パネル(gen_panel.py)と同じ書体・同じ作り方。
#   パネルは描く側(bank19 の gen_planes.c)に定数として置く＝窓の差し替え(data_read)も RAM の置き場も要らない。
# 使い方: python3 tools/gen_alert.py   (生成物はリポジトリに入れる。make では走らない)
import glob, sys, os
from PIL import Image, ImageFont, ImageDraw

H = 40            # 文字の高さ(画面 212 行の約1/5。5文字の「敵大将発見」が画面幅に収まる大きさ)
PANELS = [("alert_kan", "敵艦発見"), ("alert_tai", "敵大将発見")]

def find_font():
    for p in ["/System/Library/AssetsV2/**/WeibeiSC-Bold.otf",
              "/System/Library/Fonts/**/WeibeiSC-Bold.otf",
              "/Library/Fonts/**/WeibeiSC-Bold.otf"]:
        hits = glob.glob(p, recursive=True)
        if hits:
            return hits[0]
    return None

def bake(font, text):
    big = Image.new("L", (1400, 260), 0)
    ImageDraw.Draw(big).text((10, 10), text, font=font, fill=255)
    glyph = big.crop(big.getbbox())
    gw, gh = glyph.size
    nw = max(1, round(gw * H / gh))
    glyph = glyph.resize((nw, H), Image.LANCZOS)
    w = (nw + 7) // 8 * 8
    panel = Image.new("L", (w, H), 0)
    panel.paste(glyph, ((w - nw) // 2, 0))
    px = panel.load()
    data = []
    for y in range(H):
        for b in range(w // 8):
            byte = 0
            for bit in range(8):
                if px[b * 8 + bit, y] >= 128:
                    byte |= 0x80 >> bit
            data.append(byte)
    return w, panel, data

def main():
    fpath = find_font()
    if not fpath:
        sys.exit("WeibeiSC-Bold.otf が見つかりません(macOSの魏碑フォント)。")
    font = ImageFont.truetype(fpath, 200)
    root = os.path.join(os.path.dirname(__file__), "..")
    out = [
        "/* 警報パネル(1bpp)。tools/gen_alert.py が魏碑(Weibei SC Bold)で自動生成。手編集しない。",
        "   bank19(gen_planes.c)だけが取り込む。先頭2バイト=[幅(バイト), 高さ]、続けて1行ずつ左から。 */",
        "#ifndef PANEL_ALERT_H",
        "#define PANEL_ALERT_H",
        "",
    ]
    previews = []
    for name, text in PANELS:
        w, panel, data = bake(font, text)
        if w > 240:
            sys.exit(f"{text}: 幅 {w}px が画面に収まらない")
        out.append(f"static const u8 {name}[{2 + len(data)}] = {{   /* {text} {w}x{H} */")
        out.append(f"    {w // 8},{H},")
        wb = w // 8
        for y in range(H):
            out.append("    " + ",".join(f"0x{v:02x}" for v in data[y * wb:(y + 1) * wb]) + ",")
        out.append("};")
        previews.append(panel.point(lambda v: 255 if v >= 128 else 0))
        print(f"{text}: {w}x{H} = {len(data)}B")
    out += ["", "#endif /* PANEL_ALERT_H */", ""]
    with open(os.path.join(root, "src", "include", "panel_alert.h"), "w") as fp:
        fp.write("\n".join(out))
    # 確認用プレビュー(build/)
    pw = max(p.size[0] for p in previews)
    sheet = Image.new("L", (pw, H * len(previews) + 8), 0)
    for i, p in enumerate(previews):
        sheet.paste(p, (0, i * (H + 8)))
    os.makedirs(os.path.join(root, "build"), exist_ok=True)
    sheet.resize((pw * 3, sheet.size[1] * 3), Image.NEAREST).save(os.path.join(root, "build", "alert_preview.png"))

if __name__ == "__main__":
    main()
