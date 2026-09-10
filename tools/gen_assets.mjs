// gen_assets.mjs — BGM 曲データ(＋将来の他アセット)をデータバンク bin へパック。
//   出力: build/assets.bin   … データバンク(rompack が --bank へ生配置)の内容
//         build/bgm_data.h   … 常駐Cが読む定数(音階periodテーブル/曲オフセット/長さ)
//
//   トラック1本のバイト列(バンクに置き、再生時に data_read で RAM へコピー):
//     [ nMel, nBas, basStep, melPeak, melSus, melVib, basPeak, basSus, drumOn,
//       melNote(nMel), melLen(nMel), basNote(nBas) ]
//       melNote/basNote = 音階index(0=C2..47=B5) or 255=休符
//       melLen  = 各音符のフレーム数(60Hz)。bass は固定 basStep。
//       envelope: 発音開始 peak → 毎フレーム-1 → sustain 保持、末尾2フレーム無音。
//       vib=1: 伸ばし音(el>8)に三角ビブラート。drumOn=1: 標準マーチドラム(noise)。
//   ※旧 BattleshipProto/cport の bgm_tracks.h からタイトル/1面を移植(音階index基準は同一)。
//
// 使い方: node tools/gen_assets.mjs <bank番号> build/assets.bin build/bgm_data.h

import { writeFileSync, readFileSync } from 'node:fs';

const [, , bankArg, binOut, hdrOut] = process.argv;
const BGM_BANK = parseInt(bankArg ?? '8', 10);

// ---- PSG tone period テーブル(12bit)。TP = 1.7897725MHz/(16*freq) ----
const notetp = [];
for (let i = 0; i < 48; i++) {
  const freq = 440 * Math.pow(2, (36 + i - 69) / 12);   // midi=36+i, A4(midi69)=440Hz
  notetp.push(Math.round(111860.78125 / freq));
}

