#!/bin/sh
# gen_syms_tcl.sh — build/rom.map から openMSX 用のシンボル表(Tcl)を作る。
#   使い方: sh tools/gen_syms_tcl.sh > /path/syms.tcl   → 測定スクリプトの先頭で source し、$A(g_alert) のように使う
#   ★常駐の番地はビルドのたびに動く。ハードコードすると「別の変数へ書き込んで誤診」する(何度か踏んだ)。
sed -n 's/^ *\([0-9A-F]\{8\}\) *_\([A-Za-z0-9_]*\) *[a-z_0-9]*$/set A(\2) 0x\1/p' build/rom.map | sed 's/0x0000/0x/' | sort -u
