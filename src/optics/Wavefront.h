// Wavefront.h — 実光線追跡による波面収差 (OPD) マップと Zernike 展開。
// Qt 非依存 / C++17。LensEditorTab の「Wavefront map (Zernike)」の計算実体。
//
// ── 定義 (W. T. Welford, "Aberrations of Optical Systems" §6 と同じ) ──────
// 波面収差 W は、射出瞳に置いた**参照球面**から実際の波面までを光線に沿って
// 測った光路長。参照球面は
//   中心 = 主光線 (瞳中心の光線) が像面と交わる点
//   半径 = 射出瞳の中心からその点までの距離
// にとる (主光線基準なので傾きは入らず、像面の位置ずれは焦点ずれとして出る)。
// 射出瞳は絞りを後ろの面で近軸結像した位置 (入射瞳と同じ近軸の扱い)。
//
// 各光線について、像面の交点 Q から方向 D を逆にたどって参照球面との交点
// までの距離 s を閉形式で解き、物体側の波面から参照球面までの光路長
//   OPL_ref = OPL(波面 → 像面) − n'·s
// を作る。W = OPL_ref(主光線) − OPL_ref(光線) (光線が参照球面に早く着く
// = 波面が参照球面より進んでいるときに正)。この符号で 3 次収差の
// W040 = S_I/8・W131 = S_II/2 (SeidelAberration と同じ Welford の正規化) に
// 一致する — selftest がこの対応を独立な閉形式として突き合わせている。
//
// ── 既知の近似 ───────────────────────────────────────────────────────────
// 瞳は近軸 (RayTrace と同じく real ray aiming をしない)。視野が大きく瞳収差の
// 強い系では瞳のサンプル位置がわずかに歪む。射出瞳が無限遠 (像側テレ
// セントリック) の系は参照球面の半径が決まらないので扱わない (valid = false)。
#ifndef OFD_OPTICS_WAVEFRONT_H
#define OFD_OPTICS_WAVEFRONT_H

#include <vector>

#include "RayTrace.h"

namespace ofd {
namespace wavefront {

// 射出瞳の位置 (最終面の頂点から [mm]、+ = 後ろ)。絞りを後ろの面で近軸
// 結像する。射出瞳が無限遠なら *ok = false。
double exitPupilZ(const raytrace::System &sys, bool *ok);

struct Map {
    bool   valid = false;
    int    n = 0;                    // 瞳の格子 n × n (正規化瞳座標 −1 … +1)
    std::vector<double> px, py;      // 瞳内で追跡できた点の座標
    std::vector<double> w_mm;        // その点の W [mm]
    int    traced = 0, failed = 0;   // 瞳内の点のうち追跡できた / できなかった数
    double exitPupilZ = 0.0;         // [mm] (最終面頂点から)
    double refRadius = 0.0;          // 参照球面の半径 [mm]
    double chiefX = 0.0, chiefY = 0.0;
    double pv_mm = 0.0;              // 峰-谷
    double rms_mm = 0.0;             // 平均 (ピストン) を除いた RMS
};

// 視野半角 field_deg (y 方向) の波面。n は格子の一辺の点数 (>= 3)。
Map wavefrontMap(const raytrace::System &sys, double epd, double field_deg,
                 int n);

// Fringe (アリゾナ大) Zernike 多項式 Z_j (j = 1 … 9)。θ は x 軸から測る。
//   Z1 = 1, Z2 = ρcosθ, Z3 = ρsinθ, Z4 = 2ρ² − 1, Z5 = ρ²cos2θ,
//   Z6 = ρ²sin2θ, Z7 = (3ρ³ − 2ρ)cosθ, Z8 = (3ρ³ − 2ρ)sinθ,
//   Z9 = 6ρ⁴ − 6ρ² + 1
// 3 次収差との対応: W040 = 6·Z9 (球面)、W131 = 3·Z8 (y 方向のコマ)。
double fringeZernike(int j, double px, double py);
constexpr int kFringeTerms = 9;

struct ZernikeFit {
    bool   valid = false;
    std::vector<double> c;           // c[0] = Z1 … c[8] = Z9 (W と同じ単位)
    double residualRms = 0.0;        // 9 項で表せない残り (高次収差) の RMS
};

// 最小二乗 (正規方程式)。点が項数より少ない・特異なら valid = false。
ZernikeFit fitFringe(const std::vector<double> &px,
                     const std::vector<double> &py,
                     const std::vector<double> &w);

} // namespace wavefront
} // namespace ofd

#endif // OFD_OPTICS_WAVEFRONT_H