// ---- 曲データ(旧cport bgm_tracks.h より移植。melody/bass=音階index, 255=休符) ----
const TRACKS = [
  { // 0: タイトル(アレスタ2風, ニ短調, 四分16f)
    mel: [33,38, 36,34,33, 29,31,33, 26,33, 34,33,31, 33,38, 36,34,33,31, 29,
          38,36, 34,33,34, 36,38, 33,33, 34,33,31, 29,31,33, 31,29,28, 26,255],
    mln: [32,32, 16,16,32, 16,16,32, 48,16, 32,16,16, 32,32, 16,16,16,16, 64,
          32,32, 16,16,32, 32,32, 48,16, 32,16,16, 16,16,32, 32,16,16, 48,16],
    bas: [2,9,14,9, 2,9,14,9, 5,5,17,5, 2,9,14,2, 10,10,22,10, 2,9,14,9, 2,9,14,9, 5,5,17,5,
          2,9,14,9, 10,10,22,10, 2,9,14,9, 9,9,21,9, 10,10,22,10, 2,9,14,9, 9,9,21,9, 2,9,14,2],
    basStep:16, melPeak:14, melSus:11, melVib:1, basPeak:11, basSus:8, drum:1,
  },
  { // 1: 1面(戦艦)マーチ(八分8f)
    mel: [26,26,33,26,29,26,33, 34,33,31,29,28,26,
          26,26,33,26,29,33,38, 36,34,33,31,29,28,
          26,26,33,26,29,26,33, 34,33,31,29,31,33,
          33,33,38,33,34,33,31, 29,28,26,25,26,
          33,38,41,40, 38,40,41,40,38,36,33,
          38,36,34,33, 31,33,34,33,31,29,
          26,26,33,26,29,26,33, 34,33,31,29,28,26,
          33,31,29,28,26,25,28,31, 26,255],
    mln: [8,8,8,8,8,8,16, 8,8,8,8,16,16,
          8,8,8,8,8,8,16, 8,8,8,8,16,16,
          8,8,8,8,8,8,16, 8,8,8,8,16,16,
          8,8,8,8,8,8,16, 8,8,8,8,32,
          16,16,16,16, 8,8,8,8,8,8,16,
          16,16,16,16, 8,8,8,8,16,16,
          8,8,8,8,8,8,16, 8,8,8,8,16,16,
          8,8,8,8,8,8,8,8, 48,16],
    bas: [2,2,14,2,2,2,14,2, 2,2,14,2,2,2,14,2, 2,2,14,2,2,2,14,2, 2,2,14,2,9,9,21,9,
          2,2,14,2,2,2,14,2, 2,2,14,2,2,2,14,2, 2,2,14,2,2,2,14,2, 9,9,21,9,9,9,21,9,
          2,2,14,2,2,2,14,2, 2,2,14,2,2,2,14,2, 10,10,22,10,10,10,22,10, 9,9,21,9,9,9,21,9,
          2,2,14,2,2,2,14,2, 2,2,14,2,2,2,14,2, 9,9,21,9,9,9,21,9, 2,2,14,2,2,2,14,2],
    basStep:8, melPeak:14, melSus:11, melVib:1, basPeak:11, basSus:8, drum:1,
  },
  { // 2: エンディング(静かな讃歌, ヘ長調, ドラム無し・長い音長)
    mel: [33,36,38,36,33,31, 29,31,33,34,33, 36,38,40,38,36,33, 34,33,31,29,255],
    mln: [32,32,48,32,32,64, 48,32,48,32,64, 32,32,48,32,32,64, 48,32,48,64,64],
    bas: [5,17,5,17, 0,12,0,12, 2,14,2,14, 10,22,9,21],
    basStep:32, melPeak:11, melSus:9, melVib:1, basPeak:8, basSus:6, drum:0,   /* 意図的に絞った静かな讃歌(そのまま) */
  },
  { // 3: 2面 空母(行進曲, MEL37/BAS64)
    mel: [26,33,34,33,31,29,31,33,26, 33,38,36,34,33,31,29,28,26,25, 26,29,33,34,33,31,29,28, 29,33,38,36,34,33,31,29,26,255],
    mln: [32,32,32,16,16,32,16,16,64, 32,32,32,16,16,32,16,16,48,16, 32,32,48,16,32,16,16,64, 32,32,32,16,16,32,16,16,48,16],
    bas: [2,2,14,2,2,2,14,2,10,10,22,10,9,9,21,9, 2,2,14,2,2,2,14,2,5,5,17,5,9,9,21,9, 2,2,14,2,2,2,14,2,10,10,22,10,9,9,21,9, 2,2,14,2,2,2,14,2,10,10,22,9,9,9,21,2],
    basStep:8, melPeak:14, melSus:11, melVib:1, basPeak:11, basSus:8, drum:2,
  },
  { // 4: 3面 フッド(哀歌, ニ短調, ドラム無し・長音長)
    mel: [33,29,26,29,28,26,25,26,33,34,33,31,29,28,29,26,22,21,26, 33,34,33,31,29,28,26,25,26,255],
    mln: [16,16,48,16,16,16,48,24,24,16,32,16,16,32,16,16,16,48,32, 16,16,16,16,16,16,48,24,48,32],
    bas: [2,14,2,14,1,13,1,13,0,12,0,12,10,22,10,22,9,21,9,21,9,21,9,21,2,14,2,14,10,22,9,21],
    basStep:16, melPeak:14, melSus:12, melVib:1, basPeak:11, basSus:9, drum:3,   /* 音量を他曲(peak14/11)へ+3段シフト。内部の強弱(差=2)は維持 */
  },
  { // 5: 4面 双子(勇壮 fife&drum マーチ, MEL48)。ベースは空母のマーチ低音を流用。
    mel: [45,45,38,41,40,38,38,41,45,46,45,43,41,41,45,40,40,38, 45,46,45,43,41,40,38,45,45,38,41,40,38, 43,45,46,45,43,41,40,38,45,38,45,41,40,38,37,38,255],
    mln: [8,8,16,8,8,16,8,8,16,8,8,16,8,8,16,8,8,16, 8,8,8,8,8,8,16,8,8,16,8,8,16, 8,8,8,8,8,8,16,8,8,8,8,8,8,8,8,32,16],
    bas: [2,2,14,2,2,2,14,2,10,10,22,10,9,9,21,9, 2,2,14,2,2,2,14,2,5,5,17,5,9,9,21,9, 2,2,14,2,2,2,14,2,10,10,22,10,9,9,21,9, 2,2,14,2,2,2,14,2,10,10,22,9,9,9,21,2],
    basStep:8, melPeak:14, melSus:11, melVib:1, basPeak:11, basSus:8, drum:1,
  },
  { // 6: 5面 アイオワ(クライマックス, ニ短調・半音階・8分疾走, MEL40/BAS32)
    mel: [26,26,33,26,29,29,38,29,37,38,40,41,40,38,37,33,38,45,41,45,40,41,43,45,46,45,43,41,40,38,37,38,33,34,33,29,28,26,26,255],
    mln: [8,8,8,16,8,8,8,16,8,8,8,16,8,8,8,16,8,8,8,8,8,8,8,16,8,8,8,8,8,8,8,16,16,8,8,16,8,16,32,16],
    bas: [2,2,14,2,2,2,14,2,9,9,21,9,9,9,21,9,2,2,14,2,5,5,17,5,9,9,21,9,9,9,21,2],
    basStep:8, melPeak:14, melSus:11, melVib:1, basPeak:11, basSus:8, drum:3,
  },
  { // 7: 海イントロ共通(インターバル/渋。ニ短調・スロー foreboding のメロディ＋ロックマン風シンセドラム)
    //     メロディ: 下降 D-C-Bb-A → 半音階 C#で戻し → 高A4(属音)で解決させず「激戦の予感」を宙吊り。
    //     ベース(chB)=シンセドラム: D3刻み＋A3/D4のトム(各打撃で1oct上→基音へ急降下)。音量は他曲並みに。
    mel: [26,24,22,21,255, 21,22,24,26,25,26,255, 33,29,26,255],
    mln: [48,32,32,64,32, 48,32,32,48,32,64,32, 64,48,96,48],
    bas: [14,21,14,255, 14,26,21,255],   // D3 A3 D3 (休) / D3 D4 A3 (休)=間のある渋いシンセドラム(刻みすぎ回避)
    basStep:16, melPeak:14, melSus:11, melVib:1, basPeak:14, basSus:0, drum:0, bassSweep:1,
  },
];

