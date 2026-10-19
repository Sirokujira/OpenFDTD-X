/* test_c_api.c — C API (openfdtd_x_acoustics.h) の純 C (C99) テスト。
 *
 * このファイルは意図的に C としてコンパイルする (gcc -std=c99 相当)。
 * ヘッダが C++ の構文 (クラス / テンプレート / 例外 / STL) を露出して
 * いないこと自体がこのテストの検証項目の一つである。
 *
 * 内容:
 *   1. 人工減衰列 (RT60 = 1.0 s) を C 側で生成 → analyze → 指標妥当性
 *   2. NULL 引数のエラー処理
 *   3. struct_size / api_version 不一致の前方互換検査
 */
#include <math.h>
#include <stddef.h> /* offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/acoustics/c_api/openfdtd_x_acoustics.h"

#ifdef __cplusplus
#error "test_c_api.c must be compiled as C, not C++"
#endif

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                \
    do {                                                                     \
        ++g_checks;                                                          \
        double va_ = (a), vb_ = (b), vt_ = (tol);                            \
        if (!(fabs(va_ - vb_) <= vt_)) {                                     \
            ++g_failures;                                                    \
            printf("FAIL %s:%d: %s ~= %s (%.10g vs %.10g, tol %.3g)\n",      \
                   __FILE__, __LINE__, #a, #b, va_, vb_, vt_);               \
        }                                                                    \
    } while (0)

/* 決定的 LCG (tests/acoustics/test_common.h と同一係数) */
static unsigned g_lcg_state = 20260716u;
static double lcg_uniform(void) {
    g_lcg_state = g_lcg_state * 1664525u + 1013904223u;
    return ((double)(g_lcg_state >> 8) / 8388608.0) - 1.0;
}

/* 人工 RIR: 直接音デルタ + 指数減衰白色雑音 (RT 秒で -60 dB)。
 * tests/generators/generate_synthetic_rir.cpp と同モデル。 */
static double *make_synthetic_rir(double rt60, double fs, double duration_s,
                                  size_t direct_index, size_t *out_count) {
    size_t n = (size_t)(duration_s * fs + 0.5);
    double *h = (double *)calloc(n, sizeof(double));
    size_t i;
    if (h == NULL) {
        *out_count = 0;
        return NULL;
    }
    /* 減衰白色雑音 (初期 RMS 0.3; 一様乱数の RMS は 1/sqrt(3)) */
    {
        double k = 6.91 / (rt60 * fs);
        double amp = 0.3 * sqrt(3.0);
        for (i = direct_index; i < n; ++i) {
            h[i] += amp * lcg_uniform() * exp(-k * (double)(i - direct_index));
        }
    }
    /* 直接音デルタ */
    if (direct_index < n) h[direct_index] += 1.0;
    *out_count = n;
    return h;
}

static void test_analyze_valid(void) {
    const double rt60 = 1.0;
    const double fs = 48000.0;
    size_t count = 0;
    double *samples = make_synthetic_rir(rt60, fs, 1.8, 480 /* 10 ms */, &count);
    ofdx_ac_context *ctx;
    ofdx_ac_audio_view view;
    ofdx_ac_metrics metrics;
    ofdx_ac_error err;

    CHECK(samples != NULL);
    if (samples == NULL) return;

    ctx = ofdx_ac_context_create();
    CHECK(ctx != NULL);
    if (ctx == NULL) {
        free(samples);
        return;
    }

    view.samples = samples;
    view.sample_count = count;
    view.sample_rate_hz = fs;

    memset(&metrics, 0, sizeof(metrics));
    metrics.struct_size = sizeof(ofdx_ac_metrics);
    metrics.api_version = OFDX_AC_API_VERSION;

    err = ofdx_ac_analyze_rir(ctx, &view, &metrics);
    CHECK(err == OFDX_AC_SUCCESS);
    if (err == OFDX_AC_SUCCESS) {
        /* 残響時間: RT60 = 1.0 s に対し ±0.1 s 以内 */
        CHECK(metrics.t30.valid);
        CHECK_NEAR(metrics.t30.value, rt60, 0.1);
        CHECK(metrics.t20.valid);
        CHECK_NEAR(metrics.t20.value, rt60, 0.1);
        CHECK(metrics.edt.valid);
        CHECK_NEAR(metrics.edt.value, rt60, 0.1);
        CHECK(metrics.t30.quality == OFDX_AC_QUALITY_VALID ||
              metrics.t30.quality == OFDX_AC_QUALITY_WARNING);

        /* 明瞭度 / Definition / 重心時間: 有効かつ物理的に妥当な範囲 */
        CHECK(metrics.c50.valid);
        CHECK(metrics.c80.valid);
        CHECK(metrics.c80.value > metrics.c50.value); /* C80 > C50 (常に) */
        CHECK(metrics.d50.valid);
        CHECK(metrics.d50.value > 0.0 && metrics.d50.value < 1.0);
        CHECK(metrics.center_time.valid);
        CHECK(metrics.center_time.value > 0.0 &&
              metrics.center_time.value < rt60);

        /* 理論値: エネルギーは -60 dB/RT で減衰するので
         * D50 = 1 - 10^(-6*0.05/RT), Ts = RT/13.82 */
        CHECK_NEAR(metrics.d50.value, 1.0 - pow(10.0, -6.0 * 0.05 / rt60), 0.05);
        CHECK_NEAR(metrics.center_time.value, rt60 / 13.82, 0.02);

        /* ── api_version 2 の追加指標 ── */
        /* 早期/後期比は D50 と同じエネルギー分割から出るので
         * el50 = D50 / (1 - D50) の恒等式が成り立つ */
        CHECK(metrics.early_late_50.valid);
        CHECK_NEAR(metrics.early_late_50.value,
                   metrics.d50.value / (1.0 - metrics.d50.value), 1.0e-6);
        CHECK(metrics.early_late_80.valid);
        CHECK(metrics.early_late_80.value > metrics.early_late_50.value);
        /* ST: 1.8 s の RIR なら両方の窓 (〜100ms / 〜1000ms) が収まる。
         * 単調減衰では後期窓のエネルギーが少ないので ST_late < ST_early */
        CHECK(metrics.st_early.valid);
        CHECK(metrics.st_late.valid);
        CHECK(metrics.st_late.value < metrics.st_early.value);

        /* 成功時は last_error が空 */
        CHECK(strlen(ofdx_ac_last_error(ctx)) == 0);

        printf("  EDT=%.4f T20=%.4f T30=%.4f C50=%.2f C80=%.2f "
               "D50=%.3f Ts=%.4f EL50=%.3f ST_e=%.2f ST_l=%.2f\n",
               metrics.edt.value, metrics.t20.value, metrics.t30.value,
               metrics.c50.value, metrics.c80.value, metrics.d50.value,
               metrics.center_time.value, metrics.early_late_50.value,
               metrics.st_early.value, metrics.st_late.value);
    } else {
        printf("  analyze failed: %s\n", ofdx_ac_last_error(ctx));
    }

    /* ── バージョン 1 呼び出し側の後方互換 ──
     * api_version = 1 + 旧 struct_size (= 最初の追加フィールドのオフセット)
     * で受理され、追加フィールドには 1 バイトも書かれないこと。 */
    {
        ofdx_ac_metrics m1;
        memset(&m1, 0, sizeof(m1));
        m1.struct_size = offsetof(ofdx_ac_metrics, early_late_50);
        m1.api_version = 1u;
        m1.early_late_50.value = -777.0; /* 番兵 */
        m1.st_late.value = -888.0;       /* 番兵 */
        CHECK(ofdx_ac_analyze_rir(ctx, &view, &m1) == OFDX_AC_SUCCESS);
        CHECK(m1.t30.valid);
        CHECK_NEAR(m1.t30.value, rt60, 0.1);
        CHECK(m1.early_late_50.value == -777.0); /* 触れられていない */
        CHECK(m1.st_late.value == -888.0);
        CHECK(m1.early_late_50.valid == 0);
    }

    ofdx_ac_context_destroy(ctx);
    free(samples);
}

/* ── api_version 2: 帯域別分析 (ofdx_ac_result) ── */
static void test_analyze_full(void) {
    const double rt60 = 1.0;
    const double fs = 48000.0;
    size_t count = 0;
    double *samples = make_synthetic_rir(rt60, fs, 1.8, 480 /* 10 ms */, &count);
    ofdx_ac_context *ctx;
    ofdx_ac_audio_view view;
    ofdx_ac_analysis_config config;
    ofdx_ac_result *result = NULL;
    ofdx_ac_error err;
    size_t i;

    CHECK(samples != NULL);
    if (samples == NULL) return;

    /* 離散エコー (直接音 +50 ms, 振幅 0.5) を足して反射検出を検証する */
    {
        size_t echo = 480 + (size_t)(0.050 * fs + 0.5);
        if (echo < count) samples[echo] += 0.5;
    }

    ctx = ofdx_ac_context_create();
    CHECK(ctx != NULL);
    if (ctx == NULL) {
        free(samples);
        return;
    }

    view.samples = samples;
    view.sample_count = count;
    view.sample_rate_hz = fs;

    /* 1) 既定 (config = NULL) は Compat6 → 6 帯域 */
    err = ofdx_ac_analyze_rir_full(ctx, &view, NULL, &result);
    CHECK(err == OFDX_AC_SUCCESS);
    CHECK(result != NULL);
    if (err == OFDX_AC_SUCCESS && result != NULL) {
        const double centers[6] = {125.0, 250.0, 500.0,
                                   1000.0, 2000.0, 4000.0};
        CHECK(ofdx_ac_result_band_count(result) == 6);
        for (i = 0; i < ofdx_ac_result_band_count(result) && i < 6; ++i) {
            ofdx_ac_band_info info;
            ofdx_ac_metrics bm;
            memset(&info, 0, sizeof(info));
            info.struct_size = sizeof(info);
            CHECK(ofdx_ac_result_band_info(result, i, &info) ==
                  OFDX_AC_SUCCESS);
            CHECK_NEAR(info.center_hz, centers[i], centers[i] * 1.0e-9);
            CHECK(!info.is_full_band);
            CHECK(info.filter_ok);
            /* 帯域ごとの INR: 45 dB 以上なら T30 まで評価可能なはず */
            CHECK(info.noise_ok);
            CHECK(info.inr_db > 45.0);
            CHECK(strlen(ofdx_ac_result_band_label(result, i)) > 0);

            /* 帯域別指標 (v2 レイアウト): 減衰白色雑音は全帯域で同じ
             * RT なので T30 ~= 1.0 s (帯域フィルタの分散込みで ±0.15) */
            memset(&bm, 0, sizeof(bm));
            bm.struct_size = sizeof(bm);
            bm.api_version = OFDX_AC_API_VERSION;
            CHECK(ofdx_ac_result_band_metrics(result, i, &bm) ==
                  OFDX_AC_SUCCESS);
            CHECK(bm.t30.valid);
            CHECK_NEAR(bm.t30.value, rt60, 0.15);
        }

        /* 反射リスト: +50 ms のエコーが検出されていること */
        {
            size_t n = ofdx_ac_result_reflection_count(result);
            int found = 0;
            CHECK(n >= 1);
            for (i = 0; i < n; ++i) {
                ofdx_ac_reflection refl;
                memset(&refl, 0, sizeof(refl));
                refl.struct_size = sizeof(refl);
                CHECK(ofdx_ac_result_reflection(result, i, &refl) ==
                      OFDX_AC_SUCCESS);
                CHECK(refl.band_index == -1); /* 広帯域検出 */
                if (fabs(refl.delay_from_direct_s - 0.050) < 0.002) {
                    found = 1;
                    /* レベルの物理値は test_reflections が検証する。
                     * ここは写像の契約のみ: 有限で妥当な範囲にあること
                     * (このモデルは減衰雑音が重なり -6 dB より高く出る) */
                    CHECK(refl.relative_level_db > -20.0 &&
                          refl.relative_level_db < 6.0);
                    CHECK(refl.confidence > 0.0 && refl.confidence <= 1.0);
                }
            }
            CHECK(found);
        }

        /* warning 文字列のアクセサ (件数 0 でも API は安全) */
        {
            size_t n = ofdx_ac_result_warning_count(result);
            for (i = 0; i < n; ++i)
                CHECK(strlen(ofdx_ac_result_warning(result, i)) > 0);
            /* 範囲外は空文字列 (NULL は返さない) */
            CHECK(ofdx_ac_result_warning(result, n) != NULL);
            CHECK(strlen(ofdx_ac_result_warning(result, n)) == 0);
        }

        /* 全体品質は enum の範囲内 */
        {
            int q = ofdx_ac_result_overall_quality(result);
            CHECK(q == OFDX_AC_QUALITY_INVALID ||
                  q == OFDX_AC_QUALITY_WARNING || q == OFDX_AC_QUALITY_VALID);
        }

        ofdx_ac_result_destroy(result);
        result = NULL;
    } else {
        printf("  analyze_full failed: %s\n", ofdx_ac_last_error(ctx));
    }

    /* 2) FULL_ONLY: 1 帯域 (全帯域)。広帯域 API と同じ値になる */
    memset(&config, 0, sizeof(config));
    config.struct_size = sizeof(config);
    config.band_set = OFDX_AC_BANDS_FULL_ONLY;
    err = ofdx_ac_analyze_rir_full(ctx, &view, &config, &result);
    CHECK(err == OFDX_AC_SUCCESS);
    if (err == OFDX_AC_SUCCESS && result != NULL) {
        ofdx_ac_band_info info;
        ofdx_ac_metrics fullband;
        ofdx_ac_metrics flat;
        CHECK(ofdx_ac_result_band_count(result) == 1);
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        CHECK(ofdx_ac_result_band_info(result, 0, &info) == OFDX_AC_SUCCESS);
        CHECK(info.is_full_band);
        CHECK(strcmp(ofdx_ac_result_band_label(result, 0), "full") == 0);

        memset(&fullband, 0, sizeof(fullband));
        fullband.struct_size = sizeof(fullband);
        fullband.api_version = OFDX_AC_API_VERSION;
        CHECK(ofdx_ac_result_band_metrics(result, 0, &fullband) ==
              OFDX_AC_SUCCESS);
        memset(&flat, 0, sizeof(flat));
        flat.struct_size = sizeof(flat);
        flat.api_version = OFDX_AC_API_VERSION;
        CHECK(ofdx_ac_analyze_rir(ctx, &view, &flat) == OFDX_AC_SUCCESS);
        CHECK(fullband.t30.value == flat.t30.value); /* 同一経路 → 一致 */
        CHECK(fullband.st_early.value == flat.st_early.value);

        /* 生きたハンドルに対する ABI 検査 (struct_size / api_version) */
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info) - 1;
        CHECK(ofdx_ac_result_band_info(result, 0, &info) ==
              OFDX_AC_INVALID_ARGUMENT);
        fullband.api_version = OFDX_AC_API_VERSION + 1u;
        CHECK(ofdx_ac_result_band_metrics(result, 0, &fullband) ==
              OFDX_AC_INVALID_ARGUMENT);
        fullband.api_version = OFDX_AC_API_VERSION;
        CHECK(ofdx_ac_result_band_metrics(result, 1, &fullband) ==
              OFDX_AC_INVALID_ARGUMENT); /* 範囲外 index */
        {
            ofdx_ac_reflection refl;
            memset(&refl, 0, sizeof(refl));
            refl.struct_size = sizeof(refl) - 1;
            CHECK(ofdx_ac_result_reflection(result, 0, &refl) ==
                  OFDX_AC_INVALID_ARGUMENT);
        }

        ofdx_ac_result_destroy(result);
        result = NULL;
    }

    /* 3) エラー系: 不正な band_set / config の struct_size 不一致 /
     *    NULL 引数 / 破棄と NULL アクセサの安全性 */
    config.band_set = 99;
    CHECK(ofdx_ac_analyze_rir_full(ctx, &view, &config, &result) ==
          OFDX_AC_INVALID_ARGUMENT);
    CHECK(result == NULL);
    CHECK(strstr(ofdx_ac_last_error(ctx), "band_set") != NULL);

    config.band_set = OFDX_AC_BANDS_COMPAT6;
    config.struct_size = sizeof(config) - 1;
    CHECK(ofdx_ac_analyze_rir_full(ctx, &view, &config, &result) ==
          OFDX_AC_INVALID_ARGUMENT);

    CHECK(ofdx_ac_analyze_rir_full(NULL, &view, NULL, &result) ==
          OFDX_AC_INVALID_ARGUMENT);
    CHECK(ofdx_ac_analyze_rir_full(ctx, NULL, NULL, &result) ==
          OFDX_AC_INVALID_ARGUMENT);
    CHECK(ofdx_ac_analyze_rir_full(ctx, &view, NULL, NULL) ==
          OFDX_AC_INVALID_ARGUMENT);

    /* NULL result のアクセサは 0 / 空文字列 / INVALID を返す */
    CHECK(ofdx_ac_result_band_count(NULL) == 0);
    CHECK(ofdx_ac_result_reflection_count(NULL) == 0);
    CHECK(ofdx_ac_result_warning_count(NULL) == 0);
    CHECK(ofdx_ac_result_band_label(NULL, 0) != NULL);
    CHECK(strlen(ofdx_ac_result_band_label(NULL, 0)) == 0);
    CHECK(ofdx_ac_result_warning(NULL, 0) != NULL);
    CHECK(ofdx_ac_result_overall_quality(NULL) == OFDX_AC_QUALITY_INVALID);
    {
        ofdx_ac_band_info info;
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        CHECK(ofdx_ac_result_band_info(NULL, 0, &info) ==
              OFDX_AC_INVALID_ARGUMENT);
    }
    ofdx_ac_result_destroy(NULL); /* クラッシュしないこと */

    ofdx_ac_context_destroy(ctx);
    free(samples);
}

