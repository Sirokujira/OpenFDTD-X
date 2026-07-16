# .ofdx `opera_analysis` スキーマと WAV 対応形式

対象: `.ofdx` (JSON サイドカー) の `acoustic.opera_analysis` オブジェクト。
実装: `src/io/OfdIO.cpp` (save/load)、モデル: `src/core/Project.h`
`OperaAcousticSettings`。

## 1. 位置づけとスキーマバージョン

- `.ofd` (本家 OpenFDTD 互換テキスト) は**不可侵**。音響拡張はすべて
  `.ofdx` 側に置く (フェーズ0 調査 §7.2、baseline 運用ルール4)。
- `opera_analysis` は既存 `acoustic` オブジェクト**内のネスト追加のみ**で
  導入する (ADR-0003)。トップレベル・既存キーは一切変更しない。
- トップレベル `schemaVersion` は `opera_analysis` の導入をもって
  **"1.1"** と定義する ("1.0" = opera_analysis なし)。読み込み側は
  schemaVersion で分岐**しない** (キーの有無だけで判定する) ため、
  1.0 リーダーが 1.1 ファイルを読んでも未知キー無視で壊れない。
  注: 現行実装の save はまだ "1.0" を書き出しており、"1.1" への更新は
  フェーズ2 の残作業 (`docs/opera-acoustics-development-status.md` §3)。

## 2. `acoustic.opera_analysis` キー一覧 (schemaVersion 1.1)

すべて実装済み (`OfdIO.cpp` save/load、既定値は `OperaAcousticSettings`
のメンバ初期化子)。欠落キーは既定値のまま (旧ファイル互換)。

| キー | 型 | 既定値 | 意味 |
|---|---|---|---|
| `enabled` | bool | `false` | 実測 RIR 分析の使用フラグ |
| `rir_file` | string | `""` | 実測 RIR の WAV ファイルパス (§4 参照) |
| `voice_file` | string | `""` | 歌唱音源 WAV (フェーズ3 用に予約) |
| `voice_type` | int | `6` | 0=Sop 1=Mez 2=Alt 3=Ten 4=Bar 5=Bass 6=Unknown |
| `calibration_state` | int | `2` | 0=Absolute 1=Relative 2=Uncalibrated (`CalibrationState` と同順) |
| `direct_sound_method` | int | `1` | 0=Peak 1=EnvelopeThreshold 2=MovingRmsThreshold |
| `band_mode` | int | `0` | 0=既存互換6帯域 1=1oct(63–8k) 2=1/3oct(100–5k) 3=歌手フォルマント帯域 (ADR-0002) |
| `channel_mode` | int | `2` | 0=L 1=R 2=全チャンネル平均モノ |
| `analysis_settings` | object | — | 分析パラメータのネスト (下記) |
| `analysis_settings.noise_correction` | bool | `true` | Schroeder 減衰の Chu ノイズ補正 |
| `analysis_settings.minimum_dynamic_range_db` | double | `35.0` | これ未満の動的範囲はエラー扱い |

enum を int で永続化しているため、**対応する C++ enum の値の並び替え・
挿入は禁止** (既存 `AbsorptionRow::Role` と同じ制約)。追加は末尾のみ可。

将来の追加予定キー (予約 — 現行実装には無い):

| キー (案) | 型 | フェーズ | 意味 |
|---|---|---|---|
| `calibration_offset_db` | double | 2 (負債 #1) | Absolute 時の dBFS→dB SPL オフセット |
| `st_conditions_declared` | bool | 4 | ST 系の 1 m 測定条件の自己申告 |
| `rir_source` | string | 5 | `"measured"` / `"simulated"` (3 区分表示用) |
| `solver` | object | 5 | 外部ソルバー設定 (backend, binary_path 等) |

## 3. 後方互換規則

1. **既存キー不変**: `acoustic` 直下の既存キー (rt60/c80/…/absorption/
   noise_levels) の改名・削除・型変更・配列要素数変更を行わない。
2. **未知キー無視**: ロードは `value(key).toXxx(現在値)` 方式のため、
   欠落キー = 既定値、未知キー = 無視。新キー追加は旧リーダーを壊さない。
   ただし現行の save は既知フィールドから JSON を再構成するため、
   **他ツールが書いた未知キーは保存時に消える** (ADR-0003 で次期対応を
   記録)。
3. **大容量データはファイル参照のみ**: RIR / 歌唱 WAV の波形データ・
   減衰カーブ・帯域別結果などの大容量データは `.ofdx` に埋め込まない。
   保存するのは (a) WAV への**パス**、(b) 分析**設定**、(c) 保存する
   場合でも指標値の**サマリー**まで、とする。現行実装は (a)(b) のみで、
   分析結果は保存せず毎回再計算する (`Project.h` のコメントに明記)。
4. `schemaVersion` は情報提供のみ (読み捨て)。分岐に使い始める場合も
   「未知の将来バージョンは読める範囲で読む」を維持する。

## 4. パスの扱い

- `rir_file` / `voice_file` は現行実装では入力文字列をそのまま保存する
  (絶対パス可)。プロジェクトの可搬性のため、UI は `.ofdx` と同じ
  ディレクトリ配下の相対パスを推奨する (フェーズ2 の UI 実装方針)。
- パスが存在しない場合は分析実行時に `FileNotFound` エラーになるだけで、
  ロード自体は成功する (設定は保持される)。

## 5. WAV 対応形式 (実装済み WavReader / WavWriter)

読み込み (`readWavFile` / `readWavFromMemory`):

| 項目 | 対応 |
|---|---|
| コンテナ | RIFF/WAVE。odd サイズチャンクの 1 バイトパディング対応 |
| フォーマット | PCM 16 / 24 / 32 bit 整数、IEEE float 32 bit |
| WAVE_FORMAT_EXTENSIBLE | 対応 (サブフォーマットで判定) |
| チャンネル数 | 任意 (分析時に L / R / 平均モノを選択) |
| サンプリング周波数 | 任意 (> 0) |
| 正規化 | **自動正規化しない**。整数 PCM はフルスケール ±1.0 へ変換のみ、float は値をそのまま保持 |
| 非対応 | PCM 8 bit、float 64 bit、圧縮形式 (ADPCM / MP3 等) → `UnsupportedFormat` |

書き出し (`writeWavFile`):

| フォーマット | 挙動 |
|---|---|
| `Pcm16` | ±1.0 を超える値はクランプして 16 bit 量子化 |
| `Float32` | 値をそのまま書き出す (クランプなし) |

全チャンネルをインターリーブして書き、チャンネル間のサンプル数は一致が
必要。ラウンドトリップ精度は `tests/acoustics/test_wav.cpp` で検証済み
(PCM16 ≤ 1e-4、PCM24 ≤ 3e-7、PCM32 ≤ 2e-9、float32 ≤ 1e-6)。

## 6. 保存例 (schemaVersion 1.1)

```json
{
  "schemaVersion": "1.1",
  "domain": "acoustic",
  "acoustic": {
    "rt60": true, "c80": true,
    "...": "既存キーはそのまま",
    "opera_analysis": {
      "enabled": true,
      "rir_file": "measurements/stage_center.wav",
      "voice_file": "",
      "voice_type": 6,
      "calibration_state": 2,
      "direct_sound_method": 1,
      "band_mode": 3,
      "channel_mode": 2,
      "analysis_settings": {
        "noise_correction": true,
        "minimum_dynamic_range_db": 35.0
      }
    }
  }
}
```
