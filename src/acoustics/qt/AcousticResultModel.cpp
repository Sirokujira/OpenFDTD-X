// AcousticResultModel.cpp
#include "AcousticResultModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

using namespace ofd;
using namespace ofd::acoustics;

namespace {

struct MetricDef {
    const char *name;
    const char *unit;
    int decimals;
    const MetricValue AcousticMetricsSet::*member;
};

// 表示順: 残響系 → 明瞭度系 → 重心時間
const MetricDef kMetrics[] = {
    { "EDT", "s",  2, &AcousticMetricsSet::edt },
    { "T20", "s",  2, &AcousticMetricsSet::t20 },
    { "T30", "s",  2, &AcousticMetricsSet::t30 },
    { "C50", "dB", 1, &AcousticMetricsSet::c50 },
    { "C80", "dB", 1, &AcousticMetricsSet::c80 },
    { "D50", "-",  2, &AcousticMetricsSet::d50 },
    { "Ts",  "ms", 1, &AcousticMetricsSet::ts },
};
const int kMetricCount = int(sizeof(kMetrics) / sizeof(kMetrics[0]));

// Ts は秒で来るので表示は ms に換算する
double displayValue(const MetricDef &def, double v)
{
    return (QLatin1String(def.name) == QLatin1String("Ts")) ? v * 1000.0 : v;
}

QString bandLabel(const Band &band)
{
    if (band.fullBand) return QStringLiteral("Full band");
    return QString::fromStdString(band.label);
}

QJsonObject metricJson(const MetricValue &m)
{
    return QJsonObject{
        { "value", m.value },
        { "valid", m.valid },
        { "quality", AcousticResultModel::qualityToken(m.quality) },
        { "warning", QString::fromStdString(m.warning) } };
}

QString csvEscape(QString s)
{
    if (s.contains(',') || s.contains('"') || s.contains('\n')) {
        s.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        s = QStringLiteral("\"") + s + QStringLiteral("\"");
    }
    return s;
}

} // namespace

QString AcousticResultModel::qualityToken(AnalysisQuality q)
{
    switch (q) {
    case AnalysisQuality::Valid:   return QStringLiteral("valid");
    case AnalysisQuality::Warning: return QStringLiteral("warning");
    case AnalysisQuality::Invalid: break;
    }
    return QStringLiteral("invalid");
}

QString AcousticResultModel::reflectionBinLabel(double delaySeconds)
{
    const double ms = delaySeconds * 1000.0;
    if (ms < 20.0)  return QStringLiteral("0-20ms");
    if (ms < 80.0)  return QStringLiteral("20-80ms");
    if (ms < 200.0) return QStringLiteral("80-200ms");
    return QStringLiteral("200+ms");
}

QVector<AcousticResultRow>
AcousticResultModel::metricRows(const RirAnalysisResult &result)
{
    QVector<AcousticResultRow> rows;
    rows.reserve(int(result.bands.size()) * kMetricCount);
    for (const BandMetricsResult &bm : result.bands) {
        for (int i = 0; i < kMetricCount; ++i) {
            const MetricDef &def = kMetrics[i];
            const MetricValue &m = bm.metrics.*(def.member);
            AcousticResultRow row;
            row.metric = QLatin1String(def.name);
            row.band = bandLabel(bm.band);
            row.unit = QLatin1String(def.unit);
            if (!bm.filterOk) {
                row.valid = false;
                row.quality = QStringLiteral("invalid");
                row.warning = QString::fromStdString(bm.filterWarning);
            } else {
                row.valid = m.valid;
                row.value = displayValue(def, m.value);
                if (m.valid)
                    row.valueText = QString::number(row.value, 'f', def.decimals);
                row.quality = qualityToken(m.quality);
                row.warning = QString::fromStdString(m.warning);
            }
            rows.push_back(row);
        }
    }
    return rows;
}

