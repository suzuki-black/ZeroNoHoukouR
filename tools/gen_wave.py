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

# ---- 波頭(crest): 上端は透明、そこから下は白い塊 ----
# ★コマの中の波頭にも**その列の傾き**を持たせる。そうしないと 32px ごとに水平な段が並び、
#   「区分的一次関数の傾き0」= 階段に見える(実機の指摘)。
#   列 c と c+1 の画面Y の差 d は、斜め(SHEAR=4px)から うねりの増分 を引いたもの:
#       d(i) = SHEAR - (lift[i+1] - lift[i])
#   拡大で 1 パターン行 = 画面2px なので、コマの中では d/2 行ぶん下げながら切る。
#   うねりを**三角形**にすると d の取る値が 3 種類に収まり、波頭のコマも 3 枚で済む。
LIFT   = [0, 8, 16, 24, 24, 24, 16, 8]    # 列ごとの持ち上げ量(画面px)。増分は +8,+8,+8,0,0,-8,-8,-8
CRESTK = [0, 0, 0,  1,  1,  2,  2, 2]     # その列で使う波頭のコマ(下の SLOPE の番号)
SLOPE  = [-2, +2, +6]                     # コマの中で下げるパターン行数(= d/2。d = -4, +4, +12)
wob  = [0,1,2,3,3,3,2,1,1,2,3,3,2,1,1,0]   # 山(両端0)
def crest_h(S):
    """S = コマの左端から右端までに下げるパターン行数。両端は うねり 0 で継ぎ目を合わせる。"""
    return [max(0, min(15, 4 + (c * S) // 16 - wob[c])) for c in range(16)]
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
F_FACE  = [95, 78,94,92, 90,92,25,90, 88,90,86,30, 88,86,84,82]   # ★行1は 100%=真横の白線に見えたので落とす 
F_BODY  = [90,88,86,88, 30,86,84,86, 82,40,80,78, 74,68,25,60]
F_FOOT  = [55,48,40,44, 30,36,30,26, 22,18,14,16, 12, 9, 6, 4]

def groove(rows, r0, r1, x0, adv, ln):
    """r0..r1 の各行に、x が adv ずつ進む位置で長さ ln の穴を開ける＝**斜めの溝**。

    ★mode2 のスプライトは**色が行単位**なので、コマの中に斜めの「色の境界」は作れない
      (二段目以降の帯が真横に見えるのはこれが理由。ハードの制約)。形を斜めにできるのは
      **穴(透明)だけ**。下地の海は帯より暗いので、穴を斜めに並べると波面を流れる溝に見える。
    ★逆に「斜めの明るい筋」を作ろうとすると、その行の残り全部を穴にする必要があって
      帯が透けてしまう(一番やってはいけない失敗)。だから筋は**暗い側=穴**で描く。"""
    for r in range(r0, r1 + 1):
        x = int(x0 + adv * (r - r0))
        for k in range(ln):
            rows[r] &= (~(0x8000 >> ((x + k) % 16))) & 0xFFFF

def dash(rows, r, x, ln):
    """行 r を「x から ln 列だけ」にする＝短い横棒。**色が違う行(アクセント行)専用**。

    ★色は行単位なので、アクセント色の行を塗り潰すと**画面を横切る一本線**になる(実際そう見えた)。
      その行を短い棒にし、行が下がるごとに棒を右へずらすと、棒が斜めに並んで筋に見える。
      棒以外は穴になるが、アクセント行は16行中3行なので帯は破綻しない。"""
    v = 0
    for k in range(ln): v |= (0x8000 >> ((x + k) % 16))
    rows[r] = v

def build():
    """戻り: 波頭3種(傾き別) + 胴 + 面 + 裾。**この順で VRAM に焼き、この順で枠へ入れる**。"""
    cr=[]
    for k,(S,seed) in enumerate(zip(SLOPE,(0x1234,0x9ABC,0x5A5A))):
        h=crest_h(S)
        P=scatter(F_CREST, seed, crest_mask(h)); m=mist(h, seed^0x77)
        for r in range(16): P[r]|=m[r]
        # ★波頭の下(lip の影〜下面)は塗り率が高く、色も行単位なので**真横の帯**に見える。
        #   その列の傾きに沿って穴の筋を入れ、帯の中に流れを作る(adv = 16/S 列/行)。
        adv = 16.0 / S
        groove(P, 9, 15, 2, adv, 3)
        groove(P, 11, 15, 10, adv, 2)
        cr.append(P)
    BODY=scatter(F_BODY, 0xBEEF)
    FACE=scatter(F_FACE, 0x4242)
    FOOT=scatter(F_FOOT, 0x0DED)
    # ★面と胴は8列で共用する(枠が無い)ので、傾きは主だった列に合わせた中庸(3列/行≒+10px/列)。
    for P in (FACE, BODY):
        groove(P, 0, 15, 1, 3.0, 4)
        groove(P, 0, 15, 9, 3.0, 3)
    # ★アクセント色の行(色表で色が変わる行)は塗り潰すと横一直線になる。短い棒にして右下がりに並べる。
    for r,x,ln in ((1,1,6), (6,7,5), (11,13,5)):  dash(FACE, r, x, ln)   # 面: 泡の筋
    for r,x,ln in ((4,2,5), (9,8,5), (14,14,4)):  dash(BODY, r, x, ln)   # 胴: 濃紺の粒の筋
    return cr[0],cr[1],cr[2],BODY,FACE,FOOT

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
    C0p,C1p,C2p,BODY,FACE,FOOT=build()
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
    crest=[C0p,C1p,C2p]
    rowcol=[C0,C1,C2,C3]
    top=40
    SHEAR=4    # 列ごとに下げる画面px(斜めの角度)
    # ★★傾きには上限がある: 段の間隔(32px)= SHEAR × 列数 のときだけ、32枚の y が
    #   SHEAR px 間隔で**均等**に並び、どの32px窓にもちょうど8枚=制限ぴったりになる。
    #   8列なら SHEAR=4。これより急にすると y が重複/偏って 1走査線8枚を超える。
    #   コマ内の波頭も同じ傾き(2パターン行/コマ)で切らないと継ぎ目に段が出る。
    ph=0
    for rw in range(4):
        for col in range(8):
            i=(col+ph)&7
            if   rw==0: pat=crest[CRESTK[i]]
            elif rw==3: pat=FOOT
            else:       pat=(BODY,FACE)[(col+rw+ph)&1]
            colt= rowcol[rw]
            lift=LIFT[i]
            for pr in range(16):
                for pc in range(16):
                    if pat[pr]&(0x8000>>pc):
                        c=rgb(colt[pr])
                        X=col*32+pc*2; Y=top+rw*32+col*SHEAR+15-lift+pr*2
                        for dy in range(2):
                            for dx in range(2):
                                if 0<=X+dx<W and 0<=Y+dy<H: px[X+dx,Y+dy]=c
    im.resize((W*2,H*2),Image.NEAREST).save(path)

def cdata():
    ps=build()
    names=("波頭 上り(-4px)","波頭 下り(+4px)","波頭 急な下り(+12px)","胴","面","裾")
    out=[]
    for n,p in zip(names,ps):
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
