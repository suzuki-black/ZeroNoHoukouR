/* sprites.h — スプライトパターンの集約。各シーンは init で sprites_load() を1回呼ぶ。
   パターン番号は 4 の倍数(mode2 16x16 は 4パターン=32B 消費)。 */
#ifndef SPRITES_H
#define SPRITES_H

#include "types.h"

#define SPR_PWRLV   0   /* ★パワーアップ段階の山形(階級章)。本数は色表で決める(絵は固定)。
                           ★津波の間はこの枠を波の「面」が借りる(HUD を出さないので空く)。終わったら
                             ovl_crush_wave_off() が VRAM の控え(WAVE_ART_VRAM の7枚目)から描き直す。
                           元は SPR_BLOCK(16x16 中実の汎用マーカ)だったが、パターンを投入するだけで
                           どこからも参照されない死枠だったので転用した。 */
#define SPR_BULLET  4   /* 弾(中央 6x6)                */
#define SPR_WAVE4   8   /* ★津波の5コマ目(裾)。**ボム棒 SPR_CRUSH と枠を共有する**。
                           津波の間は HUD を出さない(拡大モードなので出せない)ので奪ってよく、
                           終わったら hud_colors() が棒のパターンを描き直す。
                           16x16 の空き枠が 144-156 の4つしか無いための苦肉の策。 */
#define SPR_CRUSH   8   /* ★ボム(メガクラッシュ)残数の棒。1〜3本を hud.c が動的に描き換える。
                           元は SPR_FIGHTER(敵戦闘機・下向き△)だったが、どこからも参照されず
                           パターンも投入されていない完全な死枠だったので転用した。
                           16x16 の低位枠はここしか空いておらず(12..51=数字)、144-156 は
                           DEBUG_FPS の "MASK" が使う。 */
#define SPR_DIGIT0  12  /* 数字0のパターン番号。数字d = SPR_DIGIT0 + d*4(HUD用, BIOSフォント) */
#define SPR_SCOREHI 52  /* ★スコア上位2桁を詰めた 16x16(左8列=万の位 / 右8列=千の位)。HUD が1走査線に
                       並べる枚数を減らすため(最低優先度にした=先に欠けるのは HUD 側)。桁が変わった
                       ときだけ hud.c が焼き直す。
                       ★ここは元 SPR_TURRET(主砲塔の絵)。VRAM に入れているだけで**誰も参照して
                         いない死枠**だった(ドームは背景、砲身は SPR_BARREL0)。16x16 の空き枠は
                         他に無いので転用した。数字は12..48を占有→次は56 */

/* ★パワーアップ(ROADMAP P2-B)。太い自機弾(縦/左上/右上)と増槽アイテム。
   元は海イントロ敵機の手描き4機種(Bf109/F4U/Spitfire/Fw190)の枠だったが、戦闘機は8方向の手続き生成(SPR_PLANE_*)へ
   移ってどこからも参照されず、パターンも投入されていなかったので転用した。 */
#define SPR_PWV      56   /* 太い自機弾・縦 */
#define SPR_PWL      60   /* 太い自機弾・左上 */
#define SPR_PWR      64   /* 太い自機弾・右上 */
#define SPR_TANK     68   /* 増槽 */
#define SPR_HELLCAT  72  /* 米: F6F = 幅広角形の直線翼(ずんぐり) */

/* 自機=零戦(A6M, 機首上向き)。プロペラ回転の2コマ(先頭行を交互)。色は zcol の行別陰影。 */
#define SPR_ZERO    76   /* 零戦 コマA(プロペラ細) */
#define SPR_ZERO2   80   /* 零戦 コマB(プロペラ太=回転ブラー) */
#define SPR_PBULLET 84   /* 自機弾=赤い縦ストリーク(旧pat44) */
#define SPR_EBSHELL 88   /* 敵の時限信管弾=太いカプセル(旧pat100)。通常敵弾より大きく予告的 */
#define SPR_EXP0    92   /* 爆発アニメ4コマ(核→炸裂→輪→残火)。撃破/被弾で使用 */
#define SPR_EXP1    96
#define SPR_EXP2    100
#define SPR_EXP3    104
#define SPR_FLASH   108  /* マズルフラッシュ(自機発砲時の一瞬の光) */
/* ★津波のコマ6枚＋階級章の控え1枚(224B)の置き場: VRAM の 221行から。宙返りのコマ(212〜220行)とスプライト色表
   (224行〜)の間の、表示にも計算にも使われていない VRAM。面の準備(ship_render の load_sprites_impl)
   がここへ焼き、オーバレイ(ovl_crush)は読み戻して使う。
   ★なぜ VRAM 経由か: ovl_crush.c は 5 つのオーバレイ(通常面＋中ボス4種)に同じ内容で入り、
     いちばん詰まっている ovl9(2面の中ボス)は残り数バイトしかなかった。絵をコードの枠から
     追い出すことで、波頭を傾き別に3枚持てるようにした。 */
#define WAVE_ART_VRAM 0x6E80u   /* 221 * 128 */

#define SPR_WAVE0   144  /* ★津波のコマ: 波頭 上り(144)/下り(148)/急な下り(152)/胴(156)。
                            面は SPR_PWRLV(0)、裾は SPR_WAVE4(8) を借りる。
                            ★波頭が3枚あるのは、コマの中の波頭にもその列の傾きを焼いてあるから
                              (水平なコマだけだと 32px ごとの階段に見える)。
                            16x16 の空き枠はここだけ。スプライト拡大(MAG)で1枚32x32ドットになり、
                            8枚で画面幅を覆う。色は段ごとの行別カラーテーブル(ovl_crush.c)。
                            ★継ぎ目: コマの中の傾きと、隣の列との段差(= SHEAR - うねりの増分)を
                              一致させてあるので、端で波頭が繋がる。並べる順は決め打ち
                              (ovl_crush.c の crestk[] が うねりの位相から選ぶ)ので入替え不可。
                            ★DEBUG_FPS の "MASK" 文字がここを使っていたが、津波に譲って文字は
                              廃止した(数値 mask は FPS の隣に出るので切り分けには十分)。 */
#define SPR_BARREL0 112  /* 主砲の可動砲身(8方向, dir d = SPR_BARREL0 + d*4)。自機を狙って回転。112..140 */
/* ★敵戦闘機/艦載機の8方向スプライト(手続き生成, 面別ロード)。各サイズ 8方向×4パターン=32枠。
   小=浮上初期/中=浮上後期/大=飛行。パターン番号 = base + dir*4 (dir 0=上,時計回り=dir8と一致)。 */
#define SPR_PLANE_S 160  /* 小(発艦ホバー初期)   160..188 */
#define SPR_PLANE_M 192  /* 中(発艦ホバー後期)   192..220 */
#define SPR_PLANE_L 224  /* 大(通常飛行)         224..252 */
extern const u8 zcol[16];        /* 零戦の16行カラーテーブル(緑系+ハイライト) */
extern const u8 barrel_col[16];  /* 砲身の行別シェード(金属感の多色) */
extern const u8 barrel_flash[16];/* 砲身の命中フラッシュ(白) */

#define GEN_PLANES_BANK 19   /* banked/gen_planes.c を置くROMバンク(戦闘機8方向の手続き生成) */
#define COLDSETUP_BANK  30   /* banked/coldsetup.c を置くROMバンク(面の準備の配置処理) */
void sprites_load(u8 stage);   /* 全静的パターン(bank16)＋その面の戦闘機8方向×3サイズ(bank19) を VRAM へ投入 */

#endif /* SPRITES_H */