QString AcousticResultModel::toCsv(const RirAnalysisResult &result)
{
    QString out;
    out += QStringLiteral("section,metric,band,value,unit,valid,quality,warning\n");

    for (const AcousticResultRow &r : metricRows(result)) {
        out += QStringLiteral("metrics,%1,%2,%3,%4,%5,%6,%7\n")
                   .arg(r.metric, csvEscape(r.band),
                        r.valid ? r.valueText : QString(),
                        r.unit, r.valid ? QStringLiteral("1")
                                        : QStringLiteral("0"),
                        r.quality, csvEscape(r.warning));
    }

    // 直接音・前処理サマリ
    const PreprocessInfo &pp = result.preprocess;
    out += QStringLiteral("summary,direct_time,,%1,s,%2,%3,%4\n")
               .arg(QString::number(result.directSound.timeSeconds, 'f', 6),
                    result.directSound.found ? QStringLiteral("1")
                                             : QStringLiteral("0"),
                    qualityToken(result.directSound.quality),
                    csvEscape(QString::fromStdString(result.directSound.warning)));
    out += QStringLiteral("summary,dynamic_range,,%1,dB,1,,\n")
               .arg(QString::number(pp.dynamicRangeDb, 'f', 1));
    out += QStringLiteral("summary,noise_floor,,%1,dBFS,1,,\n")
               .arg(QString::number(pp.noiseFloorDb, 'f', 1));
    out += QStringLiteral("summary,overall_quality,,%1,,1,%1,\n")
               .arg(qualityToken(result.overallQuality));
    if (result.absoluteSplDb.valid)
        out += QStringLiteral("summary,absolute_spl,,%1,dB SPL,1,%2,\n")
                   .arg(QString::number(result.absoluteSplDb.value, 'f', 1),
                        qualityToken(result.absoluteSplDb.quality));

    // 反射一覧
    out += QStringLiteral(
        "section,index,arrival_ms,delay_ms,relative_level_db,bin,confidence,\n");
    int idx = 1;
    for (const ReflectionEvent &e : result.reflections) {
        out += QStringLiteral("reflections,%1,%2,%3,%4,%5,%6,\n")
                   .arg(QString::number(idx++),
                        QString::number(e.arrivalTime * 1000.0, 'f', 2),
                        QString::number(e.delayFromDirect * 1000.0, 'f', 2),
                        QString::number(e.relativeLevelDb, 'f', 1),
                        reflectionBinLabel(e.delayFromDirect),
                        QString::number(e.confidence, 'f', 2));
    }

    // 警告
    for (const std::string &w : result.warnings)
        out += QStringLiteral("warnings,,,,,,,%1\n")
                   .arg(csvEscape(QString::fromStdString(w)));
    return out;
}

QString AcousticResultModel::toJson(const RirAnalysisResult &result)
{
    QJsonObject root;
    root["overall_quality"] = qualityToken(result.overallQuality);

    const PreprocessInfo &pp = result.preprocess;
    root["preprocess"] = QJsonObject{
        { "sample_count", double(pp.sampleCount) },
        { "duration_s", pp.durationSeconds },
        { "dc_offset", pp.dcOffset },
        { "dc_removed", pp.dcRemoved },
        { "clipping_detected", pp.clippingDetected },
        { "clipped_run_count", pp.clippedRunCount },
        { "peak_dbfs", pp.peakDb },
        { "noise_floor_dbfs", pp.noiseFloorDb },
        { "dynamic_range_db", pp.dynamicRangeDb } };

    const DirectSoundResult &ds = result.directSound;
    root["direct_sound"] = QJsonObject{
        { "found", ds.found },
        { "sample_index", double(ds.sampleIndex) },
        { "time_s", ds.timeSeconds },
        { "level_dbfs", ds.levelDb },
        { "quality", qualityToken(ds.quality) },
        { "warning", QString::fromStdString(ds.warning) } };

    QJsonArray bands;
    for (const BandMetricsResult &bm : result.bands) {
        QJsonObject b;
        b["band"] = bandLabel(bm.band);
        b["center_hz"] = bm.band.centerHz;
        b["filter_ok"] = bm.filterOk;
        if (!bm.filterOk)
            b["filter_warning"] = QString::fromStdString(bm.filterWarning);
        b["edt"] = metricJson(bm.metrics.edt);
        b["t20"] = metricJson(bm.metrics.t20);
        b["t30"] = metricJson(bm.metrics.t30);
        b["c50"] = metricJson(bm.metrics.c50);
        b["c80"] = metricJson(bm.metrics.c80);
        b["d50"] = metricJson(bm.metrics.d50);
        b["ts"]  = metricJson(bm.metrics.ts);
        bands.append(b);
    }
    root["bands"] = bands;

    QJsonArray refl;
    for (const ReflectionEvent &e : result.reflections) {
        refl.append(QJsonObject{
            { "arrival_ms", e.arrivalTime * 1000.0 },
            { "delay_ms", e.delayFromDirect * 1000.0 },
            { "relative_level_db", e.relativeLevelDb },
            { "bin", reflectionBinLabel(e.delayFromDirect) },
            { "confidence", e.confidence } });
    }
    root["reflections"] = refl;

    const ReflectionTimeSummary &rs = result.reflectionSummary;
    static const char *kBinKeys[ReflectionTimeSummary::kBinCount] = {
        "0_20ms", "20_80ms", "80_200ms", "200ms_plus" };
    QJsonObject bins;
    for (int i = 0; i < ReflectionTimeSummary::kBinCount; ++i) {
        bins[kBinKeys[i]] = QJsonObject{
            { "count", rs.counts[i] },
            { "energy", rs.energies[i] },
            { "energy_ratio", rs.energyRatios[i] } };
    }
    root["reflection_summary"] = bins;

    if (result.absoluteSplDb.valid)
        root["absolute_spl_db"] = metricJson(result.absoluteSplDb);

    QJsonArray warnings;
    for (const std::string &w : result.warnings)
        warnings.append(QString::fromStdString(w));
    root["warnings"] = warnings;

    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}
