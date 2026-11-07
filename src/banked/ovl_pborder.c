/* ovl_pborder.c — 自機弾と敵弾に、スプライトの枠を交互に先に渡す(通常面・1〜4 面の中ボス・撃沈のオーバレイ)。
   ★呼ぶ所: 通常面は ovl_power_frame の頭(常駐の power_frame。対空砲の当たり判定の後)、中ボスは ovl_mb_frame の頭、
     撃沈中は defeat_update が power_frame の口(ovl7 の slot21)から。5 面の中ボス(P-61)は 32 枚すべて使えるので入れていない。
   ★常駐の ent_draw_all は、実体を「同じ色の集まり」ごとにプールの並び順で枠へ置く。中ボスや弾幕で枠が 11〜15 枚に
     減った間、プールで敵弾(や爆発)が自機弾より前にあると、敵弾の集まりが先に枠を使い切り、自機弾が 1 枚も描かれなかった。
     弾は生きていて当たり判定も効くのに、何秒も全部見えない(3 面の中ボス・5 面の衝撃波の後。実機で指摘。
     1 面からの通しプレイを openMSX で自動で回すと、3 面の中ボスで自機弾 7 発・描かれた枠 0 が続いた)。
   ★1 フレームおきに「プールで最初の自機弾」と「最初の敵側(下の★)」の中身を入れ替え、先に並ぶ方を交互にする。
     枠が足りない間は自機弾も敵弾も点滅になり、どちらかが消えきることは無い。
   ★常駐(ent_draw_all)に書かないのは、常駐の空きが 25B しか無く、あの関数は C を少し変えるだけで SDCC 4.6 が
     内部エラー(error 9, gen.c)になるため(Makefile の entity.rel の★)。
   ★入れ替えるのは短命な実体どうしなので、実体を指したまま持ち越す所は無い(貫通弾が持つ砲台の番地 ax は弾と一緒に動く)。
   ★static は最後に置く(各オーバレイの .rel の並びの末尾にリンクする)。ほかの static の番地をずらさないため。 */
#include <stddef.h>
#include "types.h"
#include "entity.h"
#ifdef PBO_LITE
#define PBO_SMALL
#endif

/* 下の asm が前提にしている形 */
typedef char pbo_sz[(sizeof(Entity) == 27) ? 1 : -1];
typedef char pbo_team[(offsetof(Entity, team) == 20) ? 1 : -1];
typedef char pbo_type[(ET_BULLET == 2) ? 1 : -1];

static u8 pbo_tog;

/* ★入れ替えの相手(最初の「敵側」)は、敵弾・対空砲の時限信管弾(ET_AABURST=8)・爆発(ET_EXPLOSION=7)。
     信管弾は敵弾と同じ色なので同じ集まりになり、プールで信管弾が先にあると敵弾の集まりが先に枠を取った(5 面の戦艦モード)。
     爆発は 4 面の中ボス(枠 5 枚)で先に枠を埋めていた。どれも短命で、指したまま持ち越す所が無い。
   ★PBO_LITE(2 面の中ボスのオーバレイ)は敵弾だけを相手にし、画面外の自機弾も除かない。PBO_SMALL(4 面の中ボス)は
     画面外の自機弾を除かない。どちらもオーバレイに空きが無いため */
typedef char pbo_exp[(ET_EXPLOSION == 7 && ET_AABURST == 8) ? 1 : -1];
void ovl_pb_order(void) __naked {
    __asm
        ld   hl, #_pbo_tog
        inc  (hl)                    ; 下位ビットが 1 フレームおきに入れ替わる
        call _ent_pool               ; DE = プールの先頭
        ld   hl, #27 * (ENT_MAX - 1)
        add  hl, de                  ; HL = 最後の実体。後ろから見るので、最後に覚えたものが「最初」になる
        ld   bc, #0                  ; BC = 最初の敵側(B = 0 なら無し)
        ld   d, b                    ; DE = 最初の自機弾(D = 0 なら無し)
        ld   a, #ENT_MAX
    00001$:
        push af
        ld   a, (hl)                 ; active
        or   a
        jr   z, 00004$
        inc  hl
        ld   a, (hl)                 ; type
        dec  hl
        cp   #2                      ; ET_BULLET
        jr   z, 00007$
#ifdef PBO_LITE
        jr   00004$
#else
        sub  #7                      ; ET_EXPLOSION / ET_AABURST
        cp   #2
        jr   nc, 00004$
#endif
    00003$:
        ld   b, h                    ; 敵側
        ld   c, l
        jr   00004$
    00007$:
        push hl
        ld   a, #20                  ; team
        add  a, l
        ld   l, a
        jr   nc, 00002$
        inc  h
    00002$:
        ld   a, (hl)
        pop  hl
        or   a
        jr   z, 00003$
#ifndef PBO_SMALL
        push hl                      ; ★画面の上へ抜けかけた自機弾(y < 0)は描かれないので相手にしない。
        inc  hl                      ;   プールで最初の自機弾がそれだと、入れ替えても自機弾の集まりが先にならなかった
        inc  hl
        inc  hl
        inc  hl
        inc  hl
        ld   a, (hl)                 ; y の上位
        pop  hl
        or   a
        jr   nz, 00004$
#endif
        ld   d, h                    ; 自機弾
        ld   e, l
    00004$:
        ld   a, l                    ; 1 つ前の実体へ(27 = sizeof(Entity))
        sub  #27
        ld   l, a
        jr   nc, 00005$
        dec  h
    00005$:
        pop  af
        dec  a
        jr   nz, 00001$
        ld   a, b                    ; 両方あるときだけ(実体の番地の上位は 0 にならない)
        and  d
        ret  z
        ld   h, d                    ; (and で carry は 0)
        ld   l, e
        sbc  hl, bc                  ; carry = 自機弾が先
        sbc  a, a
        ld   hl, #_pbo_tog
        xor  (hl)
        rra                          ; carry = 望みの順(pbo_tog の下位が 1 なら自機弾が先)と逆
        ret  nc
        ld   h, b                    ; 中身を入れ替える(HL = 敵側, DE = 自機弾)
        ld   l, c
        ld   b, #27
    00006$:
        ld   a, (de)
        ld   c, (hl)
        ld   (hl), a
        ld   a, c
        ld   (de), a
        inc  hl
        inc  de
        djnz 00006$
        ret
    __endasm;
}
