# ADR-0011: 角度依存吸音 (局所反応境界) の契約

## Status

Accepted (2026-10-18) — ソルバー側 (OpenAcoustics の `ofdx_acoustic_fdtd` /
`ofdx_acoustic_ga` 両バイナリ) は実装・検証済み (OpenAcoustics PR #8)。
GUI 側の設定 UI (AcousticSolverTab のトグル →
`AcousticOpts::angleDependentAbsorption` → `.ofdx`
`acoustic.angle_dependent_absorption`) も実装済み (既定のままならキー自体を
書かない — 絶対規則 2。selftest がバイト一致往復を検証する)。
**本 ADR に未実装の項目は無い。**

## Context

- AcousticTab の吸音表 (`.ofdx` `acoustic.absorption[].alpha[6]`) の値は、
  カタログ・文献の**残響室法 (ランダム入射) 吸音率**である。
- 外部ソルバーは従来、この α を**垂直入射**の値として境界に写していた
  (圧力反射係数 R = √(1−α))。ところが FDTD の局所反応インピーダンス境界は
  斜入射の反射 R(θ) = (ζcosθ − 1)/(ζcosθ + 1) を離散化から自動的に持つ
  ので、垂直入射として写すと**斜入射で吸音が二重に効く**。
- 幾何音響ソルバーは `acoustic.ga.angle_dependent_absorption` (手書きキー) で
  Paris の式の逆解に切り替えられたが、FDTD 側に同じ切り替えが無く、
  ハイブリッド合成 (ADR-0008) のクロスオーバーで低域と高域の壁が食い違って
  いた。ADR-0010 と同じく、**両ソルバーが同じ境界モデルを使うことが整合の
  前提**である。

## Decision

1. **`.ofdx` に `acoustic.angle_dependent_absorption` (bool、既定 false) を
   新設する。** false = 従来どおり α を垂直入射として R = √(1−α) (完全後方
   互換 — 既存入力の結果は 1 bit も変わらない)。true = α を**ランダム入射**
   として読み、Paris の式
   α_stat(ζ) = (8/ζ²)[ζ + 1 − 2 ln(1+ζ) − 1/(1+ζ)]
   を逆に解いた規格化インピーダンス ζ で局所反応境界を作る
   (FDTD は Z = ρc·ζ、幾何音響は R(θ) = (ζcosθ−1)/(ζcosθ+1))。
2. **キーは `acoustic` 直下**に置く (ADR-0010 Decision 1 と同じ理由 — 両
   ソルバーが読む。FDTD 側は `acoustic.ga` を丸ごと読み飛ばす規約)。幾何音響
   ソルバーは既存の `acoustic.ga.angle_dependent_absorption` があればそちらを
   優先する (幾何音響だけ切り替えていた既存入力の後方互換。JSON の並び順に
   依らない)。GUI は `acoustic` 直下のキーだけを書く。
3. **逆問題の枝と上限**: α_stat は ζ ≈ 1.55 で最大 0.951 の山型なので、
   山より右 (ζ が大きい = 空気より硬い通常の吸音材) の枝で解く。0.951 を
   超える α は局所反応の実インピーダンスでは表せないので、**クランプして
   solver.log に warning** を出す (完全吸音と偽らない)。
4. **両ソルバーで対称に実装する** (同じ式・同じ枝。片側のみの変更は禁止)。
5. **metadata.json は追加キーのみ**: 両ソルバーとも
   `angle_dependent_absorption` (bool)。FDTD は `boundary_zeta`
   (壁ごとの ζ) も出す。ファイル契約 (ADR-0007) は不変なのでモックの変更は
   不要。
6. **ソルバー側の番人** (OpenAcoustics `acoustic_check.sh`):
   - FDTD (i) — 準 1D 管で垂直入射の R が (ζ−1)/(ζ+1) と ±3%
     (α_stat = 0.3 → ζ = 19.766 → R = 0.9037。従来解釈の √0.7 = 0.8367 は
     −7.4% で外れる)、metadata の ζ が Paris の逆解と一致、false 指定は
     既定と rir.wav がバイト一致、α = 1 はクランプ + warning。
   - 幾何音響 (I) — 斜入射 1 発の振幅が R(θ)/(4πr) と ±1%、off で
     √(1−α)、`acoustic` 直下に書いても `acoustic.ga` 側と rir.wav が
     バイト一致。
   期待値は Paris の公式を awk で独立に解いたもの (ソルバーの実装は使わない)。

## Consequences

- (+) 既存フローは無変更 (opt-in、キー省略時は従来動作と完全一致)。
  ADR-0003 の未知キー無視により、旧版 GUI / 旧版ソルバーとも共存できる。
- (+) トグル 1 つで両ソルバーに同時に効く (単発実行・ハイブリッド実行とも
  同じ `prepareRunInput` 経路でサイドカーを書く) ので、対称性が自動的に
  保たれる。
- (−) **周波数依存のインピーダンス** (バンドごとに違う Z) は範囲外。FDTD は
  有効帯域 [0, fmax] と重なるオクターブバンドの平均 α から ζ を 1 つ作る
  (OpenAcoustics の不変条件 3b)。帯域内で Z を周波数依存にするには境界の
  デジタルフィルタ (受動性の保証を含む) が要り、別途判断する。
- (−) 局所反応の仮定そのもの (横方向に音が伝わる多孔質材・共鳴器の拡張
  反応) は扱わない。
