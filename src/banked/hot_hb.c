/* hot_hb.c — 中ボスの弾を背景に描く小型版(hot.bin に同居。RAM に常駐するので中ボスのオーバレイから呼べる)。
   ★中ボス戦はスプライトが HUD 9・中ボス 16〜18・アイコン 1 で埋まり、自機と弾に 4 枠しか残らず敵弾がちらついた
     (1面・4面、実機で指摘)。弾を表示リングへ 4x4 の色 12 の四角として描く。5面の P-61 の差分描き(ovl_bgbul.c)は
     1.4KB あって中ボスのオーバレイに入らないので、ここに小さい版を置き、オーバレイは入口(hotcode.h の hb_*)を呼ぶ。
     毎フレーム「前の四角を海へ戻す→新しい四角を描く」だけ(1発 4 行×2 バイト×2)。
   ★C で書くと sdcc が 1KB を超えた(HOT_CAP に入らない)ので、本体は asm。狙いの計算(aim_dir)だけ C。
   ★位置は画面座標(1/8 ドット)で持ち、描く行だけリング(y+cam)へ＝スクロールに引きずられない(敵弾の規約)。
   ★表と海のひな形は CPU 弾幕の固定帯(0xEC00〜)を借りる。中ボスの間は弾幕が出ない。ボム(curtain_reset)がこの帯へ 0 を
     書くので、ボムの後は読み直す(hb_update が残数の減りで気づく)。中ボスが去るときは hb_clear が帯も返す。
   弾の表: 0xEC00 から 9B×24 発 = { qx(2), qy(2), vx, vy(1/8 ドット/フレーム), on, sx(バイト列), ry(リング行) }
   海のひな形: 0xED00 から 16B×16 行(y=512..527 の左 32 ドット) */
#include "types.h"
#include "gamestate.h"  /* g_loop_t / g_crush */
#include "player.h"     /* g_player_x/y */
#include "aa_hot.h"     /* cam */
#include "fire.h"       /* aim_dir / dvx,dvy */
#include "entity.h"     /* ent_player_hit */
#include "curtain.h"    /* curtain_reset */

static u8 hb_x, hb_y;   /* 生む弾の四角の左上(画面) */
static u8 hb_lastc;     /* 前のフレームのボム残数 */

/* IX=弾、C=0xCC(描く)/0(海へ戻す)。4 行×2 バイト。B/DE/HL/A を壊す */
static void hb_put(void) __naked {
    __asm
        ld   e, 8 (ix)          ; リング行
        ld   d, 7 (ix)          ; バイト列
        ld   b, #4
    00001$:
        ld   a, e               ; 番地 0x8000 + r*128 + x : R#14 = 2 + r>>7
        rlca
        and  a, #1
        add  a, #2
        di
        out  (0x99), a
        ld   a, #0x8E
        out  (0x99), a
        ld   a, e
        rrca
        and  a, #0x80
        or   a, d
        out  (0x99), a
        ld   a, e
        srl  a
        and  a, #0x3F
        or   a, #0x40
        out  (0x99), a
        ei
        ld   a, c
        or   a, a
        jr   z, 00002$
        out  (0x98), a
        out  (0x98), a
        jr   00003$
    00002$:                     ; 海: ひな形 0xED00 + (r&15)*16 + (x&15), (x+1)&15
        ld   a, e
        and  a, #15
        rlca
        rlca
        rlca
        rlca
        ld   l, a
        ld   h, #0xED
        ld   a, d
        and  a, #15
        or   a, l
        ld   l, a
        ld   a, (hl)
        out  (0x98), a
        ld   a, l
        and  a, #0xF0
        ld   l, a
        ld   a, d
        inc  a
        and  a, #15
        or   a, l
        ld   l, a
        ld   a, (hl)
        out  (0x98), a
    00003$:
        inc  e
        djnz 00001$
        ret
    __endasm;
}

/* 海のひな形を読み、表を空にする */
void hot_hb_init(void) __naked {
    __asm
        ld   hl, #0xED00
        ld   c, #0
    00001$:
        di                      ; 読み出し番地 0x10000 + r*128 : R#14 = 4
        ld   a, #4
        out  (0x99), a
        ld   a, #0x8E
        out  (0x99), a
        ld   a, c
        rrca
        and  a, #0x80
        out  (0x99), a
        ld   a, c
        srl  a
        out  (0x99), a
        ei
        ld   b, #16
    00002$:
        in   a, (0x98)
        ld   (hl), a
        inc  hl
        djnz 00002$
        inc  c
        ld   a, c
        cp   a, #16
        jr   nz, 00001$
        ld   hl, #0xEC06        ; on
        ld   de, #9
        ld   b, #24
    00003$:
        ld   (hl), #0
        add  hl, de
        djnz 00003$
        ld   a, (_g_crush)
        ld   (_hb_lastc), a
        ret
    __endasm;
}

