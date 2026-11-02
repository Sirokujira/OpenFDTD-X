// openfdtd_x_acoustics.cpp — C API 実装。C++14 で RirAnalyzer に委譲する。
// C++ の例外・型は境界を越えない: 例外はすべて捕捉して INTERNAL_ERROR に
// 変換し、エラーメッセージは context 内の固定長 char バッファへ複写する。
#include "openfdtd_x_acoustics.h"

#include <cstddef> /* offsetof */
#include <cstdio>
#include <cstring>
#include <new>

#include "../core/AcousticError.h"
#include "../core/AnalysisQuality.h"
#include "../core/ArrayView.h"
#include "../core/RirAnalyzer.h"

namespace {

const std::size_t kLastErrorCapacity = 512;

} // namespace

// 不透明な結果ハンドルの実体。ヘッダには一切露出しない。
// コアの結果をそのまま保持し、アクセサが C の型へ写して返す。
struct ofdx_ac_result {
    ofd::acoustics::RirAnalysisResult res;
};

// 不透明コンテキストの実体。ヘッダには一切露出しない。
struct ofdx_ac_context {
    char lastError[kLastErrorCapacity];

    ofdx_ac_context() { lastError[0] = '\0'; }

    void clearError() { lastError[0] = '\0'; }

    void setError(const char *message) {
        if (message == nullptr) message = "";
        std::snprintf(lastError, kLastErrorCapacity, "%s", message);
    }
};

namespace {

using ofd::acoustics::AcousticErrorCode;
using ofd::acoustics::AnalysisQuality;
using ofd::acoustics::MetricValue;

// コアのエラーコード → C API のエラーコード
ofdx_ac_error mapErrorCode(AcousticErrorCode code) {
    switch (code) {
    case AcousticErrorCode::Ok:
        return OFDX_AC_SUCCESS;
    case AcousticErrorCode::InvalidArgument:
        return OFDX_AC_INVALID_ARGUMENT;
    case AcousticErrorCode::EmptyInput:
    case AcousticErrorCode::InputTooShort:
    case AcousticErrorCode::NonFiniteSample:
    case AcousticErrorCode::ClippingDetected:
    case AcousticErrorCode::UnsupportedSampleRate:
    case AcousticErrorCode::UnsupportedFormat:
        return OFDX_AC_INVALID_AUDIO;
    case AcousticErrorCode::DirectSoundNotFound:
        return OFDX_AC_DIRECT_SOUND_NOT_FOUND;
    case AcousticErrorCode::InsufficientDynamicRange:
    case AcousticErrorCode::NoiseFloorTooHigh:
        return OFDX_AC_INSUFFICIENT_DYNAMIC_RANGE;
    case AcousticErrorCode::RegressionFailed:
    case AcousticErrorCode::FilterDesignFailed:
        return OFDX_AC_NUMERICAL_FAILURE;
    default:
        return OFDX_AC_INTERNAL_ERROR;
    }
}

int mapQuality(AnalysisQuality q) {
    switch (q) {
    case AnalysisQuality::Valid:   return OFDX_AC_QUALITY_VALID;
    case AnalysisQuality::Warning: return OFDX_AC_QUALITY_WARNING;
    default:                       return OFDX_AC_QUALITY_INVALID;
    }
}

ofdx_ac_metric mapMetric(const MetricValue &m) {
    ofdx_ac_metric out;
    out.value = m.value;
    out.valid = m.valid ? 1 : 0;
    out.quality = mapQuality(m.quality);
    return out;
}

// バージョン 1 の ofdx_ac_metrics のサイズ。末尾追加のみの規約により、
// 旧レイアウトの終端 = 最初の追加フィールドのオフセットに一致する。
const std::size_t kMetricsV1Size = offsetof(ofdx_ac_metrics, early_late_50);

// struct_size / api_version による前方互換検査。
// バージョン 2 では v2 レイアウト (現行 sizeof) に加え、バージョン 1 の
// 呼び出し側 (api_version = 1, 旧 struct_size) も受理する。書き込みは
// writeMetrics() が struct_size を見て旧レイアウト分に留める。
bool checkAbi(const ofdx_ac_metrics *m, ofdx_ac_context *ctx) {
    if (m->api_version != 1u && m->api_version != OFDX_AC_API_VERSION) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "api_version mismatch: caller=%u, library=%u",
                      static_cast<unsigned>(m->api_version),
                      static_cast<unsigned>(OFDX_AC_API_VERSION));
        ctx->setError(buf);
        return false;
    }
    const std::size_t expected = (m->api_version == 1u)
                                     ? kMetricsV1Size
                                     : sizeof(ofdx_ac_metrics);
    if (m->struct_size != expected) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "struct_size mismatch: caller=%u, library=%u",
                      static_cast<unsigned>(m->struct_size),
                      static_cast<unsigned>(expected));
        ctx->setError(buf);
        return false;
    }
    return true;
}

