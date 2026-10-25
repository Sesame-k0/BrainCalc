# Lilium 🪷

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)
[![Platform](https://img.shields.io/badge/Platform-SHARP%20Brain%20(Windows%20CE%206.0)-blue.svg)]()
[![Base](https://img.shields.io/badge/Base-Upsilon%20%2F%20NumWorks%20Epsilon-orange.svg)](https://github.com/UpsilonNumworks/Upsilon)

**Lilium**（リリウム）は、フランス NumWorks 社の高性能グラフ関数電卓 OS（[Upsilon](https://github.com/UpsilonNumworks/Upsilon) / [Epsilon](https://github.com/numworks/epsilon)）をベースに、SHARP 製カラー電子辞書「**Brain**」（Windows Embedded CE 6.0 搭載モデル）向けに最適化・移植したネイティブアプリケーションです。

洗練されたテーマカラー（`#4fbfaf` ティールブルー）の UI、高校・大学レベルの高等数学計算、2D 関数グラフ描画、数値計算、統計、確率分布、方程式ソルバーなどの高精度な電卓機能を、Brain のキーボードと大画面カラー液晶で快適に利用できます。

---

## 🌟 主な特徴 (Features)

- **Brain の画面仕様に最適化した 800×480 解像度**:
  - 右端 54px のフレキシブルタッチキー領域を除外した有効領域（800×480）で描画。
  - 左側（640×480）: 電卓画面（320×240 を自前の整数 2 倍最近傍補間で高速展開）。
  - 右側（160×480）: NumWorks デザインと調和したライトテーマの入力支援サイドバー。
- **洗練されたテーマカラー (`#4fbfaf`) & スッキリしたタイトルバー**:
  - タイトルバーおよびサイドバーのアクセントカラーにティールブルー（`#4fbfaf`）を採用。
  - ホーム画面のタイトルバーには「HOME」とシンプルに表示。
  - 余分な時計表示を排し、バッテリー残量と Shift / Alpha ロック状態のみをスマートに表示。
- **状態自動保存・復元（Auto Save & Restore）**:
  - アプリ終了時に、定義した関数・変数・計算履歴および電卓設定（角度単位や複素数モードなど）をバイナリ（`brain_state.bin`）として自動保存。
  - 次回起動時に自動復元されるため、電源を切っても作業環境がそのまま維持されます。
- **クッキリと判読性の高い「美咲ゴシック」フォント採用**:
  - 8ドットフォントの名作「[美咲ゴシック](https://littlelimit.net/misaki.htm)」をアプリ内に同梱。
  - 原寸（1倍: 8px）および整数倍（2倍: 16px）スケーリングにより、文字の滲みや偽色を抑え、Brain 実機と PC シミュレータでシャープな日本語・数学記号を表示。
- **実機キーボードに合わせた 3 モード切替キーマッピング**:
  - `[Brain English / 国語 (F2)]` キーで **数字モード (12x)** → **通常モード (abc)** → **大文字モード (ABC)** をトグル切替。
  - 数字モード時は QWERTY 配列が直感的なテンキー・四則演算・累乗・虚数単位 $i$・定数ショートカットに変化。
  - 銀色キー対応: `F1` (関数ツールボックス), `F9` (変数メニュー), `F10 / HOME` (ホーム画面), `PgUp / PgDn` (かっこ `(` / `)` またはグラフ拡大・縮小)。
- **実機バッテリー残量のリアルタイム連動**:
  - Windows CE の `GetSystemPowerStatusEx2()` API と連動し、Brain 本体のバッテリー残量を電卓画面上部のタイトルバーに正確に表示。
- **軽量・高速なレスポンス設計**:
  - `pushRect` 差分矩形更新により、画面全体の無駄な再描画と転送バス帯域を大幅削減。
  - ノンブロッキングなメッセージ処理（`MsgWaitForMultipleObjects`）により、キー入力の遅延や取りこぼしを徹底解消。
  - 起動時に OS タスクバー（`HHTaskBar`）を自動非表示化し、完全な全画面最前面（`HWND_TOPMOST`）で実行。終了時に安全に復帰。

---

## 📱 対応機種 (Target Devices)

- **OS**: Microsoft Windows Embedded CE 6.0 Core
- **CPU**: ARM926EJ-S / i.MX28 (Armv5TEJ)
- **対象シリーズ**:
  - 高校生・中学生・大学生・ビジネス向け SHARP Brain 各機種
  - PW-SH3, SH4, SH5, SH6, SH7
  - PW-H7700, PW-SBシリーズ, PW-SAシリーズ 等

---

## 🚀 インストール方法 (How to Install)

1. [Releases](../../releases) から最新の `Lilium.zip` をダウンロードして解凍します。
2. 解凍して生成された `Lilium` フォルダ（以下のファイルが含まれます）を、microSD カードのアプリ配置先（例: `アプリ` フォルダ）にそのままコピーします。
   ```text
   Lilium/
   ├── AppMain.exe          # Brain起動用ローダー (188KB, PW-SB4等に必須)
   ├── AppMain.cfg          # ローダー設定ファイル (8バイト)
   ├── AppMain_.exe         # 電卓本体バイナリ (約2.2MB, WinCE ARM)
   ├── index.din            # アプリアイコン・定義ファイル
   ├── font.ttf             # 同梱日本語フォント (美咲ゴシック)
   └── brain_state.bin      # (自動生成) 状態保存ファイル
   ```
3. microSD カードを Brain に挿入し、ホームメニューの「追加アプリ」等から **Lilium** を起動してください。

---

## ⌨️ 操作方法・キーボード対応表

詳細な仕様は [KEY-MAPPING.md](file:///c:/K0Project/Program/BrainCalc/資料/KEY-MAPPING.md) を参照してください。

### モード切替
- **`[F2]`（Brain English / 国語キー）**: 「**数字モード (12x)**」→「**通常モード (abc)**」→「**大文字モード (ABC)**」をトグル切替

### 基本キー
| キー | 動作 |
|---|---|
| `[決定 / Enter]` | 数字モード時は `EXE`（計算実行）、通常・大文字モード時は `OK` |
| `[戻る / Esc]` | 1 つ前の画面に戻る / 計算キャンセル |
| `[削除 / BackSpace]` | 1 文字削除 |
| `[スペース]` | 空白入力 |
| `[矢印キー (▲▼◀▶)]` | カーソル移動 / グラフのスクロール |
| `[>>↑ / PageUp]` | 数字モード: `(` / 通常モード: グラフ拡大 |
| `[>>↓ / PageDown]` | 数字モード: `)` / 通常モード: グラフ縮小 |

### 銀色キー・機能キー
| キー | 割り当て |
|---|---|
| `[調べる / F1]` | **Toolbox**（数学関数・コマンド一覧パレット） |
| `[ツール / F9]` | **Var**（変数ストア・一覧メニュー） |
| `[HOME / F10]` | **Home**（電卓ホーム画面へ戻る） |
| `[F5 / F7]` | **終了確認ダイアログ** 表示 |

### 数字モード（12x）時の配列
- **上段 (Q〜P)**: `1`, `2`, `3`, `4`, `5`, `6`, `7`, `8`, `9`, `0`
- **中段 (A〜L)**: `+`, `-`, `*`, `/`, `=`, `x^n`, `ℯ`, `ANS`, `STO`
- **下段 (Z〜ー)**: `√`, `x,n,t`, `i` (虚数), `π`, `log`, `,`, `.`, `-`

---

## 🛠️ ビルド方法 (How to Build)

### 開発環境
- **OS**: Windows 10 / 11 (MSYS2 MinGW64 環境)
- **クロスコンパイラ**: [CeGCC](https://sourceforge.net/projects/cegcc/) (`arm-mingw32ce-g++` 9.3.0) + Cygwin64 ランタイム

### 1. PC ホスト検証版のビルド (Windows 上で動作確認)
```bash
./build_brain.sh --host
```
- 出力バイナリ: `output/release/simulator/brain-host/AppMain.exe`
- ダブルクリックで PC 上で動作確認できます。

### 2. SHARP Brain 実機向けバイナリのビルド (Windows CE 6.0 ARM)
```bash
./build_brain.sh
```
- 出力フォルダ: `output/release/simulator/brain/Lilium/`
- このフォルダを microSD カードにコピーして実機で起動します。

---

## 📜 ライセンス (License)

- **Lilium**: CC BY-NC-SA 4.0
- **NumWorks Epsilon / Upsilon**: CC BY-NC-SA 4.0 (NumWorks & Upsilon Project)
- **美咲ゴシック**: フリーフォント ([Little Limit](https://littlelimit.net/misaki.htm))
