#!/usr/bin/env python3
# gen_panel.py — 「撃破!!」結果パネルを毛筆(魏碑=Weibei SC Bold)で1bppビットマップにベイクし
#   src/include/panel_gekiha.h を再生成する。gen_assets.mjs が同ヘッダのバイト列を bank8 へ連結する。
# 旧版の筆文字パネルに寄せる意図。字形サイズは変えず PANEL_W=112 / PANEL_H=41 を維持。
import glob, sys, os
from PIL import Image, ImageFont, ImageDraw

PANEL_W, PANEL_H = 112, 41
PANEL_WB = PANEL_W // 8   # 14
TEXT = "撃破!!"

def find_font():
    pats = [
        "/System/Library/AssetsV2/**/WeibeiSC-Bold.otf",
        "/System/Library/Fonts/**/WeibeiSC-Bold.otf",
        "/Library/Fonts/**/WeibeiSC-Bold.otf",
    ]
    for p in pats:
        hits = glob.glob(p, recursive=True)
        if hits:
            return hits[0]
    return None

def main():
    fpath = find_font()
    if not fpath:
        sys.exit("WeibeiSC-Bold.otf が見つかりません(macOSの魏碑フォント)。")
    # 大きめに描いてから 112x41 へ収める(アンチエイリアス→50%閾値で1bpp化)
    big = Image.new("L", (600, 240), 0)
    d = ImageDraw.Draw(big)
    font = ImageFont.truetype(fpath, 180)
    d.text((10, 10), TEXT, font=font, fill=255)
    bbox = big.getbbox()
    glyph = big.crop(bbox)
    # 縦横比を保ったまま 112x41 の枠へ最大化(余白2px)
    tw, th = PANEL_W - 2, PANEL_H - 2
    gw, gh = glyph.size
    scale = min(tw / gw, th / gh)
    nw, nh = max(1, int(gw * scale)), max(1, int(gh * scale))
    glyph = glyph.resize((nw, nh), Image.LANCZOS)
    panel = Image.new("L", (PANEL_W, PANEL_H), 0)
    panel.paste(glyph, ((PANEL_W - nw) // 2, (PANEL_H - nh) // 2))
    px = panel.load()
    rows = []
    for y in range(PANEL_H):
        row = []
        for b in range(PANEL_WB):
            byte = 0
            for bit in range(8):
                x = b * 8 + bit
                if px[x, y] >= 128:
                    byte |= (0x80 >> bit)
            row.append(byte)
        rows.append(row)
    out = [
        "/* 撃破!! 結果パネル(1bpp, 112x41)。tools/gen_panel.py が魏碑(Weibei SC Bold)で自動生成。手編集しない。",
        "   gen_assets.mjs がこのバイト列を読み bank8 へ連結、scene_stage が data_read+blit_panel で描画。 */",
        "#ifndef PANEL_GEKIHA_H",
        "#define PANEL_GEKIHA_H",
        "",
        f"#define PANEL_W {PANEL_W}",
        f"#define PANEL_WB {PANEL_WB}",
        f"#define PANEL_H {PANEL_H}",
        f"static const u8 panel_gekiha[PANEL_WB*PANEL_H] = {{",
    ]
    for r in rows:
        out.append("    " + ",".join(f"0x{v:02x}" for v in r) + ",")
    out += ["};", "", "#endif /* PANEL_GEKIHA_H */", ""]
    dst = os.path.join(os.path.dirname(__file__), "..", "src", "include", "panel_gekiha.h")
    with open(dst, "w") as fp:
        fp.write("\n".join(out))
    # 確認用プレビュー
    panel.point(lambda v: 255 if v >= 128 else 0).resize((PANEL_W * 4, PANEL_H * 4), Image.NEAREST).save(
        os.path.join(os.path.dirname(__file__), "..", "build", "panel_preview.png"))
    print(f"panel_gekiha.h 再生成: font={os.path.basename(fpath)} size={PANEL_W}x{PANEL_H}")

if __name__ == "__main__":
    main()
