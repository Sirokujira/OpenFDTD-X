// StatisticalRir.cpp — 統計モデルによる受聴位置の RIR 合成
#include "StatisticalRir.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ArrayView.h"
#include "BandFilter.h"

namespace ofd {
namespace acoustics {

namespace {

const double kPi = 3.14159265358979323846;

// 決定的な一様乱数 (xorshift32、(0, 1])
struct XorShift32 {
    std::uint32_t s;
    explicit XorShift32(unsigned seed) : s(seed ? std::uint32_t(seed) : 0x9E3779B9u) {}
    double uniform()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (double(s) + 1.0) / 4294967296.0;
    }
};

// 標準正規乱数を n 個 (Box–Muller)
std::vector<double> gaussian(std::size_t n, unsigned seed)
{
    XorShift32 r(seed);
    std::vector<double> v(n);
    for (std::size_t i = 0; i < n; i += 2) {
        const double u1 = r.uniform(), u2 = r.uniform();
        const double m = std::sqrt(-2.0 * std::log(u1));
        v[i] = m * std::cos(2.0 * kPi * u2);
        if (i + 1 < n) v[i + 1] = m * std::sin(2.0 * kPi * u2);
    }
    return v;
}

double energy(const std::vector<double> &x)
{
    double e = 0.0;
    for (double v : x) e += v * v;
    return e;
}

} // namespace

AcousticResult<AudioBuffer> synthesizeStatisticalRir(const StatisticalRirConfig &c)
{
    typedef AcousticResult<AudioBuffer> Result;
    const std::vector<Band> bands = makeBands(BandSet::Compat6);
    if (int(bands.size()) != kStatRirBands)
        return Result::error(AcousticErrorCode::InvalidArgument, "unexpected band set");
    if (!(c.sampleRateHz > 2.0 * bands.back().highHz))
        return Result::error(AcousticErrorCode::UnsupportedSampleRate,
                             "sample rate must exceed twice the 4 kHz band edge");
    if (!(c.directDelaySeconds >= 0.0) || !std::isfinite(c.directAmplitude))
        return Result::error(AcousticErrorCode::InvalidArgument,
                             "direct delay must be >= 0 and amplitude finite");
    double tMax = 0.0;
    for (int b = 0; b < kStatRirBands; ++b) {
        if (!(c.lateToDirect[b] >= 0.0) || !std::isfinite(c.lateToDirect[b]))
            return Result::error(AcousticErrorCode::InvalidArgument,
                                 "late-to-direct ratio must be finite and >= 0");
        if (c.lateToDirect[b] > 0.0 && !(c.rt60[b] > 0.0 && std::isfinite(c.rt60[b])))
            return Result::error(AcousticErrorCode::InvalidArgument,
                                 "rt60 must be > 0 in a band with diffuse energy");
        if (c.lateToDirect[b] > 0.0) tMax = std::max(tMax, c.rt60[b]);
    }
    if (!(c.diffuseOnsetSeconds >= 0.0) || !std::isfinite(c.diffuseOnsetSeconds))
        return Result::error(AcousticErrorCode::InvalidArgument,
                             "diffuse onset must be finite and >= 0");
    double lastEvent = c.directDelaySeconds;
    for (const StatRirReflection &r : c.reflections) {
        if (!(r.delaySeconds >= 0.0) || !std::isfinite(r.amplitude))
            return Result::error(AcousticErrorCode::InvalidArgument,
                                 "reflection delay must be >= 0 and amplitude finite");
        lastEvent = std::max(lastEvent, c.directDelaySeconds + r.delaySeconds);
    }
    const double dur = (c.durationSeconds > 0.0)
        ? c.durationSeconds
        : c.directDelaySeconds + 1.5 * tMax + 0.1;
    const double fs = c.sampleRateHz;
    const std::size_t n = std::size_t(std::ceil(dur * fs));
    const std::size_t i0 = std::size_t(std::llround(c.directDelaySeconds * fs));
    if (n == 0 || i0 >= n || std::size_t(std::llround(lastEvent * fs)) >= n)
        return Result::error(AcousticErrorCode::InvalidArgument,
                             "duration is shorter than the direct sound / reflections");

    std::vector<double> h(n, 0.0);
    h[i0] += c.directAmplitude;
    for (const StatRirReflection &r : c.reflections)
        h[i0 + std::size_t(std::llround(r.delaySeconds * fs))] += c.directAmplitude * r.amplitude;

    // 拡散音 (帯域ごと)
    const std::size_t m = n - i0;          // 直接音以降の長さ
    for (int b = 0; b < kStatRirBands; ++b) {
        if (!(c.lateToDirect[b] > 0.0) || c.directAmplitude == 0.0) continue;
        const AcousticResult<BandFilter> f = BandFilter::design(bands[b].lowHz,
                                                                bands[b].highHz, fs);
        if (!f.success())
            return Result::error(AcousticErrorCode::FilterDesignFailed, f.message());
        // 直接音 (単位インパルス) の帯域エネルギー — 分析と同じゼロ位相濾波で測る
        std::vector<double> imp(m, 0.0);
        imp[std::min<std::size_t>(m / 2, std::size_t(fs))] = 1.0;  // 端の影響を避ける位置
        const double eImp = energy(f.value().apply(ArrayView<const double>(imp.data(), m), true));
        const double target = c.lateToDirect[b] * c.directAmplitude * c.directAmplitude * eImp;

        const std::vector<double> noise = gaussian(m, c.seed * 7919u + unsigned(b) * 104729u + 1u);
        std::vector<double> tail = f.value().apply(ArrayView<const double>(noise.data(), m), true);
        const double k = 6.907755278982137 / c.rt60[b];      // ln(10^3): T で −60 dB
        const std::size_t on = std::size_t(std::llround(c.diffuseOnsetSeconds * fs));
        const double fade = 0.005 * fs;                     // 5 ms の立ち上げ
        for (std::size_t i = 0; i < m; ++i) {
            double w = std::exp(-k * double(i) / fs);
            if (i < on) w = 0.0;
            else if (double(i - on) < fade)
                w *= 0.5 * (1.0 - std::cos(kPi * double(i - on) / fade));
            tail[i] *= w;
        }
        // 比は「分析で同じ帯域フィルタを通したとき」に合わせる。tail は既に
        // 帯域制限されているが、分析はもう一度濾波するので、その出力で測る
        // (直接音側の eImp も 1 回濾波した値なので定義が揃う)
        const double e = energy(f.value().apply(ArrayView<const double>(tail.data(), m), true));
        if (!(e > 0.0)) continue;
        const double g = std::sqrt(target / e);
        for (std::size_t i = 0; i < m; ++i) h[i0 + i] += g * tail[i];
    }

    AudioBuffer out;
    out.sampleRateHz = fs;
    out.channels.push_back(h);
    return Result::ok(out);
}

} // namespace acoustics
} // namespace ofd
