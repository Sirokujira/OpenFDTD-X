# オペラ歌手向け室内音響分析 — アーキテクチャ設計

対象: OpenFDTD-X (branch `claude/opera-acoustics`)。
本書は実装済みコード (`src/acoustics/**`, `CMakeLists.txt`) と、フェーズ2
以降の計画部分 (RirAnalysisTab、外部音響ソルバー) の構成を示す。
計画部分は図中で「計画」と明記する。

## 1. 全体構成 (現在の OpenFDTD-X + 音響分析層)

```mermaid
flowchart TB
    subgraph GUI["GUI 層 — C++17 / Qt6 Widgets"]
        MW["MainWindow / DomainBar<br/>(ドメイン切替: EM / 光 / 室内音響 / 水中)"]
        AT["AcousticTab<br/>(FDTD 音響設定)"]
        RAT["RoomAcousticsTab<br/>(統計推定: Sabine / Eyring / Barron / NC)"]
        RIRT["RirAnalysisTab (計画・フェーズ2)<br/>実測 RIR 分析 UI"]
        MP["MiniPlot / PlotPanel<br/>(波形・減衰カーブ表示)"]
    end

    subgraph CORE17["既存モデル層 — C++17 / Qt 依存"]
        PRJ["Project<br/>AcousticOpts / OperaAcousticSettings"]
        ROOMAC["core/RoomAcoustics<br/>(統計推定・純関数)"]
        OFDIO["io/OfdIO<br/>.ofd + .ofdx (JSON) の読み書き"]
        RUN["kernel/Runner<br/>(QProcess: ofd / orcwa / obpm)"]
    end

    subgraph AC["音響分析層 — 新規"]
        QTA["acoustics/qt/QtAcousticAdapter<br/>(C++17 / Qt: 型変換・ch選択)"]
        subgraph CORE14["ofdx_acoustic_core — C++14 / Qt 非依存 (STATIC lib)"]
            RA["RirAnalyzer"]
            WAV["io: WavReader / WavWriter"]
        end
        CAPI["ofdx_acoustic_c_api — 安定 C ABI<br/>openfdtd_x_acoustics.h (C99 から利用可)"]
    end

    EXT["外部音響ソルバー (計画・フェーズ5)<br/>QProcess 疎結合"]
    OFDX[(".ofdx JSON<br/>acoustic.opera_analysis")]
    WAVF[("実測 RIR / 歌唱 WAV")]

    MW --> AT & RAT & RIRT
    RAT --> ROOMAC
    RIRT --> QTA --> RA
    RA --> WAV
    CAPI --> RA
    RIRT --> MP
    AT & RAT & RIRT --> PRJ
    PRJ <--> OFDIO <--> OFDX
    QTA --> WAVF
    MW --> RUN
    RUN -. "フェーズ5で拡張" .-> EXT
    EXT -. "rir.wav" .-> WAVF
```

依存規則 (CMake で強制):

- `ofdx_acoustic_core` は **Qt にリンクしない**。`CXX_STANDARD 14` /
  `CXX_STANDARD_REQUIRED ON` / `CXX_EXTENSIONS OFF` (逸脱はビルドエラー)。
- `ofdx_acoustic_c_api` はコアのみに依存 (同じく C++14 固定)。ヘッダは
  純 C (C99) から include 可能で、`tests/acoustics/test_c_api.c` が
  C コンパイラでのビルド可否そのものを検証する。
- Qt 型 (`QString` 等) がコアに入る唯一の入口は `QtAcousticAdapter`。

## 2. C++14 コアのクラス構成 (実装済み)

