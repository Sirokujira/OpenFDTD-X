# 開発状況 (フェーズ0–5)

記録日: 2026-07-16。対象 branch: `claude/opera-acoustics`
(フェーズ0/1 はコミット 093bcc4 済み、フェーズ2 は作業ツリー上で実装中)。

## 1. フェーズ進捗表

| フェーズ | 内容 | 状態 | 根拠 (実在する成果物) |
|---|---|---|---|
| 0 | 事前調査・基準記録・ライセンス調査 | **完了** | `docs/opera-acoustics-existing-analysis.md` / `opera-acoustics-baseline.md` (selftest 1560 checks 実測) / `licensing-review.md` |
| 1 | C++14 音響分析コア (RIR パイプライン + WAV I/O + テスト) | **完了** | `src/acoustics/core/` (RirAnalyzer ほか 9 モジュール)、`src/acoustics/io/` (WavReader/Writer)、`tests/acoustics/` 6 テスト + 生成器。検証 392 checks / 0 failures (`docs/opera-acoustics-validation.md`) |
| 2 | GUI 統合 (C API / Qt アダプター / RirAnalysisTab / .ofdx / CI) | **実装中** | 済: CMake ターゲット分離 (`ofdx_acoustic_core` C++14 固定 / `ofdx_acoustic_c_api`)、C API 実装 + 純 C テスト (`test_c_api.c`)、`QtAcousticAdapter`、`OperaAcousticSettings`、`.ofdx opera_analysis` save/load、CI への ctest 追加。未: 下記 §2 |
| 3 | 歌声信号分析 (F0 / ビブラート / LTAS / HNR) | **未着手** | `.ofdx` の `voice_file` / `voice_type` キー予約のみ (要求 §5) |
| 4 | 校正・歌手フィードバック拡張 (ST 系本実装 / G 絶対値 / 校正ワークフロー / レポート出力) | **未着手** | 要求 §3.2 / §4.3 と仮定 §1/§2 で方針のみ定義 |
| 5 | 外部音響ソルバー連携 (QProcess 疎結合・出力契約) | **未着手** | `docs/adr/0004-external-acoustic-solver.md` とアーキテクチャ §4 で設計のみ |

## 2. フェーズ2 の残作業 (実装中 → 完了の条件)

1. **RirAnalysisTab の実装**: `src/tabs/` に新規タブ (WAV 選択、設定
   編集、分析実行、指標テーブル (品質 3 値表示)、波形 / Schroeder
   減衰カーブの MiniPlot 表示、反射時間区分の表示)。
2. **MainWindow 統合**: 音響ドメインの `onDomainChanged()` 分岐に
   タブ追加 (既存 2 タブの並び・文言は不変 — baseline 運用ルール)。
3. **I18n キー追加**: 新 UI 文言の日英対訳。
4. **selftest 拡張**: `opera_analysis` の .ofdx ラウンドトリップ
   チェック追加 (チェック総数が 1560 から増えるため baseline 文書の
   数値更新もセット)。
5. **schemaVersion "1.1" 書き出し**: 現行 save は "1.0" のまま
   (`docs/opera-acoustics-file-format.md` §1)。
6. **calibration_offset_db の追加** (負債 #1)。

## 3. 既知の技術的負債

| # | 内容 | 影響 | 対応方針 |
|---|---|---|---|
| 1 | `calibrationOffsetDb` が `OperaAcousticSettings` / `.ofdx` / `QtAcousticAdapter::toAnalyzerConfig` に無い | GUI 経由で Absolute を選んでもオフセット 0 dB (絶対 SPL が dBFS のまま) | フェーズ2 残作業でキー `calibration_offset_db` を追加 |
| 2 | save が `schemaVersion: "1.0"` を書く | 1.1 ファイルの識別ができない (実害は小: 読み込みはキー有無判定) | フェーズ2 残作業 |
| 3 | selftest に `opera_analysis` ラウンドトリップ未追加 | 永続化の回帰をテストが検出しない | フェーズ2 残作業 |
| 4 | `.ofdx` の未知キーが保存時に消える (既知フィールド再構成方式) | 他ツールとの .ofdx 共有で相手のキーを失う | 次期対応 (ADR-0003: メモリ保持 + 保存時マージ。今回は非実装) |
| 5 | C API が広帯域 7 指標のみ (帯域別結果・反射リスト・warning 文字列・Early/Late は未公開) | 外部カーネルからの利用範囲が限定的 | 需要が出た時点で `struct_size`/`api_version` 拡張規約に従い追加 |
| 6 | `QtAcousticAdapter::readWav` がファイル全体を QByteArray に読む | 巨大 WAV (長時間・高 fs) でメモリピーク | ストリーミング読みは必要になるまで保留 |
| 7 | 帯域フィルタの数値精度検証が fs=48 kHz のみ (低い fc/fs 比 — 例 63 Hz @ 96 kHz — の IIR 係数精度が未検証) | 高 fs 入力の低帯域で精度低下の可能性 | 追加テスト候補 (96 kHz ケース) |
| 8 | クリッピング検出の陽性系ユニットテストが無い (統合テストの陰性確認のみ) | 検出ロジック回帰の検出漏れ | 追加テスト候補 |
| 9 | MiniPlot に対話機能 (ズーム/カーソル) が無い (フェーズ0 調査 §2.3) | 減衰カーブの詳細確認がしづらい | フェーズ2 では現状機能で表示し、拡張は別課題 |
| 10 | ST 系 / G / 実測 STI が未実装 (要求のみ) | 舞台支援・声の届きの定量値が不足 | フェーズ4 |

## 4. 品質基準の現在値

- 既存 baseline: `ofdx_selftest` = 24 files loaded, **1560 checks,
  0 failures** (不変であること)。
- 音響コア: 7 テスト **392 checks, 0 failures**
  (`docs/opera-acoustics-validation.md` §9)。
- CI: Linux job に `ctest --test-dir build --output-on-failure`、
  Windows job に `-C Release` + `TMPDIR` 設定を追加済み (作業ツリー)。

## 5. 次の作業 (優先順)

1. フェーズ2 残作業 §2 の 1→6 (RirAnalysisTab → 統合 → I18n →
   selftest → schemaVersion → 校正オフセット)。
2. 負債 #7 / #8 の追加テスト (96 kHz 帯域フィルタ、クリッピング陽性)。
3. フェーズ2 完了時に baseline 文書のチェック総数を更新し、
   本書のフェーズ表を更新。
4. フェーズ3 (歌声信号分析) の詳細設計 — YIN 自前実装の方針は
   `docs/licensing-review.md` §3 で確認済み。
5. フェーズ5 の出力契約 (metadata.json スキーマ) の確定
   (ADR-0004 の Consequences 参照)。