function packTrack(t) {
  if (t.mel.length !== t.mln.length) throw new Error('mel/mln length mismatch');
  for (const v of [...t.mel, ...t.bas]) if (v < 0 || v > 255) throw new Error(`note out of range: ${v}`);
  return Buffer.from([
    t.mel.length, t.bas.length, t.basStep,
    t.melPeak, t.melSus, t.melVib, t.basPeak, t.basSus, t.drum,
    t.bassSweep ?? 0,   /* 1=chBをシンセドラム(ピッチ急降下＋打撃減衰) */
    ...t.mel, ...t.mln, ...t.bas,
  ]);
}

// ---- 艦体 OPS データ(旧版 BattleshipProto の ship_ops/iowa_ops を忠実移植) ----
// 1レコード=7B {op, x, ylo, yhi, p1, p2, p3}, op=0 で終端。y は 16bit(0..495)。
// op: 1=GROUND 2=MAINGUN 3=DOME 4=DISK 5=DECKBOX 6=AAGUN 7=LMMV 8=BARREL (ship.h と一致)。
// ソースは 6値/レコード {op,x,y,p1,p2,p3}。ここで y を 2バイト分割して 7B に展開。
function shipops(recs) {
  const b = [];
  for (const [op, x, y, p1, p2, p3] of recs) b.push(op, x, y & 0xFF, (y >> 8) & 0xFF, p1, p2, p3);
  b.push(0);   // END
  for (const v of b) if (v < 0 || v > 255) throw new Error(`shipop byte out of range: ${v}`);
  return Buffer.from(b);
}
// 各面の艦(上面視, 艦高496px, 中心x=128)。hull=船体プロファイル(0=Bismarck/3=Iowa)、
// bowCnt/bowYb=波切り艦首シェブロン。船体/艦首/対空砲23基はコード(ship_render)、OPSは砲塔/艦橋/煙突/副砲。
const SHIPS = [
  { name: 'BISMARCK', kind: 0, hull: 0, bowCnt: 20, bowYb: 42, aagTbl: 0, aagP: [7,5,6,5],
    ops: shipops([
      [7,121,6,2,26,13], [7,135,6,2,26,13],                                  // 艦首波(暗)
      [3,118,34,5,0,0], [3,138,34,5,0,0],                                    // 前部ドーム
      [1,128,64,17,0,0], [2,128,64,15,0,0],                                  // Anton 主砲
      [1,128,104,19,0,0], [2,128,104,17,0,0],                                // Bruno 主砲
      [5,106,124,44,62,5],                                                   // 前部艦橋 基部
      [7,110,130,36,3,15],                                                   // 窓帯 下
      [5,112,134,32,46,5],                                                   // 2段
      [7,116,140,24,3,15],                                                   // 窓帯 上
      [5,116,146,24,30,4],                                                   // 3段(暗)
      [5,120,152,16,18,5],                                                   // 4段(頂)
      [3,128,130,9,0,0],                                                     // 司令塔
      [3,128,158,7,0,0],                                                     // 上部艦橋
      [3,128,170,6,0,0], [4,128,170,4,13,0],                                 // 前檣測距儀
      [1,104,200,7,0,0], [3,104,200,5,0,0],                                  // 15cm副砲 前左
      [1,152,200,7,0,0], [3,152,200,5,0,0],                                  // 前右
      [7,98,212,60,3,13],                                                    // 射出機レール
      [4,128,228,13,13,0], [4,128,226,11,5,0], [4,128,228,7,13,0],           // 煙突
      [7,114,236,28,4,4],                                                    // 煙突キャップ帯
      [1,104,250,7,0,0], [3,104,250,5,0,0],                                  // 副砲 後左
      [1,152,250,7,0,0], [3,152,250,5,0,0],                                  // 後右
      [7,128,262,2,26,9],                                                    // 主檣
      [5,112,266,32,34,5],                                                   // 後部指揮所 基部
      [5,116,272,24,24,4],                                                   // 2段(暗)
      [5,120,278,16,14,5],                                                   // 3段
      [3,128,276,8,0,0],                                                     // 後部管制
      [3,128,290,6,0,0], [4,128,290,3,13,0],                                 // 後部測距儀
      [1,128,322,19,0,0], [2,128,322,17,0,0],                               // Cäsar 主砲
      [1,128,362,17,0,0], [2,128,362,15,0,0],                               // Dora 主砲
      [7,112,396,4,48,5], [7,112,396,1,48,14],                              // 後甲板 縁
      [7,140,396,4,48,5], [7,140,396,1,48,14],
      [7,113,404,2,10,9], [7,108,407,12,2,4],
      [7,141,430,2,10,9], [7,136,433,12,2,4],
      [7,127,428,2,26,4], [3,128,452,6,0,0],                               // 艦尾
      [6,88,150,7,0,0], [6,168,150,7,0,0],                                 // 舷側対空砲ギャラリー12基
      [6,88,178,7,0,0], [6,168,178,7,0,0],
      [6,88,206,7,0,0], [6,168,206,7,0,0],
      [6,88,240,7,0,0], [6,168,240,7,0,0],
      [6,88,280,7,0,0], [6,168,280,7,0,0],
      [6,88,308,7,0,0], [6,168,308,7,0,0],
    ]),
  },
  // ---- 空母(2パス: carrier_ops→metalNoise→carrier2_ops)。飛行甲板/係留機はコード(ship_render kind2) ----
  { name: 'CARRIER', kind: 2, hull: 0, bowCnt: 0, bowYb: 0, aagTbl: 1, aagP: [6,6,6,6],
    ops: shipops([
      [5,140,78,34,36,4], [5,82,340,34,36,4],                              // エレベータ2
      [7,122,10,12,2,15],                                                  // 艦首甲板端マーク
      [1,95,100,9,0,0], [2,95,100,8,0,0], [1,161,100,9,0,0], [2,161,100,8,0,0],   // 5in砲(前)
      [1,95,300,9,0,0], [2,95,300,8,0,0], [1,161,300,9,0,0], [2,161,300,8,0,0],   // 5in砲(後)
      [5,134,132,50,104,4], [5,137,140,44,86,5], [5,140,150,38,40,5],      // 島(段積み)
      [7,143,155,32,5,15], [5,146,164,26,24,4],                            // 窓白帯/操舵室頂
    ]),
    ops2: shipops([
      [7,143,155,32,1,15], [7,143,155,1,5,14],                             // 窓上光/左光
      [3,159,158,7,0,0],                                                   // 操舵室ドーム
      [4,168,205,11,13,0], [4,164,201,8,5,0], [4,168,205,6,13,0],          // 煙突
      [7,158,214,22,5,4],                                                  // 煙突キャップ帯
      [7,158,168,3,30,9],                                                  // マスト
      [4,158,173,5,14,0], [4,158,185,3,15,0],                              // レーダー
      [6,159,136,7,0,0], [6,159,232,7,0,0],                                // 前後AAディレクタ
    ]),
  },
  // ---- フッド(単艦, 細い艦体 半幅46/鋭い艦首) ----
  { name: 'HOOD', kind: 0, hull: 1, bowCnt: 16, bowYb: 30, aagTbl: 2, aagP: [7,5,6,5],
    ops: shipops([
      [7,127,120,2,200,4],                                                 // 中央通路
      [1,128,70,14,0,0], [2,128,70,12,0,0], [1,128,108,14,0,0], [2,128,108,12,0,0],   // 主砲A/B
      [1,128,372,14,0,0], [2,128,372,12,0,0], [1,128,410,14,0,0], [2,128,410,12,0,0], // 主砲X/Y
      [5,114,144,28,50,4], [7,118,149,20,4,15], [5,116,153,24,34,5], [5,118,159,20,22,5],  // 艦橋城
      [3,128,148,7,0,0], [3,128,164,6,0,0], [3,128,176,5,0,0], [4,128,176,3,13,0],    // 司令塔/測距儀
      [4,128,216,9,13,0], [4,128,213,7,4,0], [4,128,216,6,13,0], [7,120,222,16,3,4],  // 煙突1
      [4,128,258,8,13,0], [4,128,256,6,4,0], [4,128,258,5,13,0], [7,121,264,14,3,4],  // 煙突2
      [5,118,296,20,26,5], [5,120,301,16,16,4],                            // 後部構造2段
      [3,128,300,5,0,0], [3,128,312,5,0,0], [4,128,312,3,13,0],            // 後部指揮所/測距儀
    ]),
  },
  // ---- 双子戦艦(2隻: L cx=76 / R cx=180, 細い艦体 半幅24)。船体/艦首は各hull、OPSは絶対Xで両艦 ----
  { name: 'TWINS', kind: 1, hull: 2, bowCnt: 12, bowYb: 24, aagTbl: 3, aagP: [6,5,5,5],
    ops: shipops([
      [7,75,120,2,200,4],                                                  // L中央通路
      [1,76,80,11,0,0], [2,76,80,9,0,0], [1,76,360,11,0,0], [2,76,360,9,0,0],   // L前後主砲
      [5,64,148,24,46,4], [7,67,152,18,4,15], [5,66,156,20,30,5], [5,68,162,16,18,4],  // L艦橋城
      [3,76,150,6,0,0], [3,76,166,5,0,0], [4,76,166,3,13,0],               // L司令塔/測距儀
      [4,76,222,7,13,0], [4,76,219,5,4,0], [4,76,222,4,13,0], [7,69,228,14,3,4],   // L煙突
      [5,66,300,20,22,5], [3,76,304,5,0,0],                                // L後部構造
      [7,179,120,2,200,4],                                                 // R中央通路
      [1,180,80,11,0,0], [2,180,80,9,0,0], [1,180,360,11,0,0], [2,180,360,9,0,0], // R前後主砲
      [5,168,148,24,46,4], [7,171,152,18,4,15], [5,170,156,20,30,5], [5,172,162,16,18,4],  // R艦橋城
      [3,180,150,6,0,0], [3,180,166,5,0,0], [4,180,166,3,13,0],            // R司令塔/測距儀
      [4,180,222,7,13,0], [4,180,219,5,4,0], [4,180,222,4,13,0], [7,173,228,14,3,4],  // R煙突
      [5,170,300,20,22,5], [3,180,304,5,0,0],                              // R後部構造
    ]),
  },
  // ---- アイオワ(5面クライマックス。半幅50/長い平行艦体) ----
  { name: 'IOWA', kind: 0, hull: 3, bowCnt: 22, bowYb: 28, aagTbl: 0, aagP: [7,5,6,5],
    ops: shipops([
      [1,128,72,21,0,0], [2,128,72,19,0,0],                                 // 主砲1
      [1,128,108,23,0,0], [2,128,108,21,0,0],                               // 主砲2
      [5,112,146,32,56,4],                                                  // 前部艦橋 基部
      [7,116,151,24,4,15],                                                  // 窓帯 下
      [5,114,156,28,42,5],                                                  // 2段
      [7,118,161,20,3,15],                                                  // 窓帯 上
      [5,116,166,24,28,4],                                                  // 3段(暗)
      [5,120,172,16,18,5],                                                  // 4段
      [3,128,150,8,0,0], [3,128,168,7,0,0],                                 // 司令塔＋上部艦橋
      [3,128,180,6,0,0], [4,128,180,4,13,0],                                // 前檣測距儀
      [4,128,226,11,13,0], [4,128,223,8,4,0], [4,128,226,6,13,0], [7,118,232,20,4,4],  // 煙突1
      [4,128,262,10,13,0], [4,128,259,7,4,0], [4,128,262,5,13,0], [7,120,268,16,3,4],  // 煙突2
      [5,116,280,24,26,5], [5,118,285,20,16,4],                             // 後部指揮所2段
      [3,128,284,7,0,0], [3,128,298,6,0,0], [4,128,298,3,13,0],             // 後部管制＋測距儀
      [1,128,330,23,0,0], [2,128,330,21,0,0],                              // 主砲3
      [1,128,372,21,0,0], [2,128,372,19,0,0],                              // 主砲4
    ]),
  },
];

