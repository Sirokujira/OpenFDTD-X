# 音響指標の計算定義 (実装準拠)

本書は `src/acoustics/core/` の**実装そのもの**を仕様として記述する
(AcousticMetrics.{h,cpp} / SchroederDecay.{h,cpp} / NoiseFloorEstimator.cpp)。
実装と本書が食い違う場合は実装を読み、本書を直すこと。
検証結果は `docs/opera-acoustics-validation.md`。

## 0. 共通の前提

- 入力: 単一チャンネル RIR `h[i]` (i = 0..N−1)、サンプリング周波数 fs。
- **時間原点**: `computeAcousticMetrics(rir, fs, directIndex)` は
  `rir[directIndex]` を t = 0 として直接音以降を切り出す
  (ISO 3382-1 は直接音到来を原点とする)。直接音位置は
  `DirectSoundDetector` が決める。
- エネルギーの数値下限: `kEnergyFloor = 1e-30` (10·log10 → −300 dB)。
  振幅の下限は 1e-15 (20·log10 → −300 dB)。
- 各指標は `MetricValue { value, valid, quality, warning }` で返る。
  quality は Valid / Warning (参考値) / Invalid (算出不可) の 3 値。

## 1. Schroeder 減衰カーブ (二乗後方積分 + Chu ノイズ補正)

出典: Schroeder (1965)。ノイズ補正は Chu (1978) の末尾ノイズエネルギー
減算に相当。

1. ノイズパワー推定: 末尾区間 (既定: 全長の 10%、最小 256 サンプル。
   入力が短い場合は全体を使い warning) の RMS から
   `p̂ = noiseRms²` (`estimateNoiseFloor`)。
2. 後方積分: `E(i) = Σ_{k=i}^{N-1} h[k]²`。
3. ノイズ補正 (`noiseCompensation = true` のとき。指標計算の既定は **on**):
   `E(i) ← max(0, E(i) − p̂·(N−i))`。
4. 正規化 dB 化: `L(i) = 10·log10(E(i)/E(0))`、比の下限 1e-30 で
   クランプ (−300 dB)。`L(0) = 0`。
5. **分析終了点** (`analysisEndIndex`): 窓幅 5 ms (既定) の移動平均
   包絡線 (x² の平均) が、包絡線最大位置より後で初めて
   `p̂ × 10^(margin/10)` (margin 既定 10 dB) を下回る点。見つからなければ
   末尾。回帰はこの点より手前のみで行う (Lundeby 法の簡略版)。
6. 減衰カーブ基準ノイズフロア:
   `noiseFloorDb = 10·log10((p̂ + 1e-30)/maxSmooth)` (maxSmooth は
   平滑化包絡線の最大値)。指数減衰では包絡線と Schroeder カーブが同じ
   傾きで減衰するため、この値を各残響指標の評価下限判定に使う。

無効条件 (`SchroederResult.valid = false`): 空入力 / fs ≤ 0 /
補正後の全エネルギー E(0) ≤ 0。

## 2. 減衰カーブの線形回帰 (`regressDecaySegment`)

区間 [startDb, endDb] (下記各指標の値) について:

- 開始点 i0: `L(i) ≤ startDb` となる最初の i。
  終了点 i1: `L(i) ≤ endDb` となる最初の i。いずれも
  `analysisEndIndex` より手前に存在しなければ **invalid**
  (「ノイズフロア到達前に評価下限に達しない」)。
- 最小二乗直線 `L ≈ slope·t + intercept` (t = i/fs [s])。点数 < 4 は
  invalid。
- 決定係数: `R² = 1 − SSE/Syy` (Syy = 0 なら 1)。
- 傾きの標準誤差: `SE(slope) = sqrt(SSE/(n−2)/Sxx)` [dB/s]。
- 結果には実際に使った区間の開始/終了レベル (startDb/endDb 実測値) が
  入る。

## 3. 残響指標 EDT / T20 / T30

共通式: `RT = −60 / slope` [s] (slope [dB/s] は §2 の回帰の傾き)。

| 指標 | 回帰区間 | 外挿倍率 | 出典 |
|---|---|---|---|
| EDT | 0 〜 −10 dB | ×6 (−60/−10) | ISO 3382-1 A.2.2 |
| T20 | −5 〜 −25 dB | ×3 (−60/−20) | ISO 3382-1 6.3 |
| T30 | −5 〜 −35 dB | ×2 (−60/−30) | ISO 3382-1 6.3 |

無効条件 (`valid = false`、順に判定):

1. **動的範囲不足**: `noiseFloorDb > endDb − validityMarginDb`
   (マージン既定 10 dB)。つまりノイズフロアが EDT: −20 dB /
   T20: −35 dB / T30: −45 dB より高いと算出不可。warning に
   「insufficient dynamic range (noise floor … > required …)」。
2. 回帰失敗 (§2 の各理由)。
3. `slope ≥ −1e-9` (減衰していない)。

