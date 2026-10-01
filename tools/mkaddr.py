#!/usr/bin/env python3
"""mkaddr.py — build/rom.map から openMSX の Tcl 用アドレス表を吐く。
   ★アドレスはビルドごとに動く。ハーネスに直書きすると静かに別の変数を叩く
     (実際 g_invinc(0xDE6F) のつもりが g_stage_sel(0xDE6D) を叩いて「無敵が効かない」まま測っていた)。
   使い方: python3 tools/mkaddr.py > addr.tcl → Tcl 側で source して $A(g_invinc) で引く。"""
import re, sys
want = sys.argv[1:] or ['g_scene','g_invinc','g_stage_sel','g_vscroll','g_lives','g_score',
                        'g_crush_t','g_mb','g_spr_limit','g_spr_base','g_spr_used',
                        'g_spr_hide_to','stage_update','vdp_wait_frame','bank_data']
m={}
for mm in re.finditer(r'0000([0-9A-F]{4})  (_[A-Za-z_0-9]+)', open('build/rom.map').read()):
    m.setdefault(mm.group(2), int(mm.group(1),16))
for k in want:
    if '_'+k in m: print(f"set A({k}) 0x{m['_'+k]:04X}")
    else: print(f"# 見つからず: {k}", file=sys.stderr)