// ---- 撃破エンプレの炎上火球(旧版 render_fireball 移植: 丸い火球=赤本体＋橙＋白芯＋ギザギザ舌) ----
// SCREEN5(2px/byte)の box×box ビットマップを生成。色0=透明(コピー時に艦BGを透かす)。四角ではなく円。
function fbDisk(px, box, cx, cy, r, col) {
  const rr = r * r;
  for (let y = 0; y < box; y++) for (let x = 0; x < box; x++) {
    const dx = x - cx, dy = y - cy;
    if (dx * dx + dy * dy <= rr) px[y * box + x] = col;
  }
}
// frame=0/1 の2コマ。★外形(赤本体＋舌)は両コマ完全に同一シルエットにし、内側の白熱芯の大きさだけ
// 変える(燃焼のちらつき)。→ 透過コピーで前コマを完全に上書きでき、下地復元なしでも残像が出ない(軽量)。
function fireballBmp(box, frame) {
  const px = new Uint8Array(box * box);            // 0=透明
  const c = (box - 1) / 2, r = box / 2 - 1;
  for (let t = 0; t < 12; t++) {                    // ギザギザの赤い舌(両コマ同一位相=footprint固定)
    const a = (t / 12) * Math.PI * 2;
    fbDisk(px, box, c + Math.cos(a) * (r - 1), c + Math.sin(a) * (r - 1), 1.8, 11);
  }
  fbDisk(px, box, c, c, r - 1, 11);                 // 赤い本体(両コマ同一)
  fbDisk(px, box, c, c, Math.max(1, (r * 3 / 5) | 0), 12);   // 橙(両コマ同一, 赤の内側)
  // 白熱の芯だけコマで拡縮(赤本体の内側に必ず収まる=footprintは赤本体のまま不変)。
  const wr = frame ? Math.max(1, (r * 3 / 10 + 1) | 0) : Math.max(1, (r * 3 / 10 - 1) | 0);
  fbDisk(px, box, c, c, wr, 15);
  const bytes = box / 2, out = Buffer.alloc(box * bytes);    // 2px/byte へパック
  for (let y = 0; y < box; y++) for (let b = 0; b < bytes; b++)
    out[y * bytes + b] = (px[y * box + b * 2] << 4) | px[y * box + b * 2 + 1];
  return out;
}
const FB_BOXES = [32, 22, 16];                       // 大(主砲=ドームを包む)/中(大型AA)/小(極小AA)
// 3サイズ × 2コマ = 6枚。順=[s0f0,s0f1, s1f0,s1f1, s2f0,s2f1]。
const fbBlobs = FB_BOXES.flatMap((b) => [fireballBmp(b, 0), fireballBmp(b, 1)]);

