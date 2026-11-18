# ZeroNoHoukouR(真 零の咆哮)— プロジェクト固有の約束

## リリース時: バージョンを保持するファイル

バージョンを上げてリリースを切るときは、次のファイルの値をそろえる。

- `VERSION`
- `README.md`(英語・日本語の系譜の表にある `v0.x.y`)
- `CHANGELOG.md`
- **`web/zeronohoukou.rom`**(ブラウザ版が読む ROM。下を読む)

### `web/zeronohoukou.rom`

itch.io などのブラウザ版(`web/index.html`)は webmsx.org を iframe で呼び、webmsx.org は
この ROM を `raw.githubusercontent.com` の **`main`** から読む。**ここが古いと、ブラウザで遊んだ
人だけが前の版を遊ぶ。** ビルド生成物の `GAME.ROM` と違い、**git で追跡する**。

リリースを切るときは、タグを打つ前に必ず:

```sh
make web            # GAME.ROM で web/zeronohoukou.rom を置き換える
make release-check  # 食い違っていたら止まる
```

- **開発中の ROM を `web/` へ置かない。** push した瞬間に遊んでいる人全員がそれを掴む。
  計測用・実機確認前のビルドも置かない。
- 中身は製品の `GAME.ROM`。`WEBMSX.ROM`(`make webmsx`)ではない。webmsx.org の WebMSX は
  turboR の PCM を持たず、ゲームが起動時に PCM を切るので、製品の ROM でそのまま動く。
- **ROM をリポジトリに置く理由:** webmsx.org は別オリジンなので、ROM の置き場が CORS を
  返す必要がある。`raw.githubusercontent.com` は返すが、itch.io のファイル置き場も GitHub の
  リリース資産も返さない(実測)。
- **WebMSX(`wmsx.js`)を同梱しない。** MSX の BIOS が入っていて再配布の許諾が確認できない。
  `web/index.html` のクレジットと webmsx.org へのリンクを消さない(WebMSX の作者の条件)。