static void test_null_arguments(void) {
    double samples[8] = {0.0};
    ofdx_ac_context *ctx = ofdx_ac_context_create();
    ofdx_ac_audio_view view;
    ofdx_ac_metrics metrics;

    CHECK(ctx != NULL);
    if (ctx == NULL) return;

    memset(&metrics, 0, sizeof(metrics));
    metrics.struct_size = sizeof(ofdx_ac_metrics);
    metrics.api_version = OFDX_AC_API_VERSION;

    view.samples = samples;
    view.sample_count = 8;
    view.sample_rate_hz = 48000.0;

    /* ctx == NULL */
    CHECK(ofdx_ac_analyze_rir(NULL, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    /* audio == NULL */
    CHECK(ofdx_ac_analyze_rir(ctx, NULL, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    CHECK(strlen(ofdx_ac_last_error(ctx)) > 0);
    /* out_metrics == NULL */
    CHECK(ofdx_ac_analyze_rir(ctx, &view, NULL) == OFDX_AC_INVALID_ARGUMENT);
    /* samples == NULL */
    view.samples = NULL;
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    view.samples = samples;
    /* sample_count == 0 */
    view.sample_count = 0;
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    view.sample_count = 8;
    /* sample_rate_hz <= 0 */
    view.sample_rate_hz = 0.0;
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    view.sample_rate_hz = -48000.0;
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    view.sample_rate_hz = 48000.0;

    /* 短すぎる有効入力 → INVALID_AUDIO (INVALID_ARGUMENT ではない) */
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) == OFDX_AC_INVALID_AUDIO);
    CHECK(strlen(ofdx_ac_last_error(ctx)) > 0);

    /* ctx == NULL の last_error は NULL でなく空文字列 */
    CHECK(ofdx_ac_last_error(NULL) != NULL);
    CHECK(strlen(ofdx_ac_last_error(NULL)) == 0);

    /* destroy(NULL) がクラッシュしないこと */
    ofdx_ac_context_destroy(NULL);

    ofdx_ac_context_destroy(ctx);
}

static void test_abi_mismatch(void) {
    double samples[8] = {0.0};
    ofdx_ac_context *ctx = ofdx_ac_context_create();
    ofdx_ac_audio_view view;
    ofdx_ac_metrics metrics;

    CHECK(ctx != NULL);
    if (ctx == NULL) return;

    view.samples = samples;
    view.sample_count = 8;
    view.sample_rate_hz = 48000.0;

    /* struct_size 不一致 */
    memset(&metrics, 0, sizeof(metrics));
    metrics.struct_size = sizeof(ofdx_ac_metrics) - 1;
    metrics.api_version = OFDX_AC_API_VERSION;
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    CHECK(strstr(ofdx_ac_last_error(ctx), "struct_size") != NULL);

    /* struct_size 未設定 (0) */
    metrics.struct_size = 0;
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);

    /* api_version 不一致 */
    metrics.struct_size = sizeof(ofdx_ac_metrics);
    metrics.api_version = OFDX_AC_API_VERSION + 1u;
    CHECK(ofdx_ac_analyze_rir(ctx, &view, &metrics) ==
          OFDX_AC_INVALID_ARGUMENT);
    CHECK(strstr(ofdx_ac_last_error(ctx), "api_version") != NULL);

    ofdx_ac_context_destroy(ctx);
}

int main(void) {
    printf("== C API: analyze synthetic RIR ==\n");
    test_analyze_valid();
    printf("== C API: band-split analysis (ofdx_ac_result) ==\n");
    test_analyze_full();
    printf("== C API: NULL arguments ==\n");
    test_null_arguments();
    printf("== C API: struct_size / api_version mismatch ==\n");
    test_abi_mismatch();

    printf("test_c_api: %d checks, %d failures — %s\n", g_checks, g_failures,
           g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