// ---- 面別ドラム(旧版 drmPat/drmPat2/drmPat3 移植)。各スタイル=[pat16, v0(4), tempo] の21B。 ----
// スタイル1=標準マーチ / 2=重い戦闘(空母) / 3=激しい刻み(フッド/アイオワ, テンポ速)。常駐節約でバンクへ。
// 1=キック/2=スネア/3=ハット。v0[type]=初期音量, tempo=1ステップのフレーム数。
const DRUM_STYLES = [
  { pat:[1,3,2,3, 1,3,2,3, 1,3,2,3, 1,2,2,3], v0:[0,14,13,6], tempo:8 },  // 1 標準マーチ
  { pat:[1,3,2,3, 1,1,2,3, 1,3,2,3, 2,2,1,3], v0:[0,15,15,8], tempo:8 },  // 2 重い戦闘(空母)
  { pat:[1,2,1,2, 1,2,1,2, 1,2,1,2, 1,2,2,2], v0:[0,10, 9,4], tempo:6 },  // 3 激しい刻み(フッド/アイオワ)
];
// 各style を 32B ストライドにパディング(オフセット計算を *21→<<5 にして常駐のmul回避)。
const drumBlob = Buffer.from(DRUM_STYLES.flatMap((s) => {
  const blk = [...s.pat, ...s.v0, s.tempo];
  while (blk.length < 32) blk.push(0);
  return blk;
}));

