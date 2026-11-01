// Wavefront.cpp — 実光線追跡による波面収差 (OPD) マップと Zernike 展開
#include "optics/Wavefront.h"

#include <algorithm>
#include <cmath>

namespace ofd {
namespace wavefront {

double exitPupilZ(const raytrace::System &sys, bool *ok)
{
    if (ok) *ok = false;
    if (!sys.isValid()) return 0.0;
    const std::vector<raytrace::Surface> &S = sys.surfaces;
    int stop = -1;
    for (std::size_t i = 0; i < S.size(); ++i)
        if (S[i].stop) { stop = int(i); break; }
    if (stop < 0) stop = 0;                      // RayTrace と同じ既定

    // z は直前に通った面の頂点から測った絞りの像の位置。絞りそのものは
    // 頂点にあるので、絞り面での屈折では位置が動かない (s = 0 → s' = 0)
    double z = 0.0;
    for (std::size_t j = std::size_t(stop) + 1; j < S.size(); ++j) {
        const double s = z - S[j - 1].thickness;  // 面 j から見た物体位置
        const double n  = S[j - 1].nAfter;
        const double n2 = S[j].nAfter;
        const double power = (S[j].R != 0.0) ? (n2 - n) / S[j].R : 0.0;
        if (std::fabs(s) < 1e-12) { z = 0.0; continue; }
        const double den = power + n / s;
        if (std::fabs(den) < 1e-12) return 0.0; // 像が無限遠 (テレセントリック)
        z = n2 / den;
    }
    if (ok) *ok = true;
    return z;
}

namespace {

// 像面の交点 Q・方向 D の光線を逆にたどり、中心 C・半径 R の参照球面と
// 射出瞳側で交わる点までの距離 s を返す (光線の進む向きを正)
bool distanceToSphere(const raytrace::RayResult &r, double zImage,
                      double cx, double cy, double zXP, double R, double *s)
{
    const double wx = r.x - cx, wy = r.y - cy, wz = 0.0;  // Q − C (C は像面上)
    const double b = wx * r.L + wy * r.M + wz * r.N;
    const double c = wx * wx + wy * wy + wz * wz - R * R;
    const double disc = b * b - c;
    if (!(disc >= 0.0)) return false;
    const double sq = std::sqrt(disc);
    const double s1 = b + sq, s2 = b - sq;
    // 交点 X = Q − s·D の z が射出瞳に近い方を採る
    const double z1 = zImage - s1 * r.N, z2 = zImage - s2 * r.N;
    *s = (std::fabs(z1 - zXP) <= std::fabs(z2 - zXP)) ? s1 : s2;
    return true;
}

} // namespace

Map wavefrontMap(const raytrace::System &sys, double epd, double field_deg,
                 int n)
{
    Map out;
    if (n < 3 || !sys.isValid()) return out;
    bool ok = false;
    const double zXP = exitPupilZ(sys, &ok);
    if (!ok) return out;
    const double zImage = sys.imageDistance;

    const raytrace::RayResult chief = raytrace::traceRay(sys, epd, field_deg, 0.0, 0.0);
    if (!chief.ok()) return out;
    const double R = std::sqrt(chief.x * chief.x + chief.y * chief.y
                               + (zImage - zXP) * (zImage - zXP));
    if (!(R > 1e-9)) return out;                 // 射出瞳が像面上 (半径 0)
    double sChief = 0.0;
    if (!distanceToSphere(chief, zImage, chief.x, chief.y, zXP, R, &sChief))
        return out;
    const double refChief = chief.oplWave - chief.nImage * sChief;

    out.n = n;
    out.exitPupilZ = zXP;
    out.refRadius = R;
    out.chiefX = chief.x;
    out.chiefY = chief.y;
    for (int iy = 0; iy < n; ++iy) {
        for (int ix = 0; ix < n; ++ix) {
            const double px = -1.0 + 2.0 * ix / (n - 1);
            const double py = -1.0 + 2.0 * iy / (n - 1);
            if (px * px + py * py > 1.0 + 1e-12) continue;
            const raytrace::RayResult r = raytrace::traceRay(sys, epd, field_deg, px, py);
            double s = 0.0;
            if (!r.ok() || !distanceToSphere(r, zImage, chief.x, chief.y, zXP, R, &s)) {
                ++out.failed;
                continue;
            }
            const double ref = r.oplWave - r.nImage * s;
            out.px.push_back(px);
            out.py.push_back(py);
            out.w_mm.push_back(refChief - ref);
            ++out.traced;
        }
    }
    if (out.w_mm.empty()) return out;
    double mean = 0.0, lo = out.w_mm[0], hi = out.w_mm[0];
    for (double w : out.w_mm) { mean += w; lo = std::min(lo, w); hi = std::max(hi, w); }
    mean /= double(out.w_mm.size());
    double ss = 0.0;
    for (double w : out.w_mm) ss += (w - mean) * (w - mean);
    out.pv_mm = hi - lo;
    out.rms_mm = std::sqrt(ss / double(out.w_mm.size()));
    out.valid = true;
    return out;
}

double fringeZernike(int j, double px, double py)
{
    const double r2 = px * px + py * py;
    switch (j) {
    case 1: return 1.0;
    case 2: return px;
    case 3: return py;
    case 4: return 2.0 * r2 - 1.0;
    case 5: return px * px - py * py;              // ρ² cos 2θ
    case 6: return 2.0 * px * py;                  // ρ² sin 2θ
    case 7: return (3.0 * r2 - 2.0) * px;          // (3ρ³ − 2ρ) cos θ
    case 8: return (3.0 * r2 - 2.0) * py;          // (3ρ³ − 2ρ) sin θ
    case 9: return 6.0 * r2 * r2 - 6.0 * r2 + 1.0;
    default: return 0.0;
    }
}

ZernikeFit fitFringe(const std::vector<double> &px,
                     const std::vector<double> &py,
                     const std::vector<double> &w)
{
    ZernikeFit out;
    const int K = kFringeTerms;
    const std::size_t m = w.size();
    if (px.size() != m || py.size() != m || m < std::size_t(K)) return out;

    // 正規方程式 AᵀA c = Aᵀw (9 × 9)。部分ピボットのガウス消去で解く
    double a[kFringeTerms][kFringeTerms + 1] = {};
    double z[kFringeTerms];
    for (std::size_t i = 0; i < m; ++i) {
        for (int k = 0; k < K; ++k) z[k] = fringeZernike(k + 1, px[i], py[i]);
        for (int r = 0; r < K; ++r) {
            for (int c = 0; c < K; ++c) a[r][c] += z[r] * z[c];
            a[r][K] += z[r] * w[i];
        }
    }
    double scale = 0.0;
    for (int r = 0; r < K; ++r) scale = std::max(scale, std::fabs(a[r][r]));
    for (int col = 0; col < K; ++col) {
        int piv = col;
        for (int r = col + 1; r < K; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[piv][col])) piv = r;
        if (!(std::fabs(a[piv][col]) > 1e-12 * scale)) return out;   // 特異
        if (piv != col)
            for (int c = 0; c <= K; ++c) std::swap(a[piv][c], a[col][c]);
        for (int r = col + 1; r < K; ++r) {
            const double f = a[r][col] / a[col][col];
            for (int c = col; c <= K; ++c) a[r][c] -= f * a[col][c];
        }
    }
    out.c.assign(K, 0.0);
    for (int r = K - 1; r >= 0; --r) {
        double v = a[r][K];
        for (int c = r + 1; c < K; ++c) v -= a[r][c] * out.c[c];
        out.c[r] = v / a[r][r];
    }
    double ss = 0.0;
    for (std::size_t i = 0; i < m; ++i) {
        double fit = 0.0;
        for (int k = 0; k < K; ++k) fit += out.c[k] * fringeZernike(k + 1, px[i], py[i]);
        ss += (w[i] - fit) * (w[i] - fit);
    }
    out.residualRms = std::sqrt(ss / double(m));
    out.valid = true;
    return out;
}

} // namespace wavefront
} // namespace ofd
