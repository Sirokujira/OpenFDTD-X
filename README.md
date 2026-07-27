# OpenFDTD-X — Qt6 マルチドメインGUI

**電磁 / 光 / 室内音響 / 水中音響** の各ドメインを統一 UI で扱う Qt6 Widgets
デスクトップアプリ。`claude.ai/design` で作成された HTML/CSS/JS モックを実コードへ
起こしたもの。

このリポジトリは **画面 (GUI) のみ** を持つ。FDTD/RCWA/BPM の **処理ロジック
(ソルバーカーネル) の実体は別リポジトリ** にあり、本アプリはそれらを subprocess
として起動するだけでソースには依存しない (疎結合):

| 役割 | リポジトリ | 起動バイナリ |
|---|---|---|
| 電磁・光 FDTD | [Sirokujira/OpenFDTD](https://github.com/Sirokujira/OpenFDTD) | `ofd` / `ofd_mpi` / `ofd_cuda` + `ofd_post` |
| 周期構造 RCWA | [Sirokujira/OpenRCWA](https://github.com/Sirokujira/OpenRCWA) | `orcwa` / `orcwa_mpi` / `orcwa_cuda` + `orcwa_post` |
| 導波路 BPM | [Sirokujira/OpenBPM](https://github.com/Sirokujira/OpenBPM) | `obpm` / `obpm_mpi` / `obpm_cuda` + `obpm_post` |

加えて tidy3d (Flexcompute 社の光FDTDクラウド) への Python スクリプト書き出しにも
対応する。GUI が出力する `.ofd` は本家カーネルがそのまま読める完全互換形式。

## ビルド

### 必要環境
- Qt 6.2+ (Widgets モジュールのみ必須)
- CMake 3.21+
- C++17 コンパイラ

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### オプション
| オプション | 既定 | 説明 |
|---|---|---|
| `-DUSE_HDF5=ON`   | OFF | HDF5 時系列/プロジェクト出力 (`io/H5Writer`) |
| `-DUSE_LIBIGL=ON` | OFF | より高精度な共形/winding-number ボクセル化 (`docs/libigl-integration.md`)。標準ビルドでも `io/Voxelizer` の階段近似ボクセル化は有効 |
| `-DBUILD_TESTS=ON`| ON  | `.ofd` ラウンドトリップ + ボクセル化 自己テスト (`ofdx_selftest`) |

### 実行
本体 `openfdtd_x` と、水中音響の分離アプリ `openuwa` の 2 つが生成される
(共有 GUI は `ofdx_gui` 静的ライブラリとして 1 度だけコンパイルされる)。

```bash
# 既存プロジェクトを開く
./build/openfdtd_x tests/data/dipole.ofd

# OpenUWA (水中音響 分離アプリ) — ドメイン切替を持たず水中固定。
# 海洋環境 / 伝搬解析 (SSP/Bellhop/PE) / 音源・指向性 / H5アニメ /
# ツール連携 の 5 タブを本体と同じ実装で再利用する。
./build/openuwa tests/data/dipole.ofd

# 起動ドメイン・言語を指定 (ja|en|both)
./build/openfdtd_x --domain optical --lang both

# 処理ロジック (ソルバーカーネル) の場所 (なければ PATH を探索)
export OPENFDTD_HOME=/path/to/OpenFDTD   # ofd, ofd_mpi, ofd_cuda ...
export OPENRCWA_HOME=/path/to/OpenRCWA   # orcwa, orcwa_post ...
export OPENBPM_HOME=/path/to/OpenBPM     # obpm, obpm_post ...
```

### 自己テスト
`tests/data` に同梱した OpenFDTD サンプル `.ofd` をロード → シリアライズ → 再パースし、
構造が完全一致することを確認する (本家フォーマット互換性の保証)。
```bash
./build/ofdx_selftest                 # 24 files + voxelizer, 0 failures
```

## アーキテクチャ

データモデル中心。タブはすべて `Project` の View であり、編集すると
`Project::changed()` / `loaded()` シグナルでビューポート・ツリー・ステータスバーが
自動更新される。

```
src/
├── main.cpp                  本体エントリ (CLI: file / --lang / --domain / --left-tab /
│                             --view-style / --ui-style / --ui-theme / --ui-density / --screenshot)
├── main_uwa.cpp              OpenUWA エントリ (水中音響 分離アプリ)
├── MainWindow.{h,cpp}        メインシェル (メニュー/ツールバー/ドック/ステータスバー)
├── UnderwaterWindow.{h,cpp}  OpenUWA シェル (ドメイン切替なし・水中固定の5タブ)
├── DomainBar.{h,cpp}         電磁/光/室内音響/水中 切替タブ
├── TabNavigator.{h,cpp}      左のカテゴリ付き縦ナビ (標準/エキスパート × ドメインで絞込)
├── CenterPane.{h,cpp}        中央ペイン (3Dシーン/2D断面/結果プロット/メッシュ表示 + ギズモ)
├── RightDock.{h,cpp}         プロジェクトツリー / 実行ログ / プロパティ の3セグメント
├── I18n.{h,cpp}              日英バイリンガル翻訳テーブル (共通キー + 各タブの reg 登録)
├── Theme.{h,cpp}             styles.css の CSS 変数を QSS へ生成
│                             (Classic/Modern/Scientific × ライト/ダーク × 密度3段)
│
├── core/                     データモデル (値オブジェクト + Project)
│   ├── Project.{h,cpp}         全体モデル + CFL/メモリ推定 + 永続化
│   ├── Domain.h               物理ドメイン enum + アクセントカラー
│   ├── Material.h             material 行 (通常/分散性) + load 行
│   ├── Geometry.h             geometry 行 (本家 shape コード)
│   ├── Source.h               feed / planewave / point 行
│   ├── MeshAxis.h             xmesh/ymesh/zmesh (座標+分割)
│   ├── PostOpts.h             plot* ポスト処理キー一式
│   ├── GlassCatalog.{h,cpp}   光学ガラスDB + Sellmeier + AGF/CSV取込
│   ├── RoomAcoustics.{h,cpp}  Sabine/Eyring + Barron統計 + 鏡像法 + NC評価
│   └── OperaHalls.h           世界のコンサートホール5 + 日本のオペラ対応ホール23 の実測値
│
├── io/
│   ├── OfdIO.{h,cpp}          .ofd テキスト読み書き (本家完全互換) + .ofdx JSON サイドカー
│   ├── Touchstone.{h,cpp}     S パラメータ .s1p/.s2p 出力
│   ├── Tidy3dExporter.{h,cpp} 光プロジェクト → tidy3d Python スクリプト生成
│   ├── H5Writer.{h,cpp}       HDF5 出力 (USE_HDF5 時)
│   ├── StlImporter.{h,cpp}    STL (バイナリ/ASCII) 取込
│   ├── Voxelizer.{h,cpp}      STL→Yee格子 階段近似ボクセル化 (libigl非依存)
│   ├── ActivationCurve.{h,cpp} ONN 光活性化カーブ (obpm 出力の取込)
│   └── EvReader.h             ev2/ev3 ネイティブパーサ骨格
│
├── kernel/
│   ├── Runner.{h,cpp}         QProcess で ofd/orcwa/obpm (+_mpi/_cuda) と *_post を起動
│   └── AcousticRunner.{h,cpp} 外部音響ソルバーの起動と出力契約の検証
│
├── widgets/
│   ├── SectionBox.{h,cpp}     見出し付きグループボックス
│   ├── MiniPlot.{h,cpp}       共用XYミニプロット (分散曲線/RT60/NC/エコーグラム)
│   ├── UnitNav.{h,cpp}        ユニット番号ナビ (◀ n/総数 ▶)
│   ├── Viewport3D.{h,cpp}     QPainter 製 3Dビュー (OpenGL不要)
│   │                          Wire/Solid/+Field/+Rays の4スタイル
│   ├── FieldHeatmap.{h,cpp}   2D断面の界分布 (jet カラーマップ + カラーバー)
│   ├── MeshPreview.{h,cpp}    実 xmesh/ymesh/zmesh の平面グリッド描画
│   ├── PlotPanel.{h,cpp}      波源波形プレビュー + 収束履歴プロット
│   ├── LogConsole.{h,cpp}     等幅ログペイン
│   └── EvViewer.{h,cpp}       ev2/ev3 図形を3戦略 (HTML/外部exe/ネイティブ) で表示
│
├── dialogs/                  モーダル (app.jsx / ansys-workflow.jsx 由来)
│   ├── AppGalleryDialog       応用ギャラリー (5ドメイン × テンプレート一覧)
│   ├── ResourceDialog         計算リソース設定
│   ├── GettingStartedDialog   はじめに (ガイドツアー)
│   ├── CloudDialog            tidy3d クラウド送信
│   └── RunDialog              計算コンソール
│
└── tabs/                     左ナビ各タブ (51 件)。カテゴリ = TabNavigator の分類。
    │                         ドメイン注記が無いものは全ドメイン共通。
    ├── セットアップ
    │   ├── GeometryTab      ① 形状 (geometry + STL取込 + CADパイプライン14節)
    │   ├── MaterialTab      ② 物性値 (material/load、光ドメインでは n/k 表示)
    │   ├── SolverRegionTab  ③ ソルバ領域
    │   ├── SourceTab        ④ 波源・観測点 (feed/planewave/point)
    │   ├── MonitorsTab      ⑤ モニター
    │   ├── GeneralTab       全般 (solver/abc/pbc/frequency)
    │   ├── MeshTab          メッシュ詳細 (xmesh/ymesh/zmesh + 統計)
    │   └── PerFaceBCTab     境界面詳細
    ├── ライブラリ
    │   ├── ComponentsTab        コンポーネント
    │   ├── MaterialExplorerTab  🔬 材料Explorer            ※電磁/光
    │   ├── GlassCatalogTab      🔷 ガラスカタログ           ※光
    │   ├── LensEditorTab        Lens (Zemax風 面テーブル)   ※光
    │   ├── LayoutGDSTab         GDS                        ※光
    │   ├── SchematicTab         Schematic                  ※光
    │   ├── PhotonicsSolversTab  FDTD/RCWA/BPM/FMM          ※光
    │   ├── ThinFilmTab          🌈 多層膜コート             ※光
    │   ├── IlluminationTab      💡 照明/測色                ※光
    │   ├── DisplayOpticsTab     👓 ディスプレイAR/VR        ※光
    │   ├── AcousticSourceTab    🎤 音源/WAV/指向性          ※音響/水中
    │   ├── OceanEnvironmentTab  🌏 海洋環境                 ※水中
    │   ├── RoomAcousticsTab     🏛 ホール解析 (28ホールプリセット + 10サブタブ) ※音響
    │   ├── SoundproofTab        🔇 防音設計                 ※音響
    │   ├── OutdoorNoiseTab      🌳 屋外騒音                 ※音響
    │   ├── CabinAcousticsTab    🚗 車内NVH                  ※音響
    │   └── UltrasoundTab        🩺 超音波                   ※音響
    ├── 解析
    │   ├── FamilySolverTab   🌳 姉妹ソルバ
    │   ├── SolverSelectorTab ソルバ詳細 (CST風カード選択)
    │   ├── VerificationTab   🔍 検証 (メッシュ収束/PML品質)
    │   ├── OptimizeTab       最適化・スイープ
    │   ├── ToleranceTab      ばらつき
    │   ├── ScriptsTab        スクリプト
    │   ├── MultiphysicsTab   マルチフィジクス
    │   └── Tidy3dTab         ☁ tidy3d クラウド連携          ※光
    ├── ポスト
    │   ├── AnalysisGroupsTab   解析グループ
    │   ├── DatasetsTab         Datasets
    │   ├── H5ViewerTab         🎬 H5アニメ
    │   ├── InteropTab          🔗 ツール連携
    │   ├── AntennaCharTab      📡 アンテナ特性              ※電磁
    │   ├── TransmissionLineTab 🔌 伝送線路                  ※電磁
    │   ├── ScatteringTab       🎯 散乱/RCS                  ※電磁
    │   ├── CircuitSolversTab   ⚡ 回路 PEEC/FEM             ※電磁
    │   ├── EmcTab              📜 EMC/EMI (CISPR/FCC/IEC)   ※電磁
    │   ├── SarTab              🧬 SAR/生体 (IEC 62704)      ※電磁
    │   ├── ChannelTab          📶 電波伝搬 (5G/6G チャネル) ※電磁
    │   ├── Post1Tab            ポスト処理(1) 周波数特性
    │   └── Post2Tab            ポスト処理(2) 遠方界・近傍界
    └── ドメイン別
        ├── OpticalTab       光解析 (FDTD/RCWA/BPM/FMM + モード別節)  ※光
        ├── AcousticTab      音響解析 (RT60/C80/STI/可聴化)          ※音響
        ├── RirAnalysisTab   🎤 実測RIR分析 (ISO 3382-1)             ※音響
        ├── VocalAnalysisTab 🎶 歌声分析 (YIN)                       ※音響
        ├── AuralizationTab  🔊 可聴化                               ※音響
        └── UnderwaterTab    水中音響 (SSP/SOFAR/ソナー)             ※水中
```

`openuwa` は上記のうち OceanEnvironment / Underwater / AcousticSource /
H5Viewer / Interop の 5 タブを**同一クラスのまま**再利用する。共有 GUI は
`ofdx_gui` 静的ライブラリとして 1 度だけコンパイルされる。

## 設計判断

1. **本家 `.ofd` 形式に完全互換** — `OfdIO` は `sol/input_data.c` と
   `post/post_data.c` が解釈する全キーを 1:1 で読み書きする。GUI が
   モデル化しないキーは `Project::extraLines()` に保持し、保存時にそのまま
   書き戻すので手編集ファイルがラウンドトリップで壊れない。

2. **拡張ドメインは `.ofdx` (JSON) に分離** — 光・音響・水中・tidy3d の設定は
   `.ofd` と同じ basename のサイドカー JSON に保存。本家カーネルは無視するので
   **下位互換 100%**。

3. **既存カーネルを subprocess として再利用** — `Runner` が CPU/GPU × MPI の
   4種バイナリと姉妹ソルバー (FDTD=`ofd`, RCWA=`orcwa`, BPM=`obpm`) を
   `QProcess` で起動。OpenMP は `OMP_NUM_THREADS`、MPI は `mpiexec -n N`、
   GPU は `CUDA_VISIBLE_DEVICES`。solver→post の2段実行 (一括モード) にも対応。

4. **tidy3d は光FDTD専用のクラウドバックエンド** — 独立した物理ドメインではなく
   光ドメインのタブとして配置。クラウド送信ボタンは光ドメイン選択時のみ有効。

5. **ビューポートは OpenGL 非依存** — `Viewport3D` は `QPainter` の正射影
   ワイヤフレーム。ヘッドレス/リモート環境でも動作する。

6. **応用画面はローカル状態** — EMC/SAR/電波伝搬/多層膜/照明/AR-VR などの
   応用タブは `.ofd` に対応フィールドを持たないため、`Project.h` を拡張せず
   ウィジェットのローカル状態 (モック既定値) として保持する。
   **`.ofd` へ新しいキーや値を書き出すことはない**。
   例: 吸収境界の「Mur 2次」は本家フォーマットに無いので UI 選択肢としてのみ
   持ち、保存時は Mur 1次 (`abc=0`) として書き出す旨を画面に明示する。

7. **テーマは CSS 変数から QSS を生成** — `Theme` がモックの `styles.css` と
   同じカスケード順 (style → dark → density → ドメインaccent) でパレットを
   組み立て、QSS 文字列を生成する。静的な `.qss` は持たない。

8. **CI は 3 プラットフォーム** — Linux / macOS / Windows でビルド + `ofdx_selftest`
   + `ctest` + ヘッドレス GUI スモーク (スクリーンショット生成) を実行する。
   macOS は `MACOSX_BUNDLE` のため実体が `.app` 内にある点に注意
   (`build/openfdtd_x.app/Contents/MacOS/openfdtd_x`)。

## デザインリファレンス
元の HTML/CSS/JS モック (`claude.ai/design` プロジェクト「OpenFDTD対応」) と
各画面が 1:1 で対応する。モックには 2 つのエントリがあり、
`OpenFDTD-X.html` → `openfdtd_x`、`OpenUWA-Underwater.html` → `openuwa` に対応する。

画面を変更する前にモック側の更新有無を確認すること (モックは実装後も更新される)。
`docs/` に `.ofd` 処理パイプライン・ev2/ev3 形式・libigl 統合の設計メモ、
`docs/adr/` に音響コアの設計判断記録を収録。