// ---- 面名/撃沈メッセージを常駐から追い出す: 各16Bスロット(NUL終端)でデータバンクへ。 ----
// stagename=開始カード用, sunk_msg=結果画面用(旧版 g_L[10..14])。stage_build で当該面をRAMへ。
const STAGE_NAMES = ['BISMARCK', 'CARRIER', 'HOOD', 'TWINS', 'IOWA'];
const SUNK_MSGS   = ['BISMARCK SUNK', 'ESSEX SUNK', 'HMS HOOD SUNK', 'SISTERS SUNK', 'USS IOWA SUNK'];
function str16(arr) {
  const b = Buffer.alloc(arr.length * 16);   // 0埋め=NUL終端
  arr.forEach((s, i) => b.write(s, i * 16, 'ascii'));
  return b;
}
const strBlob = Buffer.concat([str16(STAGE_NAMES), str16(SUNK_MSGS)]);

// ---- 海イントロ敵機の行別カラー(陰影)を常駐から追い出す: 面別16B×5をデータバンクへ ----
const FIGHTER_CTAB = [
  [ 3, 3, 3, 3, 3, 3, 8,10, 8, 3, 3, 3, 3, 3, 3, 3],  // Bf109 独緑
  [12,12,11,12,12,12,14,12,11,12,12,12,11,12,12,12],  // Corsair 米橙
  [ 9, 3, 9, 9, 9, 9,10,10,10,10, 9, 9, 9, 9, 9, 9],  // Spitfire 英
  [14,14,13,14,14,14,15,15,14,14,14,14,13,14,13,14],  // Fw190 独灰
  [11,11,11,11,11,11,12,15,12,11,11,11,11,11,11,11],  // Hellcat 米赤
];
const fctabBlob = Buffer.from(FIGHTER_CTAB.flat());

