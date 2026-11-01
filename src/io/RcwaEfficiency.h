// RcwaEfficiency.h — orcwa が出す rcwa_efficiency.csv を読む。
//
// 書式はカーネル側 (`OpenRCWA/sol/rcwa_bridge.cpp`) の書き出しが正:
//
//     frequency[Hz],lambda[m],R_TE,T_TE,R_TM,T_TM
//
// R / T は**電力の反射率・透過率** (回折次数を全部足したもの) で、無次元の
// 0..1。dB ではない。
//
// **給電点の反射 Ref[dB] とは別物である。** `<kernel>.log` の Ref[dB] は
// 給電点から見た整合の良さ (ポート反射) で、こちらは平面波の電力反射率。
// 定義も規格化も違うので、**この 2 つを突き合わせてはいけない**。
#pragma once
#include <QString>
#include <QVector>

namespace ofd {
namespace rcwa {

struct EfficiencyPoint {
    double freqHz = 0.0, lambda_m = 0.0;
    double rTE = 0.0, tTE = 0.0, rTM = 0.0, tTM = 0.0;
};

struct Efficiency {
    QVector<EfficiencyPoint> points;
    bool valid() const { return points.size() >= 1; }
    // 無損失なら R + T = 1。**最大のずれ**を返す (偏波の悪い方)。
    // 点が無ければ 0。
    double worstEnergyError() const;
    // そのずれが起きた周波数 [Hz] (点が無ければ 0)
    double worstEnergyFreqHz() const;
};

// ファイルから読む (無ければ空)。見出し行は読み飛ばし、数値が 6 個そろって
// いない行は**捨てる** (足りない列を 0 で埋めない)。
Efficiency read(const QString &csvPath);
Efficiency parse(const QString &text);

// ── 回折次数ごとの効率 (rcwa_orders.csv) ──────────────────────────────────
// 書式はカーネル側 (`OpenRCWA/sol/rcwa_bridge.cpp`) の書き出しが正:
//
//     frequency[Hz],lambda[m],pol,m,n,R,T
//
// 入射側か透過側で伝搬する次数 (m, n) だけが 1 行ずつ並ぶ (伝搬しない側は 0)。
// pol は "TE" / "TM"。同じ周波数・偏波の行の和が rcwa_efficiency.csv の R / T。
struct OrderPoint {
    double freqHz = 0.0, lambda_m = 0.0;
    bool   te = true;              // false = TM
    int    m = 0, n = 0;
    double r = 0.0, t = 0.0;
};

struct Orders {
    QVector<OrderPoint> points;
    bool valid() const { return points.size() >= 1; }
    // 次数の和と合計 (rcwa_efficiency.csv) の最大のずれ。合計側に無い周波数は
    // 比べない。どちらかが空なら 0。
    double worstSumError(const Efficiency &total) const;
    // 0 次以外で効率 (R か T) が最大の次数。無ければ false。
    bool strongestNonSpecular(OrderPoint *out) const;
};

// 見出し行・列の欠けた行・pol が TE/TM でない行は捨てる。
Orders readOrders(const QString &csvPath);
Orders parseOrders(const QString &text);

} // namespace rcwa
} // namespace ofd