// 指標セットを out へ書く。バージョン 1 レイアウト (struct_size が旧サイズ)
// には v1 の 7 指標のみ書き、追加フィールドには触れない。
void writeMetrics(ofdx_ac_metrics *out,
                  const ofd::acoustics::AcousticMetricsSet &m) {
    out->edt = mapMetric(m.edt);
    out->t20 = mapMetric(m.t20);
    out->t30 = mapMetric(m.t30);
    out->c50 = mapMetric(m.c50);
    out->c80 = mapMetric(m.c80);
    out->d50 = mapMetric(m.d50);
    out->center_time = mapMetric(m.ts);
    if (out->struct_size >= sizeof(ofdx_ac_metrics)) {
        out->early_late_50 = mapMetric(m.earlyLate50);
        out->early_late_80 = mapMetric(m.earlyLate80);
        out->st_early = mapMetric(m.stEarly);
        out->st_late = mapMetric(m.stLate);
    }
}

// 分析本体 (例外はここから漏れ得るので呼び出し側の try で受ける)
ofdx_ac_error analyzeImpl(ofdx_ac_context *ctx,
                          const ofdx_ac_audio_view *audio,
                          ofdx_ac_metrics *out) {
    using namespace ofd::acoustics;

    // 広帯域 (フィルタなし) の指標のみを返す構成
    RirAnalyzerConfig cfg;
    cfg.bandSet = BandSet::FullBandOnly;
    RirAnalyzer analyzer(cfg);

    AcousticResult<RirAnalysisResult> r = analyzer.analyze(
        ArrayView<const double>(audio->samples, audio->sample_count),
        audio->sample_rate_hz);
    if (!r.success()) {
        ctx->setError(r.message().c_str());
        ofdx_ac_error e = mapErrorCode(r.errorCode());
        return (e == OFDX_AC_SUCCESS) ? OFDX_AC_INTERNAL_ERROR : e;
    }

    const RirAnalysisResult &res = r.value();
    if (res.bands.empty()) {
        ctx->setError("internal: analysis produced no band results");
        return OFDX_AC_INTERNAL_ERROR;
    }
    writeMetrics(out, res.bands[0].metrics);
    return OFDX_AC_SUCCESS;
}

// 帯域別分析の本体 (例外は呼び出し側の try で受ける)
ofdx_ac_error analyzeFullImpl(ofdx_ac_context *ctx,
                              const ofdx_ac_audio_view *audio,
                              int bandSetValue,
                              ofdx_ac_result **outResult) {
    using namespace ofd::acoustics;

    RirAnalyzerConfig cfg;
    switch (bandSetValue) {
    case OFDX_AC_BANDS_COMPAT6:        cfg.bandSet = BandSet::Compat6; break;
    case OFDX_AC_BANDS_FULL_ONLY:      cfg.bandSet = BandSet::FullBandOnly; break;
    case OFDX_AC_BANDS_OCTAVE_63_8K:   cfg.bandSet = BandSet::Octave63To8k; break;
    case OFDX_AC_BANDS_THIRD_100_5K:   cfg.bandSet = BandSet::ThirdOctave100To5k; break;
    case OFDX_AC_BANDS_SINGER_FORMANT: cfg.bandSet = BandSet::SingerFormant; break;
    default: {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "unknown band_set: %d", bandSetValue);
        ctx->setError(buf);
        return OFDX_AC_INVALID_ARGUMENT;
    }
    }
    RirAnalyzer analyzer(cfg);

    AcousticResult<RirAnalysisResult> r = analyzer.analyze(
        ArrayView<const double>(audio->samples, audio->sample_count),
        audio->sample_rate_hz);
    if (!r.success()) {
        ctx->setError(r.message().c_str());
        ofdx_ac_error e = mapErrorCode(r.errorCode());
        return (e == OFDX_AC_SUCCESS) ? OFDX_AC_INTERNAL_ERROR : e;
    }

    ofdx_ac_result *result = new (std::nothrow) ofdx_ac_result();
    if (result == nullptr) {
        ctx->setError("out of memory allocating result");
        return OFDX_AC_INTERNAL_ERROR;
    }
    result->res = r.value();
    *outResult = result;
    return OFDX_AC_SUCCESS;
}

} // namespace

