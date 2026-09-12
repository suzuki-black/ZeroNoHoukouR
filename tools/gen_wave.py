#!/usr/bin/env python3
"""gen_wave.py — メガクラッシュの津波スプライトを生成し、焼く前に見るためのプレビュー器。

  python3 tools/gen_wave.py prev.png   … VRAM と同じ規則で 256x128 を合成して PNG に出す
  python3 tools/gen_wave.py c          … src/banked/ovl_crush.c に貼る C の配列を吐く

★なぜこれが要るか: 色表・塗り率・パターンの組合せは試行回数が要る。1回3分の openMSX 実行
  では回らないので、同じ規則でここで合成して目で見る(苦労と教訓 §11-13)。

★スプライト mode2 は **行ごとに1色**。だから
   ・色は縦にしか変えられない(1行おきに替えると拡大で2px の横縞=鎧戸になる) → 色は塊で置く
   ・質感は「穴(透明)」が作る。下地はノイズ入りの海なので穴から粒が透ける
   ・波の色を海の地色と同じにした領域は穴が見えない → そこは低い塗り率でよい
  波の解剖(peak/lip/trough/foam)に沿わせ、lip の真下に濃い影を一本入れるのが効く。
"""
import sys
from PIL import Image

PAL = {0:(0,0,0),1:(1,4,5),2:(2,5,6),3:(1,3,1),4:(3,3,3),5:(2,2,2),6:(6,5,3),7:(0,1,3),
       8:(2,5,2),9:(3,3,1),10:(4,6,4),11:(7,1,1),12:(7,4,0),13:(1,1,1),14:(4,4,5),15:(7,7,7)}
def rgb(i):
    r,g,b = PAL[i]; return (r*36, g*36, b*36)

class R:
    def __init__(s,seed): s.v=seed
    def n(s):
        s.v=(s.v*25173+13849)&0xFFFF; return (s.v>>8)&0xFF

def scatter(rows_fill, seed, base=None):
    """rows_fill: 16要素の 0..100(その行の塗り率)。base があれば AND を取る(波頭の上の透明部)。"""
    rng=R(seed); rows=[0]*16
    for r in range(16):
        f=rows_fill[r]
        for c in range(16):
            if rng.n()*100 < f*256: rows[r]|=(0x8000>>c)
        if base is not None: rows[r]&=base[r]
    return rows

# ---- 波頭(crest): 上端は透明、そこから下は白い塊。輪郭の高さは左右端を row4 に揃える ----
# ★斜めの壁にするので、**コマの中の波頭も同じ傾きで斜めに切る**。
#   コマは列ごとに SHEAR(=16px 画面) ずつ下がる。拡大で 1 パターン行 = 画面2px なので、
#   16px = パターン 8 行。パターン列 px の波頭行は px/2 が「直線」。
#   継ぎ目: コマ c の px=15 は行7(画面+14px) / コマ c+1 の px=0 は行0 だが 16px 下 → 連続。
#   その直線に、両端(px=0,15)では 0 になるうねりを足す。
def slope(px): return px // 8   # SHEAR=4 画面px/列 = 2パターン行/コマ
wob  = [0,1,2,3,3,3,2,1,1,2,3,3,2,1,1,0]   # 山(両端0)
wob2 = [0,1,2,2,3,4,4,3,3,4,4,3,2,2,1,0]   # 谷(両端0)
hA=[max(0, slope(c) + 4 - wob[c])  for c in range(16)]
hB=[max(0, slope(c) + 4 + wob2[c]) for c in range(16)]
def crest_mask(h):
    m=[0]*16
    for c in range(16):
        for r in range(h[c],16): m[r]|=(0x8000>>c)
    return m
def mist(h, seed, amt=22):
    rng=R(seed); m=[0]*16
    for c in range(16):
        for d in (1,2,3):
            rr=h[c]-d
            if rr>=0 and rng.n()*100 < amt*256*(4-d)//4: m[rr]|=(0x8000>>c)
    return m

# 塗り率プロファイル(行 0..15)
# ★塗り率は「大きな塊(88〜96%)＋アクセント行(低率で粒を散らす)」。全部を中間の率にすると
#   行ごとの破線が揃って砂嵐に見える(一度そうなった)。穴は下地=ノイズ入りの海が透ける＝それ自体が粒。
F_CREST = [94,92,95,96, 95,93,92,90, 92,70,90,60, 96,96,95,85]
F_FACE  = [95,100,94,92, 90,92,25,90, 88,90,86,30, 88,86,84,82]
F_BODY  = [90,88,86,88, 30,86,84,86, 82,40,80,78, 74,68,25,60]
F_FOOT  = [55,48,40,44, 30,36,30,26, 22,18,14,16, 12, 9, 6, 4]

