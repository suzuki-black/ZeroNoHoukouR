# ZeroNoHoukouR — turboR spectacle spin-off (WIP)

An experimental spin-off of
**[BattleshipProtoR](https://github.com/suzuki-black/BattleshipProtoR)** that reuses the same
engine and assets, but flips the goal. Instead of a clean, balanced baseline, this project asks one
question: **"how far can the MSX turboR (R800) be pushed?"** — software sprite pseudo-rotation,
full-screen effects, and other **CPU-heavy / VDP-light** spectacle. **Game balance is explicitly
secondary**; the point is to make the turboR show off.

- **Derived from** BattleshipProtoR (v0.2.0 source), MIT. Same toolchain (SDCC · Z80/R800 asm ·
  ASCII8 mega-ROM) and the RAM-execution engine (§4-3), 8-direction procedural sprites, entity /
  scroll / sound systems — all reused as the foundation.
- **Design seed:** [docs/次版設計メモ_CPU重VDP軽の演出と1943ギミック.md](docs/次版設計メモ_CPU重VDP軽の演出と1943ギミック.md)
  — the plan for CPU-heavy/VDP-light effects and "1943-feel" gimmicks/power-ups.
- **Reference docs** carried over: [ARCHITECTURE](docs/ARCHITECTURE.md) ·
  [性能と高速化](docs/性能と高速化.md) · [苦労と教訓](docs/苦労と教訓.md) ·
  [アルゴリズム解説](docs/アルゴリズム解説.md) · [仕様書](docs/仕様書.md).
- **Workflow:** correctness is developed/verified on **openMSX (MSX2+ · C-BIOS · Z80)**; **turboR
  performance is verified on WebMSX turboR / real hardware** (openMSX has no turboR firmware). The
  design stays in the "heavy compute, light VRAM" regime where this split is most trustworthy.

> Status: **early WIP.** Identity, README, and versioning will be developed as the project takes shape.

---

## 日本語

**[BattleshipProtoR](https://github.com/suzuki-black/BattleshipProtoR)** の実験的スピンオフ。同じ
エンジンと素材を再利用しつつ、目的を反転させる。整った基盤ではなく、**「MSX turboR（R800）は
どこまで詰め込めるか？」**を主題に、**ソフトによるスプライト疑似回転や全画面演出など、CPU 重・
VDP 軽のド派手な見せ場**を追う。**ゲームバランスは明確に二の次**で、とにかく turboR に凄いと
言わせることを狙う。

- **由来:** BattleshipProtoR（v0.2.0 のソース）派生・MIT。ツールチェーン（SDCC・Z80/R800 asm・
  ASCII8 メガROM）と §4-3 の RAM 実行エンジン、8方向手続き生成、エンティティ/スクロール/音は
  そのまま土台として再利用。
- **設計の種:** [docs/次版設計メモ_CPU重VDP軽の演出と1943ギミック.md](docs/次版設計メモ_CPU重VDP軽の演出と1943ギミック.md)。
- **開発ワークフロー:** 正しさは **openMSX（MSX2+・C-BIOS・Z80）**で開発・検証。**turboR の性能は
  WebMSX turboR／実機**で確認（openMSX に turboR ファーム無し）。設計は「重い計算・軽い出力」に
  留め、この分業が最も信頼できる領域で進める。

> 状態: **初期 WIP。** 名称・README・版採番はプロジェクトの形が見えてから詰める。