/* A=方向(0-31)。空きへ 1 発(速さ 3 ドット/フレーム=スプライトの弾と同じ)を hb_x/hb_y に生んで描く */
static void hb_spawn(u8 dir) __naked {
    (void)dir;
    __asm
        ld   c, a
        push ix
        ld   ix, #0xEC00
        ld   b, #24
        ld   de, #9
    00001$:
        ld   a, 6 (ix)
        or   a, a
        jr   z, 00002$
        add  ix, de
        djnz 00001$
        pop  ix
        ret
    00002$:
        ld   a, (_hb_x)
        ld   l, a
        ld   h, #0
        add  hl, hl
        add  hl, hl
        add  hl, hl
        ld   0 (ix), l
        ld   1 (ix), h
        ld   a, (_hb_y)
        ld   l, a
        ld   h, #0
        add  hl, hl
        add  hl, hl
        add  hl, hl
        ld   2 (ix), l
        ld   3 (ix), h
        ld   a, c
        and  a, #31
        ld   e, a
        ld   d, #0
        ld   hl, #_dvx
        add  hl, de
        ld   a, (hl)
        ld   b, a
        add  a, a
        add  a, b
        ld   4 (ix), a
        ld   hl, #_dvy
        add  hl, de
        ld   a, (hl)
        ld   b, a
        add  a, a
        add  a, b
        ld   5 (ix), a
        ld   6 (ix), #1
        ld   a, (_hb_x)
        srl  a
        ld   7 (ix), a
        ld   a, (_cam)
        ld   b, a
        ld   a, (_hb_y)
        add  a, b
        ld   8 (ix), a
        ld   c, #0xCC
        call _hb_put
        pop  ix
        ret
    __endasm;
}

/* 自機狙いの n-way(2 ステップ=22.5° 間隔)。(ox,oy) は元の弾のスプライトの左上(四角の左上はその +6) */
void hot_hb_fan(s16 ox, s16 oy, u8 n) {
    u8 a;
    if (ox < -2 || ox > 242 || oy < 2 || oy > 198) return;
    a = (u8)(aim_dir(ox, oy, g_player_x, g_player_y) - (n - 1));
    hb_x = (u8)(ox + 6); hb_y = (u8)(oy + 6);
    for (; n; n--, a += 2) hb_spawn(a);
}

/* 動かす・描き直す・画面外で消す・自機との当たり(宙返り中は当たらない)。ボムの後は読み直す */
void hot_hb_update(void) __naked {
    __asm
        ld   a, (_g_crush)
        ld   hl, #_hb_lastc
        cp   a, (hl)
        ld   (hl), a
        call c, _hot_hb_init    ; 残数が減った=ボム: 津波が弾を消しリングも描き直した。帯は curtain_reset が 0 にした
        push ix
        ld   ix, #0xEC00
        ld   b, #24
    00001$:
        push bc
        ld   a, 6 (ix)
        or   a, a
        jp   z, 00009$
        ld   c, #0
        call _hb_put            ; 前の四角を海へ
        ld   6 (ix), #0
        ld   a, 4 (ix)          ; qx += vx
        ld   e, a
        rla
        sbc  a, a
        ld   d, a
        ld   l, 0 (ix)
        ld   h, 1 (ix)
        add  hl, de
        ld   0 (ix), l
        ld   1 (ix), h
        sra  h
        rr   l
        sra  h
        rr   l
        sra  h
        rr   l
        ld   a, h               ; x は 4..248
        or   a, a
        jp   nz, 00009$
        ld   a, l
        cp   a, #4
        jp   c, 00009$
        cp   a, #249
        jp   nc, 00009$
        ld   (_hb_x), a
        ld   a, 5 (ix)          ; qy += vy
        ld   e, a
        rla
        sbc  a, a
        ld   d, a
        ld   l, 2 (ix)
        ld   h, 3 (ix)
        add  hl, de
        ld   2 (ix), l
        ld   3 (ix), h
        sra  h
        rr   l
        sra  h
        rr   l
        sra  h
        rr   l
        ld   a, h               ; y は 8..204
        or   a, a
        jp   nz, 00009$
        ld   a, l
        cp   a, #8
        jp   c, 00009$
        cp   a, #205
        jp   nc, 00009$
        ld   (_hb_y), a
        ld   a, (_g_loop_t)     ; 当たり: 四角の中心と自機の中心が ±4 以内(=左上どうしで x-px-2 が 0..8)
        or   a, a
        jr   nz, 00005$
        ld   a, (_g_player_x)
        ld   b, a
        ld   a, (_hb_x)
        sub  a, b
        sub  a, #2
        cp   a, #9
        jr   nc, 00005$
        ld   a, (_g_player_y)
        ld   b, a
        ld   a, (_hb_y)
        sub  a, b
        sub  a, #2
        cp   a, #9
        jr   nc, 00005$
        ld   a, (_g_player_x)   ; ent_player_hit(g_player_x, g_player_y): HL, DE
        ld   l, a
        ld   h, #0
        ld   a, (_g_player_y)
        ld   e, a
        ld   d, #0
        call _ent_player_hit
        jr   00009$
    00005$:
        ld   a, (_hb_x)
        srl  a
        ld   7 (ix), a
        ld   a, (_cam)
        ld   b, a
        ld   a, (_hb_y)
        add  a, b
        ld   8 (ix), a
        ld   6 (ix), #1
        ld   c, #0xCC
        call _hb_put
    00009$:
        ld   de, #9
        add  ix, de
        pop  bc
        dec  b
        jp   nz, 00001$
        pop  ix
        ret
    __endasm;
}

/* 中ボスが去る: 弾を海へ戻し、借りていた帯を CPU 弾幕へ返す */
void hot_hb_clear(void) __naked {
    __asm
        push ix
        ld   ix, #0xEC00
        ld   b, #24
    00001$:
        ld   a, 6 (ix)
        or   a, a
        jr   z, 00002$
        push bc
        ld   c, #0
        call _hb_put
        pop  bc
        ld   6 (ix), #0
    00002$:
        ld   de, #9
        add  ix, de
        djnz 00001$
        pop  ix
        jp   _curtain_reset
    __endasm;
}