def build():
    ca=crest_mask(hA); cb=crest_mask(hB)
    A=scatter(F_CREST, 0x1234, ca); ma=mist(hA,0x77)
    B=scatter(F_CREST, 0x9ABC, cb); mb=mist(hB,0x55)
    for r in range(16): A[r]|=ma[r]; B[r]|=mb[r]
    FACE=scatter(F_FACE, 0x4242)
    BODY=scatter(F_BODY, 0xBEEF)
    FOOT=scatter(F_FOOT, 0x0DED)
    return A,B,FACE,BODY,FOOT

# 色表: 高密度の行=地の色 / 低密度の行=散らす別色
# ★色は**塊で**置く。1行おきに替えると拡大で2px の横縞になり、水でなく鎧戸に見える(一度そうなった)。
#   替えるのは波の解剖上の意味があるところだけ: lip の下の濃い影 / 面の泡の線 / 粒のアクセント。
C0=[15,15,15,15, 15,15,15,15, 15,14,14, 7,  7, 7, 7,14]   # 白い泡 →淡→ **lipの下の濃い影** →下面の淡
C1=[ 2,15, 2, 2,  2, 2,15, 2,  2, 2, 2,14,  2, 2, 2, 2]   # 面: 明るい水＋泡の線＋白/淡の粒
# ★胴は「面の続き(明るい水)→海の地色」。ここを濃紺で厚く塗ると画面下半分が黒い塊になり、
#   艦も海も飲み込んで汚い(一度そうなった)。**地の色と同じ色**にすれば穴が見えないので、
#   低い塗り率でも破綻せず、粒だけが効く。
C2=[ 2, 2, 2, 2, 14, 2, 2, 1,  1, 7, 1, 1,  1, 1, 7, 1]   # 胴: 明るい水→海の地色、濃紺の粒
C3=[ 1, 1, 1, 7,  1, 1, 7, 1,  7, 1, 7, 1,  7, 1, 7, 7]   # 裾: 海の地色と濃紺の粒＝引き波の乱れ

def preview(path):
    A,B,FACE,BODY,FOOT=build()
    W,H=256,200
    im=Image.new('RGB',(W,H))
    px=im.load()
    rng=R(0xC0DE)
    # 下地=海(色1にノイズ 2/7)
    for y in range(H):
        for x in range(W): px[x,y]=rgb(1)
    for _ in range(2600):
        px[rng.n()|(rng.n()&1)*0, rng.n()*H//256]=rgb(2)
    for _ in range(2000):
        px[rng.n(), rng.n()*H//256]=rgb(7)
    rowpat=[(A,B),(FACE,BODY),(BODY,FACE),(FOOT,FOOT)]
    rowcol=[C0,C1,C2,C3]
    top=16
    SHEAR=4    # 列ごとに下げる画面px(斜めの角度)
    # ★★傾きには上限がある: 段の間隔(32px)= SHEAR × 列数 のときだけ、32枚の y が
    #   SHEAR px 間隔で**均等**に並び、どの32px窓にもちょうど8枚=制限ぴったりになる。
    #   8列なら SHEAR=4。これより急にすると y が重複/偏って 1走査線8枚を超える。
    #   コマ内の波頭も同じ傾き(2パターン行/コマ)で切らないと継ぎ目に段が出る。
    for rw in range(4):
        for col in range(8):
            pat = rowpat[rw][(col+rw)&1]
            colt= rowcol[rw]
            for pr in range(16):
                for pc in range(16):
                    if pat[pr]&(0x8000>>pc):
                        c=rgb(colt[pr])
                        X=col*32+pc*2; Y=top+rw*32+col*SHEAR+pr*2
                        for dy in range(2):
                            for dx in range(2):
                                if 0<=X+dx<W and 0<=Y+dy<H: px[X+dx,Y+dy]=c
    im.resize((W*2,H*2),Image.NEAREST).save(path)

def cdata():
    A,B,FACE,BODY,FOOT=build()
    names=("CRESTA","CRESTB","FACE","BODY","FOOT")
    out=[]
    for n,p in zip(names,(A,B,FACE,BODY,FOOT)):
        L=[(r>>8)&0xFF for r in p]; R_=[r&0xFF for r in p]; b=L+R_
        out.append("    /* %s */\n    { %s,\n      %s },"%(n,
            ", ".join("0x%02X"%x for x in b[:16]), ", ".join("0x%02X"%x for x in b[16:])))
    print("\n".join(out))
    print()
    for n,t in (("wcol0",C0),("wcol1",C1),("wcol2",C2),("wcol3",C3)):
        print("static const u8 %s[16] = { %s };"%(n, ", ".join("%2d"%v for v in t)))

if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='c': cdata()
    else: preview(sys.argv[1] if len(sys.argv)>1 else 'prev.png')
