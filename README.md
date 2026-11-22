# Lilium 🪷

[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)
[![Platform](https://img.shields.io/badge/Platform-SHARP%20Brain%20(Windows%20CE%206.0)-blue.svg)]()
[![Base](https://img.shields.io/badge/Base-Upsilon%20%2F%20NumWorks%20Epsilon-orange.svg)](https://github.com/UpsilonNumworks/Upsilon)

**Lilium** は、SHARP 製カラー電子辞書「**Brain**」（Windows Embedded CE 6.0 / ARM926EJ-S）向けに開発された、軽量統合アプリケーションスイートです。

NumWorks / [Upsilon](https://github.com/UpsilonNumworks/Upsilon)（[Epsilon](https://github.com/numworks/epsilon)）ベースのグラフ関数・経理計算対応電卓を中核に、4×3 グリッドの専用ランチャー、統合設定アプリ、常駐型日本語 IME（Mozc OSS 辞書ベース）、Markdown エディタ、時間割、ファイラ、タスクマネージャ、ライフゲーム、サイレント版 DOOM を 1 つの統一デザイン（`#4fbfaf` ティールブルー × フラット UI × 美咲フォント）で提供します。

---

## 📦 スイート構成 (Applications)

ランチャーから各アプリや `apps/` 内の外部 Brain アプリを起動できます。

| アプリ | 実行ファイル | 主な機能 |
|---|---|---|
| **Lilium Launcher** | `AppMain_.exe` | 4×3 Launchpad 風アイコン一覧、アプリ並び替え、外部フォルダアプリ（`AppMain.cfg` / `AppMain.exe` / アイコン抽出）認識・起動、多段階ローダー（`ceOpener` 等）追跡、辞書解放確認＆自動再起動 |
| **Lilium Calc** | `apps/LiliumCalc.exe` | NumWorks / Upsilon ベースのグラフ関数電卓 + 統合経理電卓モード（税込/税抜・粗利/原価/売価・GT・日数/定数計算等）、右側日本語解説バー、状態自動保存（`brain_state.bin`） |
| **Lilium Editor** | `apps/LiliumEditor.exe` | Markdown エディタ＆ビューワ（見出し・箇条書き・チェックボックス・コード・引用・表のプレビュー、Lilium IME による直接日本語入力、クイック記号入力） |
| **Lilium TimeTable** | `apps/LiliumTimeTable.exe` | 高校 45/50 分・大学 90/100 分プリセット対応の時間割管理、対開講設定、曜日表示・配色カスタマイズ、終了時刻・休み時間の後続コマ自動追従、`.ttb` ファイル管理 |
| **Lilium Filer** | `apps/LiliumFiler.exe` | システム重要領域の削除・新規書込保護付きファイラ、テキスト/HEX ビューワ、アイコンツールバー、Lilium IME 対応ファイル名変更 |
| **Lilium TaskMgr** | `apps/LiliumTaskMgr.exe` | プロセス一覧・ウィンドウ一覧・RAM 推移グラフ、Brain 内蔵辞書シェルの安全なメモリ解放・再起動 |
| **Lilium Clock** | `apps/LiliumClock.exe` | デジタル時計、ストップウォッチ、カウントダウンタイマー |
| **Lilium Life** | `apps/LiliumLife.exe` | Conway's Life / Brian's Brain / Langton's Ant 等を内蔵したセルオートマトン＆スクリーンセーバー |
| **Lilium Setting** | `apps/LiliumSetting.exe` | Brain 本体の画面輝度・自動電源OFF・開閉センサー・ランチャー並び順＆表示切替・各アプリ初期値・ログ一括管理（`lilium_settings.cfg`）・バージョン情報 |
| **Lilium DOOM** | `apps/doom.exe` + `apps/freedoom1.zwad` | doomgeneric + 音声省略・Episode 1 軽量化 ZWAD によるサイレント DOOM |
| **Lilium IME** | `apps/LiliumIME.exe` + `apps/lilium_ime.dic` | 常駐型ローマ字漢字変換デーモン。非圧縮インメモリバイナリ辞書（LIME v2、約 920 KB / 51,000 ペア）により即時変換を実現し、共有メモリ IPC および外部アプリ用フローティングオーバーレイを提供 |

---

## 🌟 主な特徴 (Features)

- **Brain 800×480 解像度・差分矩形転送設計**:
  - テキストエディタ・IME モーダル・各種画面で変更行・変更領域のみの差分描画を行い、ARM9 + バス帯域の制約下でも軽快に応答します。
  - タスクバー（`HHTaskBar`）を起動時に自動非表示化し、外部アプリ（`ceOpener` 等）起動時は安全に復帰・引き渡しを行います。
- **判読性の高い「美咲フォント」採用**:
  - 8 ドットビットマップフォント「[美咲フォント](https://littlelimit.net/misaki.htm)」を同梱し、仮名文字領域（`0x3000..0x30FF`）を含め O(1) ルックアップテーブルで高速描画します。
- **統一キーボード操作**:
  - `[F2]`（英語/国語キー相当）で各アプリのタブ切替や入力モード（`あ (IME)` / `abc` / `ABC` / `12x`）を切り替えられます。
  - 電卓画面では右側 160px にキーガイド・設定状態を日本語表示します。

---

## 📱 対応機種 (Target Devices)

- **OS**: Microsoft Windows Embedded CE 6.0 Core
- **CPU**: ARM926EJ-S / i.MX28 (Armv5TEJ)
- **対象シリーズ**: PW-SH3 / SH4 / SH5 / SH6 / SH7 / H7700 / PW-SB シリーズ / PW-SA シリーズ など 800×480 液晶搭載 SHARP Brain 各機種

---

## 🚀 インストール方法 (How to Install)

ビルドされた `output/release/simulator/brain/Lilium` フォルダ（またはリリースの `Lilium` フォルダ）を microSD カードのアプリ配置先（例: `アプリ/Lilium`）にコピーし、Brain 本体の「追加アプリ」から起動します。

```text
Lilium/
├── AppMain.exe            # Brain 起動ローダー (PW-SB4 等に対応)
├── AppMain.cfg            # ローダー設定
├── AppMain_.exe           # Lilium Launcher 本体 (WinCE ARM)
├── index.din              # Brain アプリアイコン・定義ファイル
├── font.ttf               # 同梱フォント (美咲ゴシック)
├── misaki_gothic.ttf
├── scripts/
└── apps/
    ├── LiliumCalc.exe
    ├── LiliumEditor.exe
    ├── LiliumTimeTable.exe
    ├── LiliumFiler.exe
    ├── LiliumTaskMgr.exe
    ├── LiliumClock.exe
    ├── LiliumLife.exe
    ├── LiliumSetting.exe
    ├── LiliumIME.exe
    ├── lilium_ime.dic     # Mozc OSS 由来 LIME v2 非圧縮辞書 (約 920 KB)
    ├── doom.exe
    └── freedoom1.zwad     # Freedoom Episode 1 圧縮 ZWAD
```

---

## ⌨️ 操作方法 (Controls)

### 電卓 (Lilium Calc)
| キー | 動作 |
|---|---|
| `[F2]` | 入力モード切替: `12x`（数字） / `abc`（小文字） / `ABC`（大文字） |
| `[F1]` / `[F9]` | Toolbox（関数一覧） / Var（変数一覧） |
| `[F10 / HOME]` | 電卓ホーム画面へ戻る（メニュー選択画面から抜ける） |
| `[PgUp / PgDn]` | `12x`: `(` / `)`、その他・グラフ: ズームイン / ズームアウト |
| `[F5 / F7]` | 終了確認ダイアログ（ランチャーへ戻る） |

### 日本語入力 (Lilium IME / Editor / モーダル)
| キー | 動作 |
|---|---|
| `[F2]` | 入力モード切替: `あ (IME)` / `abc` / `ABC` / `12x` |
| `[Space]` | 漢字変換・次候補 |
| `[1-8]` / `[←→]` | 候補確定 / 候補移動 |
| `[F7]` | カタカナ変換・確定 |
| `[Enter]` | 未変換文字・候補の確定（Editor では確定後に改行入力） |
| `[Ctrl+Space]` / `[Alt+Space]` / `[F11]` | 外部アプリ実行中の Lilium IME オーバーレイ呼出（WinCE 環境では `[F11]` / `[変換]` 等にも対応） |

---

## 🛠️ ビルド方法 (How to Build)

### 1. PC ホスト検証版 (Windows MSYS2 MinGW64)
```bash
./build_brain.sh --host
```
- 出力先: `output/release/simulator/brain-host/Lilium/`
- ランチャー起動: `output/release/simulator/brain-host/Lilium/AppMain.exe`

### 2. SHARP Brain 実機版 (Windows CE 6.0 ARM / CeGCC)
```bash
./build_brain.sh
```
- クロスコンパイラ: CeGCC (`arm-mingw32ce-g++`) + Cygwin ランタイム
- 出力先: `output/release/simulator/brain/Lilium/`

### 3. IME 辞書の再生成（任意）
```bash
python suite/ime/build_mozc_dict.py
```
- Google Mozc OSS 辞書データから美咲フォント対応語彙を抽出し、`suite/ime/lilium_ime.dic`（LIME v2 形式）を生成します。

---

## 📜 ライセンス・謝辞 (License & Attributions)

- **Lilium Calc / Epsilon / Upsilon 派生部分**: [CC BY-NC-SA 4.0](LICENSE.md)（NumWorks Epsilon / Upsilon / KhiCAS ベース）
- **Lilium Suite 独自コンポーネント**（Launcher, Filer, TaskMgr, Clock, Life, TimeTable, Editor, IME クライアント/デーモン）: クリーンルーム新規実装
- **Lilium DOOM (`doomgeneric` 移植)**: GNU GPL v2（原著: ozkl / doomgeneric・id Software Doom engine）
- **Freedoom Phase 1 (`freedoom1.zwad` 元データ)**: 3-Clause BSD License ([Freedoom Project](https://freedoom.github.io/))
- **美咲フォント**: フリーフォント（[Little Limit](https://littlelimit.net/misaki.htm)）
- **日本語変換辞書元データ**: Google Mozc Open Source Dictionary (`dictionary_oss`, BSD-3-Clause)
