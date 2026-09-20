#!/usr/bin/env python3
# gen_alert.py — 「敵艦発見」「敵大将発見」の警報パネルを毛筆(魏碑=Weibei SC Bold)で 1bpp にベイクし
#   src/include/panel_alert.h を再生成する。撃破!! パネル(gen_panel.py)と同じ書体・同じ作り方。
#   パネルは描く側(bank19 の gen_planes.c)に定数として置く＝窓の差し替え(data_read)も RAM の置き場も要らない。
# 使い方: python3 tools/gen_alert.py   (生成物はリポジトリに入れる。make では走らない)
import glob, sys, os
from PIL import Image, ImageFont, ImageDraw

H = 40            # 文字の高さ(画面 212 行の約1/5。5文字の「敵大将発見」が画面幅に収まる大きさ)
PANELS = [("alert_kan", "敵艦発見"), ("alert_tai", "敵大将発見")]
MSG_H = 18        # 電文の1文字(18x18。3バイト幅×18行=54B/字)
MSG = ["我、敵総大将ヲ発見セリ", "乾坤一擲ノ決戦ヲ敢行ス"]

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

def bake_line(font, line, box):
    """1行を box x box の升目に等幅で焼く(1文字ずつ拡大すると「、」が1文字分に膨らむ)。
       全角の送り幅で並べて描き、行全体の高さで一度に縮める=字ごとの大小と位置が保たれる。"""
    adv = max(font.getlength(c) for c in line)
    big = Image.new("L", (int(adv * len(line)) + 80, 500), 0)
    d = ImageDraw.Draw(big)
    for i, c in enumerate(line):
        d.text((40 + i * adv, 40), c, font=font, fill=255)
    bb = big.getbbox()
    out = []
    for i in range(len(line)):
        cell = big.crop((int(40 + i * adv), bb[1], int(40 + (i + 1) * adv), bb[3])).resize((box, box), Image.LANCZOS)
        px = cell.load()
        wb = (box + 7) // 8
        for y in range(box):
            for b in range(wb):
                byte = 0
                for bit in range(8):
                    x = b * 8 + bit
                    if x < box and px[x, y] >= 112:
                        byte |= 0x80 >> bit
                out.append(byte)
    return out

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
    # 電文(一文字ずつ出す)。1文字 = 3バイト幅 × MSG_H 行
    mfont = ImageFont.truetype(fpath, 160)
    wb = (MSG_H + 7) // 8
    out.append(f"#define MSG_H {MSG_H}")
    out.append(f"#define MSG_WB {wb}")
    out.append(f"#define MSG_N {len(MSG[0])}   /* 1行の文字数(2行とも同じ) */")
    for li, line in enumerate(MSG):
        if len(line) != len(MSG[0]):
            sys.exit("電文の2行は同じ文字数にしてください")
        data = bake_line(mfont, line, MSG_H)
        out.append(f"static const u8 msg{li}[MSG_N*MSG_WB*MSG_H] = {{   /* {line} */")
        for i in range(0, len(data), wb * MSG_H):
            out.append("    " + ",".join(f"0x{v:02x}" for v in data[i:i + wb * MSG_H]) + ",")
        out.append("};")
        print(f"電文{li}: {len(line)}字 {len(data)}B")
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
