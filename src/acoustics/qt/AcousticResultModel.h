// AcousticResultModel.h — RirAnalysisResult → 表表示用の行リスト変換と
// CSV / JSON 文字列化。GUI (RirAnalysisTab) とファイル出力の共通層。
//
// 行の文字列は言語非依存の生値 (指標名 / 数値 / 単位 / 品質トークン)。
// 画面向けの翻訳 (品質バッジ・「算出不可」表記など) はタブ側で行う。
#pragma once
#include <QString>
#include <QVector>

#include "../core/RirAnalyzer.h"

namespace ofd {

// 指標表の1行 (指標 × 帯域)
struct AcousticResultRow {
    QString metric;   // "EDT" / "T20" / "T30" / "C50" / "C80" / "D50" / "Ts"
    QString band;     // 帯域ラベル ("125 Hz", "Full band" …)
    bool    valid = false;
    double  value = 0.0;      // valid 時のみ意味を持つ
    QString valueText;        // 表示用数値 (invalid 時は空)
    QString unit;             // "s" / "dB" / "-" 等
    QString quality;          // "valid" / "warning" / "invalid"
    QString warning;          // 品質低下・無効の理由 (空 = 問題なし)
};

class AcousticResultModel {
public:
    // 指標 × 帯域の行リスト (EDT/T20/T30/C50/C80/D50/Ts の順、帯域ごと)
    static QVector<AcousticResultRow>
    metricRows(const acoustics::RirAnalysisResult &result);

    // 品質トークン ("valid"/"warning"/"invalid")
    static QString qualityToken(acoustics::AnalysisQuality q);

    // 反射の時間区分ラベル: 0-20 / 20-80 / 80-200 / 200+ ms
    static QString reflectionBinLabel(double delaySeconds);

    // CSV 文字列化 (指標表 + 反射一覧 + 警告)
    static QString toCsv(const acoustics::RirAnalysisResult &result);

    // JSON 文字列化 (QJsonDocument 経由、前処理・直接音・帯域・反射を含む)
    static QString toJson(const acoustics::RirAnalysisResult &result);
};

} // namespace ofd