// ---- 主砲4基の艦内(x,y)を常駐から追い出す: 面別 [x0..3(u8), y0..3(u16 LE)] = 12B×5 をバンクへ ----
const GUN_X = [[128,128,128,128],[95,161,95,161],[128,128,128,128],[76,76,180,180],[128,128,128,128]];
const GUN_Y = [[64,104,322,362],[100,100,300,300],[70,108,372,410],[80,360,80,360],[72,108,330,372]];
const gunBlob = Buffer.alloc(5 * 12);
{ let o = 0; for (let s = 0; s < 5; s++) { for (const x of GUN_X[s]) gunBlob[o++] = x;
    for (const y of GUN_Y[s]) { gunBlob.writeUInt16LE(y, o); o += 2; } } }

// ---- 撃破!! パネル(1bpp)を常駐から追い出す: 既存ヘッダのバイト列を読み、データバンクへ ----
const panelSrc = readFileSync(new URL('../src/include/panel_gekiha.h', import.meta.url), 'utf8');
const panelW  = +(panelSrc.match(/#define\s+PANEL_W\s+(\d+)/)[1]);
const panelWB = +(panelSrc.match(/#define\s+PANEL_WB\s+(\d+)/)[1]);
const panelH  = +(panelSrc.match(/#define\s+PANEL_H\s+(\d+)/)[1]);
const panelBytes = (panelSrc.match(/panel_gekiha\[[^\]]*\]\s*=\s*\{([\s\S]*?)\}/)[1]
  .match(/0x[0-9a-fA-F]+|\d+/g) || []).map((n) => Number(n) & 0xFF);
if (panelBytes.length !== panelWB * panelH) throw new Error(`panel bytes ${panelBytes.length} != ${panelWB * panelH}`);
const panelBlob = Buffer.from(panelBytes);

// ---- バンク配置(bank8): BGM曲 → 各艦の [ops, ops2] → 火球3枚 を連結 ----
const emptyops = shipops([]);   // ops2 が無い艦(1バイト END)
const bgmBlobs = TRACKS.map(packTrack);
const shipBlobs = SHIPS.flatMap((s) => [s.ops, s.ops2 || emptyops]);   // 艦ごとに ops, ops2 の2枚
const parts = [...bgmBlobs, ...shipBlobs, ...fbBlobs, panelBlob, drumBlob, strBlob, fctabBlob, gunBlob];
const offAll = [];
let cur = 0;
for (const b of parts) { offAll.push(cur); cur += b.length; }
const bgmOff = offAll.slice(0, bgmBlobs.length);
const shipOpsOff  = SHIPS.map((_, i) => offAll[bgmBlobs.length + i * 2]);
const shipOps2Off = SHIPS.map((_, i) => offAll[bgmBlobs.length + i * 2 + 1]);
const fbBase = bgmBlobs.length + shipBlobs.length;
const fbOff = fbBlobs.map((_, i) => offAll[fbBase + i]);
const panelOff = offAll[fbBase + fbBlobs.length];
const drumOff = offAll[fbBase + fbBlobs.length + 1];
const strOff  = offAll[fbBase + fbBlobs.length + 2];
const fctabOff = offAll[fbBase + fbBlobs.length + 3];
const gunOff = offAll[fbBase + fbBlobs.length + 4];
const bin = Buffer.concat(parts);
if (bin.length > 0x2000) throw new Error(`assets ${bin.length}B > 8KB bank`);
writeFileSync(binOut, bin);

const bgmRamMax = Math.max(...bgmBlobs.map((b) => b.length));
const shipRamMax = Math.max(...shipBlobs.map((b) => b.length));
const h = [
  '/* 自動生成(tools/gen_assets.mjs)。手で編集しない。BGM＋各面の艦体OPS をデータバンクへ。 */',
  '#ifndef ASSETS_DATA_H', '#define ASSETS_DATA_H',
  `#define ASSET_BANK ${BGM_BANK}`,
  '/* --- BGM --- */',
  `#define BGM_BANK ${BGM_BANK}`,
  `#define BGM_TRACK_COUNT ${TRACKS.length}`,
  `#define BGM_RAM_MAX ${bgmRamMax}`,
  `static const unsigned int bgm_notetp[48] = { ${notetp.join(',')} };`,
  `static const unsigned int bgm_off[BGM_TRACK_COUNT] = { ${bgmOff.join(',')} };`,
  `static const unsigned int bgm_len[BGM_TRACK_COUNT] = { ${bgmBlobs.map((b) => b.length).join(',')} };`,
  '/* --- 艦体 OPS(面ごと。data_read で SHIP_OPS_RAM_MAX の RAM へ読み ship_render で解釈) --- */',
  `#define STAGE_COUNT ${SHIPS.length}`,
  `#define SHIP_OPS_RAM_MAX ${shipRamMax}`,
  `static const unsigned int ship_ops_off[STAGE_COUNT]  = { ${shipOpsOff.join(',')} };`,
  `static const unsigned int ship_ops_len[STAGE_COUNT]  = { ${SHIPS.map((s) => s.ops.length).join(',')} };`,
  `static const unsigned int ship_ops2_off[STAGE_COUNT] = { ${shipOps2Off.join(',')} };`,
  `static const unsigned int ship_ops2_len[STAGE_COUNT] = { ${SHIPS.map((s) => (s.ops2 || emptyops).length).join(',')} };`,
  `static const unsigned char ship_kind[STAGE_COUNT]   = { ${SHIPS.map((s) => s.kind).join(',')} };`,
  `static const unsigned char ship_hull[STAGE_COUNT]   = { ${SHIPS.map((s) => s.hull).join(',')} };`,
  `static const unsigned char ship_bowcnt[STAGE_COUNT] = { ${SHIPS.map((s) => s.bowCnt).join(',')} };`,
  `static const unsigned int ship_bowyb[STAGE_COUNT]   = { ${SHIPS.map((s) => s.bowYb).join(',')} };`,
  `static const unsigned char ship_aagtbl[STAGE_COUNT] = { ${SHIPS.map((s) => s.aagTbl).join(',')} };`,
  `static const unsigned char ship_aagp[STAGE_COUNT][4] = { ${SHIPS.map((s) => `{${s.aagP.join(',')}}`).join(', ')} };`,
  '/* --- 撃破エンプレの炎上火球(丸, 2コマ×3サイズ=6枚。box×box/2 byte, 色0=透明)。 --- */',
  `#define FB_COUNT ${fbBlobs.length}`,          /* 6 = 3サイズ×2コマ, 順[s0f0,s0f1,s1f0,...] */
  `#define FB_RAM_MAX ${Math.max(...fbBlobs.map((b) => b.length))}`,
  `static const unsigned int fb_off[FB_COUNT] = { ${fbOff.join(',')} };`,
  '/* --- 撃破!! パネル(1bpp)。常駐節約のためデータバンクへ。results_and_fanfare が data_read して blit。 --- */',
  `#define PANEL_W ${panelW}`,
  `#define PANEL_WB ${panelWB}`,
  `#define PANEL_H ${panelH}`,
  `#define PANEL_LEN ${panelBlob.length}`,
  `static const unsigned int panel_off = ${panelOff};`,
  '/* --- 面別ドラム(スタイル1-3)。各21B=[pat16,v0(4),tempo]。bgm_play が p[8]=style で当該21BをRAMへ。 --- */',
  `#define DRUM_STYLE_BYTES 21`,
  `#define DRUM_STYLE_STRIDE 32`,
  `static const unsigned int drum_off = ${drumOff};`,
  '/* --- 面名(開始カード)/撃沈メッセージ(結果画面)。各16Bスロット。stage_build で当該面をRAMへ。 --- */',
  `static const unsigned int stagename_off = ${strOff};`,
  `static const unsigned int sunk_off = ${strOff + 5 * 16};`,
  `static const unsigned int fighter_ctab_off = ${fctabOff};`,
  `static const unsigned int gun_off = ${gunOff};`,   /* 面別 [x0-3(u8),y0-3(u16)] = 12B */
  '/* --- 開始カードの事前ベイク艦画像(64x48=48行x32byte)。bank4(旧demo跡)に5艦連結(assets/cards.bin)。 --- */',
  '#define SHIP_CARD_BANK 4',
  '#define SHIP_CARD_LEN 1536',
  `static const unsigned int ship_card_off[STAGE_COUNT] = { ${SHIPS.map((_, i) => i * 1536).join(',')} };`,
  '#endif /* ASSETS_DATA_H */', '',
];
writeFileSync(hdrOut, h.join('\n'));
console.log(`assets: ${bin.length}B (bank${BGM_BANK}), ${TRACKS.length} tracks + ${SHIPS.length} ships → ${binOut}, ${hdrOut}`);