```mermaid
classDiagram
    class RirAnalyzer {
        +RirAnalyzer(RirAnalyzerConfig)
        +analyze(ArrayView~const double~, double fs) AcousticResult~RirAnalysisResult~
        +analyze(AudioBuffer, channel) AcousticResult~RirAnalysisResult~
    }
    class RirAnalyzerConfig {
        +CalibrationState calibration
        +double calibrationOffsetDb
        +BandSet bandSet
        +bool zeroPhaseFiltering
        +bool removeDc
        +double minDynamicRangeDb
        +double clipThreshold
        +int clipRunLength
    }
    class DirectSoundDetector {
        +detectDirectSound(x, fs, cfg) DirectSoundResult
        Peak / EnvelopeThreshold / MovingRmsThreshold
    }
    class NoiseFloorEstimator {
        +estimateNoiseFloor(x, tailFraction, minTail) NoiseFloorEstimate
        末尾10% RMS
    }
    class SchroederDecay {
        +computeSchroederDecay(rir, fs, opt) SchroederResult
        +regressDecaySegment(curve, fs, startDb, endDb, endIdx) RegressionResult
        二乗後方積分 + Chu 1978 ノイズ補正
    }
    class AcousticMetrics {
        +computeAcousticMetrics(rir, fs, directIndex, opt) AcousticMetricsSet
        EDT T20 T30 C50 C80 D50 Ts Early/Late
    }
    class BandFilter {
        +makeBands(BandSet) vector~Band~
        +design(lowHz, highHz, fs)$ AcousticResult~BandFilter~
        +apply(x, zeroPhase) vector~double~
        4次バターワース BPF (filtfilt 対応)
    }
    class ReflectionDetector {
        +detectReflections(rir, fs, direct, cfg) vector~ReflectionEvent~
        +summarizeReflections(events, directEnergy) ReflectionTimeSummary
        時間区分 0-20 / 20-80 / 80-200 / 200+ ms
    }
    class WavReader {
        +readWavFile(path) AcousticResult~AudioBuffer~
        +readWavFromMemory(data, size) AcousticResult~AudioBuffer~
    }
    class WavWriter {
        +writeWavFile(path, buffer, format) AcousticResult~bool~
        Pcm16 / Float32
    }

    RirAnalyzer --> RirAnalyzerConfig
    RirAnalyzer --> DirectSoundDetector : ② 直接音検出
    RirAnalyzer --> NoiseFloorEstimator : ① 前処理・動的範囲
    RirAnalyzer --> BandFilter : ③ 帯域分割
    RirAnalyzer --> AcousticMetrics : ④ 帯域別指標
    AcousticMetrics --> SchroederDecay : 減衰カーブ + 回帰
    SchroederDecay --> NoiseFloorEstimator : ノイズパワー推定
    RirAnalyzer --> ReflectionDetector : ⑤ 反射検出 (広帯域)
    RirAnalyzer ..> WavReader : AudioBuffer 経由
    WavWriter ..> WavReader : ラウンドトリップ検証
```

処理順 (`RirAnalyzer::analyze`):
前処理 (入力長 → 非有限値 → クリッピング → DC 除去 → 動的範囲) →
直接音検出 → 絶対 SPL (校正時のみ) → 帯域分割 → 帯域別
Schroeder/ISO 3382-1 指標 → 反射検出・時間区分集計 → 総合品質判定。
共通の結果表現は `MetricValue` (値 + valid + AnalysisQuality + warning) と
`AcousticResult<T>` (エラーコード 16 値、例外なし)。

## 3. 分析結果フロー (フェーズ2: GUI 統合)

```mermaid
sequenceDiagram
    participant U as ユーザー
    participant T as RirAnalysisTab (計画)
    participant A as QtAcousticAdapter (実装済み)
    participant C as ofdx_acoustic_core (実装済み)
    participant P as Project / OfdIO

    U->>T: RIR WAV を選択・設定変更
    T->>P: OperaAcousticSettings 更新 (touch)
    P-->>P: 保存時 .ofdx acoustic.opera_analysis へ (設定のみ)
    U->>T: 「分析」実行
    T->>A: analyzeFile(settings)
    A->>A: readWav (QFile→メモリ→readWavFromMemory)
    A->>A: selectChannel (L / R / 平均モノ)
    A->>A: toAnalyzerConfig (校正/帯域/直接音法/動的範囲)
    A->>C: RirAnalyzer::analyze(samples, fs)
    C-->>A: AcousticResult<RirAnalysisResult>
    A-->>T: 結果 (コア型のまま返す)
    T->>T: MetricValue.quality で 有効/参考値/算出不可 を表示
    T->>T: MiniPlot に波形・Schroeder 減衰カーブ
    Note over T,P: 分析結果は永続化しない (毎回再計算)。<br/>.ofdx には設定のみ保存 (ADR-0003)
```

- 結果のライフサイクル: `RirAnalysisResult` はメモリ上のみ。`.ofdx` には
  設定 (`opera_analysis` キー群) だけを保存する。大容量データ (WAV) は
  パス参照 (`docs/opera-acoustics-file-format.md`)。
- 統計推定 (RoomAcousticsTab) / 実測 RIR / シミュレーション RIR の 3 区分
  を UI で明示する (要求 §2)。

## 4. 外部音響ソルバー連携 (フェーズ5 — 計画)

