// test_statrir.cpp — 統計モデル RIR 合成 (StatisticalRir) の検証。
// 合成した RIR を既存の RirAnalyzer (別実装) で分析して、入力に与えた
// 帯域別 T と拡散音/直接音のエネルギー比が戻ってくることを確かめる。
#include <cmath>
#include <cstdio>
#include <vector>

#include "../../src/acoustics/core/ArrayView.h"
#include "../../src/acoustics/core/BandFilter.h"
#include "../../src/acoustics/core/RirAnalyzer.h"
#include "../../src/acoustics/core/StatisticalRir.h"
#include "test_common.h"

using namespace ofd::acoustics;

namespace {

StatisticalRirConfig baseConfig()
{
    StatisticalRirConfig c;
    c.sampleRateHz = 48000.0;
    c.directDelaySeconds = 0.05;
    c.directAmplitude = 0.1;          // r = 10 m 相当の 1/r
    const double t[kStatRirBands] = { 2.2, 2.0, 1.8, 1.7, 1.5, 1.2 };
    for (int b = 0; b < kStatRirBands; ++b) { c.rt60[b] = t[b]; c.lateToDirect[b] = 3.0; }
    c.seed = 7u;
    return c;
}

double energy(const std::vector<double> &x, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (std::size_t i = from; i < to && i < x.size(); ++i) e += x[i] * x[i];
    return e;
}

// (1) 直接音と離散反射は指定どおりのサンプルに指定どおりの振幅で置かれる
void testImpulses()
{
    StatisticalRirConfig c = baseConfig();
    for (int b = 0; b < kStatRirBands; ++b) c.lateToDirect[b] = 0.0;   // 拡散音なし
    c.reflections.push_back(StatRirReflection(0.012, 0.5));
    c.reflections.push_back(StatRirReflection(0.0305, -0.25));
    c.durationSeconds = 0.2;
    const AcousticResult<AudioBuffer> r = synthesizeStatisticalRir(c);
    CHECK(r.success());
    const std::vector<double> &h = r.value().channels[0];
    CHECK(h.size() == std::size_t(std::ceil(0.2 * 48000.0)));
    const std::size_t i0 = 2400;                          // 0.05 s × 48 kHz
    CHECK(h[i0] == 0.1);
    CHECK(h[i0 + 576] == 0.05);                           // 12 ms
    CHECK(h[i0 + 1464] == -0.025);                        // 30.5 ms
    double rest = 0.0;
    for (std::size_t i = 0; i < h.size(); ++i)
        if (i != i0 && i != i0 + 576 && i != i0 + 1464) rest += std::fabs(h[i]);
    CHECK(rest == 0.0);                                   // ほかは厳密に 0
}

// (2) 同じ seed なら同じビット列、違う seed なら違う
void testDeterminism()
{
    StatisticalRirConfig c = baseConfig();
    c.durationSeconds = 1.0;
    const AcousticResult<AudioBuffer> a = synthesizeStatisticalRir(c);
    const AcousticResult<AudioBuffer> b = synthesizeStatisticalRir(c);
    c.seed = 8u;
    const AcousticResult<AudioBuffer> d = synthesizeStatisticalRir(c);
    CHECK(a.success() && b.success() && d.success());
    CHECK(a.value().channels[0] == b.value().channels[0]);
    CHECK(a.value().channels[0] != d.value().channels[0]);
}

// (3) RirAnalyzer で帯域別 T30 を測ると入力の T に戻る (±5% — 検証許容値)。
//     雑音の 1 つの実現では T30 自体がばらつく (ISO 3382-2 の不確かさ
//     σ(T30) ≈ 0.55·T·√(2.52/(B·T)): 125 Hz・T = 2.2 s で約 6%) ので、
//     8 つの seed の平均で比べる (平均の σ は約 2%)
void testDecayTimes()
{
    double mean[kStatRirBands] = { 0, 0, 0, 0, 0, 0 };
    const int kSeeds = 8;
    StatisticalRirConfig c = baseConfig();
    for (int s = 0; s < kSeeds; ++s) {
        c.seed = unsigned(100 + s);
        const AcousticResult<AudioBuffer> r = synthesizeStatisticalRir(c);
        CHECK(r.success());
        RirAnalyzer an;
        const AcousticResult<RirAnalysisResult> res = an.analyze(r.value());
        CHECK(res.success());
        if (!res.success()) return;
        const std::vector<BandMetricsResult> &bands = res.value().bands;
        CHECK(bands.size() == std::size_t(kStatRirBands));
        for (int b = 0; b < kStatRirBands && b < int(bands.size()); ++b) {
            CHECK(bands[b].metrics.t30.valid);
            mean[b] += bands[b].metrics.t30.value / kSeeds;
        }
    }
    const std::vector<Band> bands = makeBands(BandSet::Compat6);
    for (int b = 0; b < kStatRirBands; ++b) {
        CHECK_REL(mean[b], c.rt60[b], 0.05);
        std::printf("  %s: mean T30 = %.3f s (input %.2f, %+.2f%%)\n",
                    bands[b].label.c_str(), mean[b], c.rt60[b],
                    100.0 * (mean[b] - c.rt60[b]) / c.rt60[b]);
    }
}

// (4) 帯域で見た 拡散音 / 直接音 のエネルギー比が lateToDirect になる。
//     帯域ごとに 1 つだけ拡散音を入れれば隣の帯域からの漏れが無いので、
//     定義どおりの比が厳密 (濾波の数値誤差の範囲) に戻る
void testLevels()
{
    for (int b = 0; b < kStatRirBands; ++b) {
        StatisticalRirConfig c = baseConfig();
        for (int k = 0; k < kStatRirBands; ++k) c.lateToDirect[k] = 0.0;
        c.lateToDirect[b] = 2.5;
        const AcousticResult<AudioBuffer> rDir = [&] {
            StatisticalRirConfig d = c;
            d.lateToDirect[b] = 0.0;
            d.durationSeconds = 0.05 + 1.5 * c.rt60[b] + 0.1;
            return synthesizeStatisticalRir(d);
        }();
        const AcousticResult<AudioBuffer> rAll = synthesizeStatisticalRir(c);
        CHECK(rDir.success() && rAll.success());
        const std::vector<Band> bands = makeBands(BandSet::Compat6);
        const AcousticResult<BandFilter> f =
            BandFilter::design(bands[b].lowHz, bands[b].highHz, 48000.0);
        const std::vector<double> &hd = rDir.value().channels[0];
        const std::vector<double> &ha = rAll.value().channels[0];
        CHECK(hd.size() == ha.size());
        std::vector<double> late(ha.size());
        for (std::size_t i = 0; i < ha.size(); ++i) late[i] = ha[i] - hd[i];
        const double eD = energy(f.value().apply(ArrayView<const double>(hd.data(), hd.size()), true),
                                 0, hd.size());
        const double eL = energy(f.value().apply(ArrayView<const double>(late.data(), late.size()), true),
                                 0, late.size());
        // 直接音のインパルスは配列の端に近いと濾波の過渡が欠けるので、
        // ここでの比は合成側 (配列中央で測る) と 1% 以内で一致すればよい
        CHECK_REL(eL / eD, 2.5, 0.01);
    }
}

// (4b) 拡散音の始まり: それより前は厳密に 0 (直接音以外)、比は始まり以降で決まる
void testOnset()
{
    StatisticalRirConfig c = baseConfig();
    c.diffuseOnsetSeconds = 0.020;
    // 呼び出し側 (ホール解析タブ) と同じく、始まりには最初の反射がある
    c.reflections.push_back(StatRirReflection(0.020, 0.4));
    const AcousticResult<AudioBuffer> r = synthesizeStatisticalRir(c);
    CHECK(r.success());
    const std::vector<double> &h = r.value().channels[0];
    const std::size_t i0 = 2400, on = 2400 + 960;           // 20 ms × 48 kHz
    double before = 0.0;
    for (std::size_t i = i0 + 1; i < on; ++i) before += std::fabs(h[i]);
    CHECK(before == 0.0);
    CHECK(h[i0] == 0.1);
    CHECK(std::fabs(h[on] - 0.04) < 1e-15);                 // 立ち上げの始点は 0
    double after = 0.0;
    for (std::size_t i = on; i < on + 960; ++i) after += std::fabs(h[i]);
    CHECK(after > 0.0);
    // T30 は始まりがあっても入力の T に戻る。分析は始まり以降だけに掛ける —
    // 直接音と最初の反射の間の**完全な無音**は、RirAnalyzer の分析終了点の
    // 探索 (包絡がノイズ基準を最初に下回った点) がそこで止まってしまい
    // 2k/4k の T30 が無効になる (雑音の無い合成 RIR に固有の別の問題。
    // 実測 RIR では隙間に背景雑音がある)
    RirAnalyzer an;
    const AcousticResult<RirAnalysisResult> res =
        an.analyze(ArrayView<const double>(h.data() + on, h.size() - on), 48000.0);
    CHECK(res.success());
    // 1 つの実現なので許容は ISO 3382-2 の不確かさの 2σ 相当 (125 Hz で ±12%)
    if (res.success())
        for (int b = 0; b < kStatRirBands; ++b) {
            CHECK(res.value().bands[b].metrics.t30.valid);
            CHECK_REL(res.value().bands[b].metrics.t30.value, c.rt60[b], 0.12);
        }
    c.diffuseOnsetSeconds = -0.001;
    CHECK(!synthesizeStatisticalRir(c).success());
}

// (5) 不正な入力
void testInvalid()
{
    StatisticalRirConfig c = baseConfig();
    c.sampleRateHz = 8000.0;                       // 4 kHz 帯 (上端 5.66 kHz) を表せない
    CHECK(!synthesizeStatisticalRir(c).success());
    c = baseConfig();
    c.rt60[3] = 0.0;                               // 拡散音のある帯域で T = 0
    CHECK(!synthesizeStatisticalRir(c).success());
    c = baseConfig();
    c.lateToDirect[0] = -1.0;
    CHECK(!synthesizeStatisticalRir(c).success());
    c = baseConfig();
    c.durationSeconds = 0.01;                      // 直接音 (0.05 s) より短い
    CHECK(!synthesizeStatisticalRir(c).success());
    c = baseConfig();
    c.reflections.push_back(StatRirReflection(-0.001, 0.5));
    CHECK(!synthesizeStatisticalRir(c).success());
}

} // namespace

int main()
{
    testImpulses();
    testDeterminism();
    testDecayTimes();
    testLevels();
    testOnset();
    testInvalid();
    return testutil::summary("test_statrir");
}
