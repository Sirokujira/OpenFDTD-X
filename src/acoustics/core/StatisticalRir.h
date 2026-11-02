// StatisticalRir.h — 統計モデルによる受聴位置の RIR 合成 (可聴化用)。
// Qt 非依存 / C++14。
//
// ── 何を作るか ──────────────────────────────────────────────────────────────
// ホール解析タブ (統計推定) の「各席の可聴化」が使う、波動計算も幾何音響の
// 追跡もしない**統計モデルの RIR**。実測 RIR やソルバーの RIR の代わりではなく、
// 設計初期に「この RT・この席の距離ならどう聞こえるか」を耳で確かめる目安。
//
//   h(t) = A_d·δ(t − t_d)                        直接音
//        + Σ A_d·a_i·δ(t − t_d − τ_i)            初期の離散反射 (呼び出し側が
//                                                 1 次鏡像法などで与える)
//        + Σ_b g_b·[BP_b * n_b](t − t_d)·e^{−6.91 (t − t_d)/T_b}   拡散音
//
// 拡散音は帯域 b ごとに独立な白色雑音 n_b を帯域通過 (BandFilter、ゼロ位相)
// してから振幅包絡 e^{−6.91 t/T_b} (T_b 秒で −60 dB) を掛ける。**包絡は濾波の
// 後に掛ける**ので減衰率は T_b のまま崩れない (先に掛けるとフィルタの応答で
// 減衰の始まりがなまる)。帯域は ISO 3382 の Compat6 (125 Hz … 4 kHz 1/1 oct)。
//
// ── 拡散音の大きさ ─────────────────────────────────────────────────────────
// g_b は「帯域 b のフィルタ (ゼロ位相) を通して見た拡散音のエネルギー /
// 同じフィルタを通した直接音のエネルギー」が lateToDirect[b] になるよう
// 決める。両方とも**分析側 (RirAnalyzer) と同じ濾波をもう一度かけた出力**で
// 数値的に測るので、分析で見える比と定義が一致する (隣の帯域の拡散音が
// フィルタの裾から漏れ込む分は含まない)。lateToDirect の値 (Barron の
// 修正理論など) をどう選ぶかは呼び出し側の責任。
//
// ── 再現性 ────────────────────────────────────────────────────────────────
// 雑音は seed から作る決定的な xorshift32 + Box–Muller。現在時刻や
// std::random_device は使わない (同じ入力なら同じビット列 — selftest/ctest で
// 判定している)。
//
// ── 含まないもの (呼び出し側は UI に明記すること) ─────────────────────────
// - 6 帯域の外 (≈ 88 Hz 未満・≈ 5.7 kHz 超) の拡散音 (直接音と離散反射は全帯域)
// - 2 次以上の鏡像・拡散反射の時間構造 (拡散音は統計的な包絡だけ)
// - 両耳・指向性 (モノラル)
#pragma once
#include <vector>

#include "AcousticError.h"
#include "AudioBuffer.h"

namespace ofd {
namespace acoustics {

struct StatRirReflection {
    double delaySeconds; // 直接音からの遅れ [s] (≥ 0)
    double amplitude;    // 直接音の振幅に対する比 (符号つき可)
    StatRirReflection() : delaySeconds(0.0), amplitude(0.0) {}
    StatRirReflection(double d, double a) : delaySeconds(d), amplitude(a) {}
};

constexpr int kStatRirBands = 6;   // Compat6 (125, 250, 500, 1k, 2k, 4k)

struct StatisticalRirConfig {
    double sampleRateHz;        // 出力の fs (4 kHz 帯の上端 5.66 kHz より 2 倍以上)
    double directDelaySeconds;  // 直接音の到達時刻 [s] (≥ 0)
    double directAmplitude;     // 直接音の振幅 (例: 1/r)
    std::vector<StatRirReflection> reflections;
    double rt60[kStatRirBands];        // 残響時間 [s] (lateToDirect > 0 の帯域は > 0)
    double lateToDirect[kStatRirBands];// 拡散音 / 直接音 のエネルギー比 (≥ 0)
    // 拡散音の始まり (直接音からの遅れ [s]、≥ 0)。これより前の拡散音は 0。
    // 包絡は直接音の到達から測った e^{−6.91 t/T} のまま (始まりで値を
    // 持ち上げない) なので、lateToDirect は「始まり以降」のエネルギー比。
    // 始まりのクリックを避けるため 5 ms の二乗余弦で立ち上げる
    double diffuseOnsetSeconds;
    double durationSeconds;     // ≤ 0 なら t_d + 1.5·max T + 0.1 s
    unsigned seed;

    StatisticalRirConfig()
        : sampleRateHz(48000.0), directDelaySeconds(0.0), directAmplitude(1.0),
          reflections(), diffuseOnsetSeconds(0.0), durationSeconds(0.0), seed(1u)
    {
        for (int b = 0; b < kStatRirBands; ++b) { rt60[b] = 0.0; lateToDirect[b] = 0.0; }
    }
};

// モノラルの AudioBuffer を返す。引数が不正 (fs が 4 kHz 帯を表せない、
// 負の値、拡散音を持つ帯域の T ≤ 0、長さが直接音・反射より短い) なら
// InvalidArgument。
AcousticResult<AudioBuffer> synthesizeStatisticalRir(const StatisticalRirConfig &c);

} // namespace acoustics
} // namespace ofd