extern "C" {

ofdx_ac_context *ofdx_ac_context_create(void) {
    return new (std::nothrow) ofdx_ac_context();
}

void ofdx_ac_context_destroy(ofdx_ac_context *ctx) {
    delete ctx;
}

ofdx_ac_error ofdx_ac_analyze_rir(ofdx_ac_context *ctx,
                                  const ofdx_ac_audio_view *audio,
                                  ofdx_ac_metrics *out_metrics) {
    if (ctx == nullptr) return OFDX_AC_INVALID_ARGUMENT;
    ctx->clearError();
    try {
        if (audio == nullptr) {
            ctx->setError("audio view is NULL");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        if (out_metrics == nullptr) {
            ctx->setError("out_metrics is NULL");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        if (!checkAbi(out_metrics, ctx)) return OFDX_AC_INVALID_ARGUMENT;
        if (audio->samples == nullptr || audio->sample_count == 0) {
            ctx->setError("audio samples are NULL or empty");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        if (!(audio->sample_rate_hz > 0.0)) {
            ctx->setError("sample_rate_hz must be > 0");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        return analyzeImpl(ctx, audio, out_metrics);
    } catch (const std::exception &e) {
        ctx->setError(e.what());
        return OFDX_AC_INTERNAL_ERROR;
    } catch (...) {
        ctx->setError("unknown exception in acoustic core");
        return OFDX_AC_INTERNAL_ERROR;
    }
}

const char *ofdx_ac_last_error(const ofdx_ac_context *ctx) {
    if (ctx == nullptr) return "";
    return ctx->lastError;
}

/* ── api_version 2: 帯域別分析 ─────────────────────────────────────────── */

ofdx_ac_error ofdx_ac_analyze_rir_full(ofdx_ac_context *ctx,
                                       const ofdx_ac_audio_view *audio,
                                       const ofdx_ac_analysis_config *config,
                                       ofdx_ac_result **out_result) {
    if (ctx == nullptr) return OFDX_AC_INVALID_ARGUMENT;
    ctx->clearError();
    try {
        if (out_result == nullptr) {
            ctx->setError("out_result is NULL");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        *out_result = nullptr;
        if (audio == nullptr) {
            ctx->setError("audio view is NULL");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        int bandSetValue = OFDX_AC_BANDS_COMPAT6;
        if (config != nullptr) {
            if (config->struct_size != sizeof(ofdx_ac_analysis_config)) {
                char buf[128];
                std::snprintf(
                    buf, sizeof(buf),
                    "config struct_size mismatch: caller=%u, library=%u",
                    static_cast<unsigned>(config->struct_size),
                    static_cast<unsigned>(sizeof(ofdx_ac_analysis_config)));
                ctx->setError(buf);
                return OFDX_AC_INVALID_ARGUMENT;
            }
            bandSetValue = config->band_set;
        }
        if (audio->samples == nullptr || audio->sample_count == 0) {
            ctx->setError("audio samples are NULL or empty");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        if (!(audio->sample_rate_hz > 0.0)) {
            ctx->setError("sample_rate_hz must be > 0");
            return OFDX_AC_INVALID_ARGUMENT;
        }
        return analyzeFullImpl(ctx, audio, bandSetValue, out_result);
    } catch (const std::exception &e) {
        ctx->setError(e.what());
        return OFDX_AC_INTERNAL_ERROR;
    } catch (...) {
        ctx->setError("unknown exception in acoustic core");
        return OFDX_AC_INTERNAL_ERROR;
    }
}

void ofdx_ac_result_destroy(ofdx_ac_result *result) {
    delete result;
}

size_t ofdx_ac_result_band_count(const ofdx_ac_result *result) {
    if (result == nullptr) return 0;
    return result->res.bands.size();
}

ofdx_ac_error ofdx_ac_result_band_info(const ofdx_ac_result *result,
                                       size_t index,
                                       ofdx_ac_band_info *out_info) {
    if (result == nullptr || out_info == nullptr)
        return OFDX_AC_INVALID_ARGUMENT;
    if (out_info->struct_size != sizeof(ofdx_ac_band_info))
        return OFDX_AC_INVALID_ARGUMENT;
    if (index >= result->res.bands.size()) return OFDX_AC_INVALID_ARGUMENT;
    const ofd::acoustics::BandMetricsResult &b = result->res.bands[index];
    out_info->center_hz = b.band.centerHz;
    out_info->low_hz = b.band.lowHz;
    out_info->high_hz = b.band.highHz;
    out_info->is_full_band = b.band.fullBand ? 1 : 0;
    out_info->filter_ok = b.filterOk ? 1 : 0;
    out_info->noise_ok = b.noiseOk ? 1 : 0;
    out_info->peak_db = b.peakDb;
    out_info->noise_floor_db = b.noiseFloorDb;
    out_info->inr_db = b.inrDb;
    return OFDX_AC_SUCCESS;
}

const char *ofdx_ac_result_band_label(const ofdx_ac_result *result,
                                      size_t index) {
    if (result == nullptr || index >= result->res.bands.size()) return "";
    return result->res.bands[index].band.label.c_str();
}

ofdx_ac_error ofdx_ac_result_band_metrics(const ofdx_ac_result *result,
                                          size_t index,
                                          ofdx_ac_metrics *out_metrics) {
    if (result == nullptr || out_metrics == nullptr)
        return OFDX_AC_INVALID_ARGUMENT;
    // ofdx_ac_analyze_rir と同じ ABI 検査。ここには ctx が無いので
    // メッセージは書けない (戻り値のみ)。
    if (out_metrics->api_version != 1u &&
        out_metrics->api_version != OFDX_AC_API_VERSION)
        return OFDX_AC_INVALID_ARGUMENT;
    const size_t expected = (out_metrics->api_version == 1u)
                                ? kMetricsV1Size
                                : sizeof(ofdx_ac_metrics);
    if (out_metrics->struct_size != expected) return OFDX_AC_INVALID_ARGUMENT;
    if (index >= result->res.bands.size()) return OFDX_AC_INVALID_ARGUMENT;
    writeMetrics(out_metrics, result->res.bands[index].metrics);
    return OFDX_AC_SUCCESS;
}

size_t ofdx_ac_result_reflection_count(const ofdx_ac_result *result) {
    if (result == nullptr) return 0;
    return result->res.reflections.size();
}

ofdx_ac_error ofdx_ac_result_reflection(const ofdx_ac_result *result,
                                        size_t index,
                                        ofdx_ac_reflection *out_reflection) {
    if (result == nullptr || out_reflection == nullptr)
        return OFDX_AC_INVALID_ARGUMENT;
    if (out_reflection->struct_size != sizeof(ofdx_ac_reflection))
        return OFDX_AC_INVALID_ARGUMENT;
    if (index >= result->res.reflections.size())
        return OFDX_AC_INVALID_ARGUMENT;
    const ofd::acoustics::ReflectionEvent &e = result->res.reflections[index];
    out_reflection->arrival_time_s = e.arrivalTime;
    out_reflection->delay_from_direct_s = e.delayFromDirect;
    out_reflection->relative_level_db = e.relativeLevelDb;
    out_reflection->energy = e.energy;
    out_reflection->confidence = e.confidence;
    out_reflection->band_index = e.bandIndex;
    return OFDX_AC_SUCCESS;
}

size_t ofdx_ac_result_warning_count(const ofdx_ac_result *result) {
    if (result == nullptr) return 0;
    return result->res.warnings.size();
}

const char *ofdx_ac_result_warning(const ofdx_ac_result *result,
                                   size_t index) {
    if (result == nullptr || index >= result->res.warnings.size()) return "";
    return result->res.warnings[index].c_str();
}

int ofdx_ac_result_overall_quality(const ofdx_ac_result *result) {
    if (result == nullptr) return OFDX_AC_QUALITY_INVALID;
    return mapQuality(result->res.overallQuality);
}

} // extern "C"
