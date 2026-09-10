# タイトル(SCREEN12/YJK) → SPACE → ステージ(SCREEN5) の通し検証。
# モード往復の回帰テスト: R#25 残留(YJK)で海が白化する不具合を捕まえる(海が青ければOK)。
#   1) 起動〜YJKロード確立を時間待ち(C-BIOSロゴ + 54KB流し込み)してタイトルを撮影
#   2) SPACE(row8 bit0)を短く押して開始
#   3) g_scene==2(STAGE)で SCREEN5 復帰を撮影
# 注:タイトルは g_scene==0 だが電源時BSSゼロと同値のためID検出に使えない→時間待ちで撮る。
#    STAGE(==2)はゼロ既定と非衝突なので確実。g_scene の番地は build/rom.noi の _g_scene で確認。
set b [file dirname [info script]]
set SCENE 0xC3CB
proc rd {a} { debug read memory $a }
proc shot {f} { global b; screenshot -raw -prefix {} [file join $b .. build $f] }
proc pressSpace {} { keymatrixdown 8 0x01; after time 0.2 { keymatrixup 8 0x01 } }
proc waitStage {} { global SCENE
    if {[rd $SCENE] == 2} { after time 3.0 { shot flow_stage.png; exit } } else { after time 0.2 waitStage } }
after time 7.0 { shot flow_title.png; pressSpace; after time 1.0 waitStage }
after time 40 "exit"