品質低下 (`quality = Warning`、値は返す = 参考値):
`R² < rSquaredWarningThreshold` (既定 0.95) のとき
「low regression quality (R² = …)」。

パイプライン全体の動的範囲要件 (`RirAnalyzer`): 入力全体の
`peakDb − noiseFloorDb` が `minDynamicRangeDb` (コア既定 10 dB、
GUI 設定既定 35 dB) 未満なら分析自体をエラー
(`InsufficientDynamicRange`)、`warnDynamicRangeDb` (既定 30 dB) 未満は
警告付きで続行。

## 4. 明瞭度 C50 / C80 (ISO 3382-1 A.2.4)

境界サンプル: `b_te = floor(te·fs + 0.5)`、早期 = [0, b)、後期 = [b, N)
(直接音を 0 とした切り出し後の添字)。

```
C_te = 10·log10( Σ_{i=0}^{b-1} h[i]² / Σ_{i=b}^{N-1} h[i]² )   [dB]
te = 50 ms (C50) / 80 ms (C80)
```

無効条件: 全エネルギー ≤ 1e-30 (「no energy after direct sound」) /
信号長 N ≤ b_te (「signal shorter than 50/80 ms after direct」) /
後期エネルギー ≤ 1e-30 (「no late energy (ratio undefined)」)。

## 5. Definition D50 (ISO 3382-1 A.2.3)

```
D50 = Σ_{i=0}^{b50-1} h[i]² / Σ_{i=0}^{N-1} h[i]²    [0..1]
```

無効条件: N ≤ b50 / 全エネルギー ≤ 1e-30。
(後期エネルギーが 0 でも D50 は定義でき、値 1.0 になる — テスト済み。)

## 6. 重心時間 Ts (ISO 3382-1 A.2.5)

```
Ts = Σ_i (i/fs)·h[i]² / Σ_i h[i]²    [s]   (i = 0..N−1, 直接音 = t 0)
```

無効条件: 全エネルギー ≤ 1e-30。
注意: 積分上限は理論の ∞ ではなく**測定信号の末尾**である
(仮定 `docs/opera-acoustics-assumptions.md` §13)。

## 7. Early/Late エネルギー比 (50 ms / 80 ms)

```
EL_te = Σ_{i<b_te} h[i]² / Σ_{i≥b_te} h[i]²    (線形比, 無次元)
```

C50/C80 の線形版 (`C_te = 10·log10(EL_te)`)。無効条件は C50/C80 と同じ。

## 8. 帯域別指標 (BandFilter)

- 4 次バターワース帯域通過 IIR (双一次変換 + プリワーピング、零点
  z = +1 ×2 / z = −1 ×2、中心周波数で振幅 1 正規化)。
- `zeroPhaseFiltering = true` (既定) で前後方向フィルタ (filtfilt 相当、
  ゼロ位相)。このとき振幅特性は 2 乗され**実効 8 次**。
- 帯域上端がナイキスト (fs/2) 以上のときは `FilterDesignFailed` →
  その帯域の全指標が算出不可 (帯域単位の warning)。
- 帯域セット: Compat6 (125…4k オクターブ) / FullBandOnly /
  Octave63To8k / ThirdOctave100To5k (IEC 61260 公称中心) /
  SingerFormant (2.0-2.5k / 2.5-3.15k / 3.15-4.0k / 2.0-4.0k)。

## 9. 単位・表示レンジまとめ

| 指標 | 単位 | 典型レンジ (表示目安) |
|---|---|---|
| EDT / T20 / T30 | s | 0.3 – 4.0 |
| C50 | dB | −10 – +10 |
| C80 | dB | −5 – +15 |
| D50 | 無次元 (0..1) | 0 – 1 |
| Ts | s (UI は ms 表示可) | 0 – 0.4 |
| Early/Late | 線形比 | 0 – ∞ (log 表示推奨) |
| ノイズフロア / ピーク | dBFS | −120 – 0 |
| 反射相対レベル | dB (直接音比) | −60 – 0 |

## 10. 出典

- ISO 3382-1:2009 — Acoustics — Measurement of room acoustic
  parameters — Part 1: Performance spaces (6.3 残響時間、Annex A.2:
  EDT/D50/C50/C80/Ts の定義)。
- M. R. Schroeder, "New Method of Measuring Reverberation Time",
  J. Acoust. Soc. Am. 37, 409–412 (1965) — 二乗後方積分。
- W. T. Chu, "Comparison of reverberation measurements using
  Schroeder's impulse method and decay-curve averaging method",
  J. Acoust. Soc. Am. 63(5), 1444–1450 (1978) — 定常ノイズエネルギー
  減算による補正。
- (参考) A. Lundeby et al., "Uncertainties of Measurements in Room
  Acoustics", Acustica 81 (1995) — 分析終了点判定の考え方 (本実装は
  簡略版であり完全な反復法ではない)。

規格文書・論文本文は転載せず、定義から自前実装している
(`docs/licensing-review.md` §3)。
