# OpenFDTD-X — Claude Code プロジェクトメモリ

Qt6 Widgets 製マルチドメイン FDTD GUI (電磁 / 光 / 室内音響 / 水中音響)。
`claude.ai/design` の HTML/CSS/JS モックを実 C++ に起こしたもの。
**このリポジトリは GUI のみ** — ソルバーカーネル (ofd/orcwa/obpm) は別リポジトリで、
`kernel/Runner` が subprocess として起動する (疎結合)。

## ビルド & テスト (最初にこれが通ること)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ofdx_selftest        # .ofd ラウンドトリップ + voxelizer 自己テスト (0 failures 必須)
```

- 必要環境: Qt 6.2+ (Widgets のみ), CMake 3.21+, C++17。macOS は `brew install qt`。
- GUI スモーク (ヘッドレス):
  `QT_QPA_PLATFORM=offscreen ./build/openfdtd_x tests/data/dipole.ofd --screenshot /tmp/x.png`
  検証用 CLI: `--domain` `--left-tab` `--view-style` `--ui-style` `--ui-theme` `--ui-density`。
  いずれもセッション限りで `QSettings` は書き換えない。
  **macOS は実体が .app 内**: `build/openfdtd_x.app/Contents/MacOS/openfdtd_x`
  (`./build/openfdtd_x` は存在しない)。
- 実行ファイルは 2 つ: 本体 `openfdtd_x` と水中音響の分離アプリ `openuwa`。
  共有 GUI は `ofdx_gui` 静的ライブラリで 1 度だけコンパイルされる。
- 新規ソースは **CMakeLists.txt の GUI_SOURCES / CORE_SOURCES に手で追加** (glob していない)。

## アーキテクチャの掟

1. **データモデル中心**: タブは全て `core/Project` の View。編集後は `Project::touch()`。
   `changed()` / `loaded()` シグナルでビューポート・ツリー・ステータスバーが自動更新。
2. **`.ofd` は本家完全互換**: `io/OfdIO` が読み書き。GUI が知らないキーは
   `Project::extraLines()` に保持し保存時にそのまま書き戻す (ラウンドトリップ保証)。
   `.ofd` の書式を変える変更は禁止。拡張は `.ofdx` (JSON サイドカー) へ。
3. **タブの作法**: `QScrollArea` 継承、ctor は `(Project*, QWidget*)`、
   `apply()` (widgets→model) / `refresh()` (model→widgets, `m_updating` ガード付き)。
   見出し付きグループは `widgets/SectionBox`、小型プロットは `widgets/MiniPlot`。
4. **i18n**: 全 UI 文字列は `I18n::tr("key")`。共通キーは `I18n.cpp` の `loadTables()`、
   タブ固有語彙は各 .cpp 冒頭の file-local `I18n::reg()` (接頭辞をタブ毎に固有に)。
   日本語がベース、`--lang en|both` で切替。
5. **依存を増やさない**: Qt6 Widgets のみ。OpenGL / QML / Qt Charts / 外部ライブラリ禁止
   (HDF5 と libigl はオプションで既にゲート済み)。描画は QPainter。
6. **タブの登録**: 左ナビは `TabNavigator`。タブは `MainWindow::buildLeftNav()` の
   `defs` テーブルに 1 行追加するだけで登録される
   (キー / カテゴリ / ラベルキー / ページ / 対象ドメイン / 標準表示か)。
   ドメインと標準・エキスパートによる絞り込みは `TabNavigator::rebuild()` が担当する
   (`MainWindow::onDomainChanged()` で個別に addTab/removeTab していた旧方式は廃止)。
   tidy3d は独立ドメインではなく光ドメイン専用のクラウドバックエンド。
7. **応用画面はローカル状態**: `.ofd` に対応フィールドが無い画面 (EMC/SAR/電波伝搬/
   多層膜/照明/AR-VR など) は `Project.h` を拡張せず、ウィジェットのローカル状態として
   モック既定値を持つ。**`.ofd` に新しいキーや値を書き出さないこと**。
   フォーマットに無い選択肢を UI に出す場合は、保存時に何へ丸められるかを画面に明示する
   (例: 吸収境界の「Mur 2次」→ `abc=0` = Mur 1次として保存)。
8. **テーマ**: 配色は `Theme` がモックの `styles.css` から QSS を生成する
   (style → dark → density → ドメインaccent のカスケード)。ウィジェット単位の
   `setStyleSheet` はアプリ QSS に勝つので原則使わない。素の `QWidget` 派生に
   背景色を効かせるには `WA_StyledBackground` が必要。

## コーディングスタイル

- コメントは日本語ベース (既存ファイル参照)。ヘッダ冒頭に「// Xxx.h — 役割 (元jsx名)」。
- Qt6 スタイルの connect (ポインタメンバ関数)。`QStringLiteral` は任意。
- UTF-8 リテラル直書き OK (MSVC は `/utf-8` 済み)。
- デザインリファレンス: claude.ai/design プロジェクト「OpenFDTD対応」
  (project_id `f932d009-df2b-46b5-8664-50be11b6018f`)。エントリは 2 つ:
  `OpenFDTD-X.html` → `openfdtd_x`、`OpenUWA-Underwater.html` → `openuwa`。
  画面を変える前にモックを確認し忠実に再現する。**モックは実装後も更新される**ので、
  着手前に design MCP の `list_files` で etag を比較し差分を確認すること。

## やってはいけないこと

- `tests/data/*.ofd` の書き換え (本家 OpenFDTD のサンプルそのもの)
- カーネルソースへの依存追加 (subprocess 起動のみ)
- `ofdx_selftest` を通さないままのコミット
- 検証用 CLI やタブ初期化で `QSettings` を書き換えること
  (スクリーンショットを撮っただけでユーザー設定が変わる)