既存 `ofd` (電磁 FDTD) は音響に流用しない (ADR-0004)。音響ソルバーは
別バイナリとして QProcess で疎結合に起動する。

```mermaid
flowchart LR
    subgraph APP["OpenFDTD-X (GUI)"]
        CFG["AcousticBackend 設定<br/>(enum: None / External)"]
        RES["resolveAcousticSolver()<br/>探索順: ① OFDX_ACOUSTIC_SOLVER (直接指定)<br/>② OPENFDTD_ACOUSTICS_HOME 配下<br/>③ アプリ実行ディレクトリ kernel/<br/>④ PATH"]
        QP["QProcess 起動<br/>(作業ディレクトリに入力一式を書き出し)"]
        AN["RirAnalyzer<br/>(rir.wav を実測と同一パイプラインで分析)"]
    end
    subgraph WD["作業ディレクトリ (出力契約)"]
        MJ["metadata.json<br/>(ソルバー名/版, 格子, fs, 音源・受音点, 実行条件)"]
        RW["rir.wav<br/>(受音点 RIR, float32 推奨)"]
        MX["metrics.json<br/>(ソルバー側算出指標 — 任意)"]
        LG["solver.log<br/>(進捗・診断。stdout 進捗行の写し)"]
    end
    SOLVER["外部音響ソルバー<br/>(別バイナリ・別リポジトリ可)"]

    CFG --> RES --> QP --> SOLVER
    SOLVER --> MJ & RW & MX & LG
    RW --> AN
    MJ --> AN
    AN --> GUI2["結果表示<br/>(区分: シミュレーション RIR)"]
```

出力契約 (ソルバー側の義務):

| ファイル | 必須 | 内容 |
|---|---|---|
| `metadata.json` | 必須 | スキーマ版、ソルバー名/バージョン、格子 (Δx, セル数)、fs、音源/受音点座標、音速、実行時間 |
| `rir.wav` | 必須 | 受音点ごとの RIR (複数受音点は複数チャンネルまたは連番ファイル)。float32 推奨 (WavReader 対応形式であること) |
| `metrics.json` | 任意 | ソルバー側で算出した指標。GUI 側は自前計算 (RirAnalyzer) を正とし、突合表示のみに使う |
| `solver.log` | 必須 | 実行ログ。進捗は stdout にも出力 (Runner が進捗解析) |

環境変数:

- `OPENFDTD_ACOUSTICS_HOME` — 音響ソルバー群のインストール先 (既存
  `OPENFDTD_HOME` / `OPENRCWA_HOME` / `OPENBPM_HOME` と同じ流儀)。
- `OFDX_ACOUSTIC_SOLVER` — ソルバーバイナリの絶対パス直接指定
  (探索順のどれよりも優先。CI・開発時のオーバーライド用)。

`rir.wav` の分析は実測 RIR と完全に同一のコード経路
(`WavReader` → `RirAnalyzer`) を通すため、指標の定義差・実装差が
発生しない — これがフェーズ5 を「ソルバー = RIR 生成器」に限定する
主目的である。

## 5. 二層 C++ 規格 (GUI C++17 / コア C++14) の理由

詳細は `docs/adr/0001-two-tier-cxx-standard.md`。要点:

1. **全体を C++14 にはできない**: Qt6 が C++17 を強制する
   (実測: Qt 6.4.2 `qglobal.h` が `-std=c++14` に対し
   `#error "Qt requires a C++17 compiler"`)。GUI 層は C++17 必須。
2. **コアを C++14 に固定する価値**: 音響コアは外部カーネル
   (フェーズ5 のソルバー側での再利用)・他プロジェクト・古い
   ツールチェーン (CUDA / 組込み / 長期サポートコンパイラ) から
   リンクされ得る。要求規格を下げるほど再利用先が広がる。
3. **機械的強制**: 「C++14 の範囲で書く」だけでは逸脱を検知できない
   (フェーズ0 調査 §7.1)。別 STATIC ライブラリターゲットに分離し
   `CXX_STANDARD 14; CXX_STANDARD_REQUIRED ON; CXX_EXTENSIONS OFF` を
   設定することで、C++17 構文の混入がその場でビルドエラーになる。
4. **接続コスト**: Qt 型との変換は `QtAcousticAdapter` の 1 箇所に集約
   し、コアは `std::vector<double>` / `std::string` / POD のみを使う。
   さらに C ABI (`ofdx_acoustic_c_api`) を被せ、C99 からの利用と
   ABI 前方互換 (`struct_size` / `api_version` 検査) を保証する。
