/* openfdtd_x_acoustics.h — OpenFDTD-X 音響分析コアの安定 C API。
 *
 * 純 C (C99) から利用可能。STL / Qt / C++ クラス / 例外 / テンプレートは
 * 一切露出しない。ABI 互換性は ofdx_ac_metrics.struct_size と
 * ofdx_ac_metrics.api_version による前方互換検査で担保する:
 * 呼び出し側は analyze の前に必ず
 *     metrics.struct_size = sizeof(ofdx_ac_metrics);
 *     metrics.api_version = OFDX_AC_API_VERSION;
 * を設定すること。実装は既知のレイアウトと一致しない要求を
 * OFDX_AC_INVALID_ARGUMENT で拒否する。
 *
 * スレッド安全性: 異なる context を使う限り並行呼び出し可。同一 context の
 * 共有には外部同期が必要 (last_error バッファが context 内にあるため)。
 */
#ifndef OPENFDTD_X_ACOUSTICS_H
#define OPENFDTD_X_ACOUSTICS_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint32_t */

#ifdef __cplusplus
extern "C" {
#endif

/* この API のバージョン。構造体レイアウトが変わったら増やす。
 * 履歴: 1 = 広帯域 7 指標のみ
 *       2 = ofdx_ac_metrics に early/late・ST を末尾追加 +
 *           帯域別結果 / 反射リスト / warning 文字列の ofdx_ac_result
 * バージョン 1 の呼び出し側 (api_version = 1, 旧 struct_size) も
 * 引き続き受理する (旧レイアウト分のみ書き込む)。 */
#define OFDX_AC_API_VERSION 2u

/* エラーコード */
typedef enum ofdx_ac_error {
    OFDX_AC_SUCCESS = 0,
    OFDX_AC_INVALID_ARGUMENT = 1,            /* NULL / struct_size 不一致など */
    OFDX_AC_INVALID_AUDIO = 2,               /* 短すぎる / NaN / クリッピング等 */
    OFDX_AC_DIRECT_SOUND_NOT_FOUND = 3,      /* 直接音が検出できない */
    OFDX_AC_INSUFFICIENT_DYNAMIC_RANGE = 4,  /* 動的範囲不足 */
    OFDX_AC_NUMERICAL_FAILURE = 5,           /* 回帰 / フィルタ設計の失敗 */
    OFDX_AC_INTERNAL_ERROR = 6               /* 想定外の内部エラー */
} ofdx_ac_error;

/* 指標の品質区分 (ofdx_ac_metric.quality の値) */
typedef enum ofdx_ac_quality {
    OFDX_AC_QUALITY_INVALID = 0, /* 無効 (評価不能) */
    OFDX_AC_QUALITY_WARNING = 1, /* 参考値 (決定係数不足など) */
    OFDX_AC_QUALITY_VALID = 2    /* 有効 */
} ofdx_ac_quality;

/* 非所有の音声ビュー。samples の寿命は呼び出し側が保証する。 */
typedef struct ofdx_ac_audio_view {
    const double *samples;   /* モノラルサンプル列 (振幅, フルスケール ±1) */
    size_t sample_count;     /* サンプル数 */
    double sample_rate_hz;   /* サンプリング周波数 [Hz] (> 0) */
} ofdx_ac_audio_view;

/* 単一指標の値と品質 */
typedef struct ofdx_ac_metric {
    double value; /* 指標値 (単位は指標ごと: 秒 / dB / 比) */
    int valid;    /* 0 = 評価不能, 非 0 = 評価可能 */
    int quality;  /* ofdx_ac_quality の値 */
} ofdx_ac_metric;

/* 分析結果 (広帯域 = フィルタなし全帯域の ISO 3382-1 指標)。
 * struct_size / api_version は呼び出し側が analyze の前に設定する入力
 * フィールド。将来フィールドが追加されても、古い呼び出し側は自分の
 * struct_size を渡すことで検出できる。 */
typedef struct ofdx_ac_metrics {
    size_t struct_size;      /* [in] sizeof(ofdx_ac_metrics) を設定 */
    uint32_t api_version;    /* [in] OFDX_AC_API_VERSION を設定 */
    ofdx_ac_metric edt;         /* Early Decay Time [s] */
    ofdx_ac_metric t20;         /* 残響時間 T20 [s] */
    ofdx_ac_metric t30;         /* 残響時間 T30 [s] */
    ofdx_ac_metric c50;         /* 明瞭度 C50 [dB] */
    ofdx_ac_metric c80;         /* 明瞭度 C80 [dB] */
    ofdx_ac_metric d50;         /* Definition D50 [0..1] */
    ofdx_ac_metric center_time; /* 重心時間 Ts [s] */
    /* ── ここから api_version 2 の末尾追加 (並び替え・挿入は禁止) ── */
    ofdx_ac_metric early_late_50; /* 早期/後期エネルギー比 (0-50ms / 50ms-) [線形比] */
    ofdx_ac_metric early_late_80; /* 早期/後期エネルギー比 (0-80ms / 80ms-) [線形比] */
    /* ST_early / ST_late (ISO 3382-1 Annex C の舞台支援) [dB]。
     * 測定条件 (舞台上・音源から 1 m・空席) はデータから検証できないため、
     * ここでは生値と品質フラグをそのまま返す (JSON 出力と同じ方針)。
     * 利用者向け表示での 3 値規則 (要求 §3.2) の適用は呼び出し側の責任。 */
    ofdx_ac_metric st_early;      /* ST_early [dB] (20-100ms / 0-10ms) */
    ofdx_ac_metric st_late;       /* ST_late [dB] (100-1000ms / 0-10ms) */
} ofdx_ac_metrics;

/* 不透明な分析コンテキスト (last_error バッファ等を保持) */
typedef struct ofdx_ac_context ofdx_ac_context;

/* コンテキスト生成。失敗時 (メモリ不足) は NULL。 */
ofdx_ac_context *ofdx_ac_context_create(void);

/* コンテキスト破棄。NULL は無視する。 */
void ofdx_ac_context_destroy(ofdx_ac_context *ctx);

/* 室内インパルス応答 (RIR) を分析し、広帯域の ISO 3382-1 指標を
 * out_metrics に書き込む。呼び出し前に out_metrics->struct_size と
 * out_metrics->api_version を設定すること。
 * 成功時 OFDX_AC_SUCCESS。失敗時はエラーコードを返し、詳細メッセージを
 * ofdx_ac_last_error(ctx) で取得できる。 */
ofdx_ac_error ofdx_ac_analyze_rir(ofdx_ac_context *ctx,
                                  const ofdx_ac_audio_view *audio,
                                  ofdx_ac_metrics *out_metrics);

/* 直近のエラーメッセージ (UTF-8, NUL 終端)。エラーがなければ空文字列。
 * 返るポインタは ctx が破棄されるか次の API 呼び出しまで有効。
 * ctx == NULL でも静的な空文字列を返す (NULL は返さない)。 */
const char *ofdx_ac_last_error(const ofdx_ac_context *ctx);

/* ══════════════════ api_version 2: 帯域別分析 (ofdx_ac_result) ═══════════════
 * 帯域別指標・反射リスト・warning 文字列は可変長のため、フラット構造体では
 * なく不透明ハンドル + アクセサで公開する。ハンドルは
 * ofdx_ac_result_destroy() で必ず破棄すること。
 * アクセサから返る const char* はハンドルが破棄されるまで有効。 */

/* 帯域セット (コア BandSet と同順。.ofdx の band_mode とは別番号) */
typedef enum ofdx_ac_band_set {
    OFDX_AC_BANDS_COMPAT6 = 0,        /* 1 oct {125..4k} (6 帯域) */
    OFDX_AC_BANDS_FULL_ONLY = 1,      /* 全帯域 (フィルタなし) のみ */
    OFDX_AC_BANDS_OCTAVE_63_8K = 2,   /* 1 oct {63..8k} (8 帯域) */
    OFDX_AC_BANDS_THIRD_100_5K = 3,   /* 1/3 oct {100..5k} (18 帯域) */
    OFDX_AC_BANDS_SINGER_FORMANT = 4  /* 歌手フォルマント帯域 (4 帯域) */
} ofdx_ac_band_set;

/* 分析設定。NULL を渡すと既定 (band_set = OFDX_AC_BANDS_COMPAT6)。
 * 将来の設定追加も末尾フィールド追加 + struct_size で行う。 */
typedef struct ofdx_ac_analysis_config {
    size_t struct_size; /* [in] sizeof(ofdx_ac_analysis_config) を設定 */
    int band_set;       /* [in] ofdx_ac_band_set の値 */
} ofdx_ac_analysis_config;

/* 帯域の情報。struct_size は呼び出し側が設定する ([in])。 */
typedef struct ofdx_ac_band_info {
    size_t struct_size;    /* [in] sizeof(ofdx_ac_band_info) を設定 */
    double center_hz;      /* 中心周波数 (幾何平均)。全帯域は 0 */
    double low_hz;         /* 下側エッジ [Hz]。全帯域は 0 */
    double high_hz;        /* 上側エッジ [Hz]。全帯域は 0 */
    int is_full_band;      /* 非 0 = 全帯域 (フィルタなし) */
    int filter_ok;         /* 非 0 = 帯域フィルタ設計に成功 */
    int noise_ok;          /* 非 0 = 帯域内動的範囲 (INR) を推定できた */
    double peak_db;        /* 帯域信号のピーク [dBFS] */
    double noise_floor_db; /* 帯域信号の末尾区間ノイズフロア [dBFS] */
    double inr_db;         /* impulse-to-noise ratio = peak - noise [dB] */
} ofdx_ac_band_info;

/* 反射音イベント。struct_size は呼び出し側が設定する ([in])。 */
typedef struct ofdx_ac_reflection {
    size_t struct_size;         /* [in] sizeof(ofdx_ac_reflection) を設定 */
    double arrival_time_s;      /* 信号先頭を 0 とした到達時刻 [s] */
    double delay_from_direct_s; /* 直接音からの遅延 [s] */
    double relative_level_db;   /* 直接音比レベル [dB] */
    double energy;              /* ピーク近傍のエネルギー (x² の和) */
    double confidence;          /* 0..1 (ノイズフロアからの突出量) */
    int band_index;             /* 帯域番号 (-1 = 広帯域) */
} ofdx_ac_reflection;

/* 不透明な分析結果ハンドル */
typedef struct ofdx_ac_result ofdx_ac_result;

/* RIR を帯域別に分析し、結果ハンドルを *out_result に返す。
 * config は NULL で既定。成功時は必ず ofdx_ac_result_destroy() で破棄する。
 * 失敗時は *out_result = NULL、詳細は ofdx_ac_last_error(ctx)。 */
ofdx_ac_error ofdx_ac_analyze_rir_full(ofdx_ac_context *ctx,
                                       const ofdx_ac_audio_view *audio,
                                       const ofdx_ac_analysis_config *config,
                                       ofdx_ac_result **out_result);

/* 結果ハンドルの破棄。NULL は無視する。 */
void ofdx_ac_result_destroy(ofdx_ac_result *result);

/* 帯域数 (result == NULL なら 0) */
size_t ofdx_ac_result_band_count(const ofdx_ac_result *result);

/* index 番目の帯域情報を out へ書く。呼び出し前に out->struct_size を
 * 設定すること。範囲外 / NULL / struct_size 不一致は INVALID_ARGUMENT。 */
ofdx_ac_error ofdx_ac_result_band_info(const ofdx_ac_result *result,
                                       size_t index,
                                       ofdx_ac_band_info *out_info);

/* index 番目の帯域ラベル (UTF-8, 例 "125", "full")。
 * 範囲外 / NULL は空文字列 (NULL は返さない)。 */
const char *ofdx_ac_result_band_label(const ofdx_ac_result *result,
                                      size_t index);

/* index 番目の帯域の指標を out へ書く。ofdx_ac_analyze_rir と同じ
 * struct_size / api_version 規約 (バージョン 1 レイアウトも受理)。 */
ofdx_ac_error ofdx_ac_result_band_metrics(const ofdx_ac_result *result,
                                          size_t index,
                                          ofdx_ac_metrics *out_metrics);

/* 反射音イベント数 (result == NULL なら 0)。広帯域信号からの検出。 */
size_t ofdx_ac_result_reflection_count(const ofdx_ac_result *result);

/* index 番目の反射音イベントを out へ書く。規約は band_info と同じ。 */
ofdx_ac_error ofdx_ac_result_reflection(const ofdx_ac_result *result,
                                        size_t index,
                                        ofdx_ac_reflection *out_reflection);

/* 分析 warning の件数 (result == NULL なら 0) */
size_t ofdx_ac_result_warning_count(const ofdx_ac_result *result);

/* index 番目の warning 文字列 (UTF-8)。範囲外 / NULL は空文字列。 */
const char *ofdx_ac_result_warning(const ofdx_ac_result *result,
                                   size_t index);

/* 分析全体の品質 (ofdx_ac_quality の値。result == NULL なら INVALID) */
int ofdx_ac_result_overall_quality(const ofdx_ac_result *result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OPENFDTD_X_ACOUSTICS_H */
