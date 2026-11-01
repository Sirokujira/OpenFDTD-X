#include "RcwaEfficiency.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace ofd {
namespace rcwa {

double Efficiency::worstEnergyError() const
{
    double worst = 0.0;
    for (const EfficiencyPoint &p : points) {
        worst = std::max(worst, std::fabs(p.rTE + p.tTE - 1.0));
        worst = std::max(worst, std::fabs(p.rTM + p.tTM - 1.0));
    }
    return worst;
}

double Efficiency::worstEnergyFreqHz() const
{
    double worst = -1.0, at = 0.0;
    for (const EfficiencyPoint &p : points) {
        const double e = std::max(std::fabs(p.rTE + p.tTE - 1.0),
                                  std::fabs(p.rTM + p.tTM - 1.0));
        if (e > worst) { worst = e; at = p.freqHz; }
    }
    return (worst >= 0.0) ? at : 0.0;
}

Efficiency parse(const QString &text)
{
    Efficiency out;
    static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = line.split(sep, Qt::SkipEmptyParts);
        if (parts.size() < 6) continue;               // 見出し行もここで落ちる
        double v[6] = { 0, 0, 0, 0, 0, 0 };
        bool all = true;
        for (int k = 0; k < 6 && all; ++k) {
            bool ok = false;
            v[k] = parts[k].toDouble(&ok);
            if (!ok) all = false;
        }
        if (!all) continue;                           // 数字でない行は捨てる
        EfficiencyPoint p;
        p.freqHz = v[0];
        p.lambda_m = v[1];
        p.rTE = v[2];
        p.tTE = v[3];
        p.rTM = v[4];
        p.tTM = v[5];
        out.points.push_back(p);
    }
    return out;
}

Efficiency read(const QString &csvPath)
{
    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return Efficiency();
    QTextStream ts(&f);
    return parse(ts.readAll());
}

double Orders::worstSumError(const Efficiency &total) const
{
    double worst = 0.0;
    for (const EfficiencyPoint &p : total.points) {
        // 周波数は CSV の同じ %.8e 表記から読むので、相対 1e-9 で同一とみなす
        double r[2] = { 0, 0 }, t[2] = { 0, 0 };
        bool any = false;
        for (const OrderPoint &o : points) {
            if (std::fabs(o.freqHz - p.freqHz) > 1e-9 * std::fabs(p.freqHz)) continue;
            any = true;
            r[o.te ? 0 : 1] += o.r;
            t[o.te ? 0 : 1] += o.t;
        }
        if (!any) continue;
        worst = std::max({ worst, std::fabs(r[0] - p.rTE), std::fabs(t[0] - p.tTE),
                           std::fabs(r[1] - p.rTM), std::fabs(t[1] - p.tTM) });
    }
    return worst;
}

bool Orders::strongestNonSpecular(OrderPoint *out) const
{
    bool found = false;
    double best = -1.0;
    for (const OrderPoint &o : points) {
        if (o.m == 0 && o.n == 0) continue;
        const double v = std::max(o.r, o.t);
        if (v > best) { best = v; found = true; if (out) *out = o; }
    }
    return found;
}

Orders parseOrders(const QString &text)
{
    Orders out;
    static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList parts = line.split(sep, Qt::SkipEmptyParts);
        if (parts.size() < 7) continue;
        const QString pol = parts[2].trimmed().toUpper();
        if (pol != QLatin1String("TE") && pol != QLatin1String("TM")) continue;   // 見出し行
        bool ok[6] = { false, false, false, false, false, false };
        OrderPoint p;
        p.freqHz = parts[0].toDouble(&ok[0]);
        p.lambda_m = parts[1].toDouble(&ok[1]);
        p.te = (pol == QLatin1String("TE"));
        p.m = parts[3].toInt(&ok[2]);
        p.n = parts[4].toInt(&ok[3]);
        p.r = parts[5].toDouble(&ok[4]);
        p.t = parts[6].toDouble(&ok[5]);
        if (!(ok[0] && ok[1] && ok[2] && ok[3] && ok[4] && ok[5])) continue;
        out.points.push_back(p);
    }
    return out;
}

Orders readOrders(const QString &csvPath)
{
    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return Orders();
    QTextStream ts(&f);
    return parseOrders(ts.readAll());
}

} // namespace rcwa
} // namespace ofd
