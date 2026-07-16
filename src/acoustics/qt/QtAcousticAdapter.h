// QtAcousticAdapter.h — Qt モデル (OperaAcousticSettings) と C++14 音響コア
// (src/acoustics/core, src/acoustics/io) を橋渡しする static 関数群。
//
// 方針:
//   - コアの結果型 (RirAnalysisResult / SchroederResult / AudioBuffer) は
//     Qt 型に包まず「そのまま」返す。QString 化は AcousticResultModel が担う。
//   - QString パス → std::string 変換と、チャンネル選択 (L/R/平均モノ) の
//     適用はここで行う。
#pragma once
#include <QString>
#include <vector>

#include "../core/RirAnalyzer.h"
#include "../core/SchroederDecay.h"
#include "../io/WavReader.h"
#include "../../core/Project.h"

namespace ofd {

class QtAcousticAdapter {
public:
    // WAV 読み込み (QString → std::string 変換のみ、正規化なし)
    static acoustics::AcousticResult<acoustics::AudioBuffer>
    readWav(const QString &path);

    // チャンネル選択: channelMode 0=L 1=R 2=平均モノ。
    // 指定チャンネルが無い場合は先頭チャンネルにフォールバックする。
    static std::vector<double>
    selectChannel(const acoustics::AudioBuffer &buffer, int channelMode);

    // OperaAcousticSettings → コアの RirAnalyzerConfig 変換
    static acoustics::RirAnalyzerConfig
    toAnalyzerConfig(const OperaAcousticSettings &settings);

    // 選択済み 1ch 信号を分析する
    static acoustics::AcousticResult<acoustics::RirAnalysisResult>
    analyze(const std::vector<double> &samples, double sampleRateHz,
            const OperaAcousticSettings &settings);

    // 便宜関数: settings.rirPath を読み込み → チャンネル選択 → 分析。
    // outSamples / outSampleRate が非 null なら分析に使った信号を返す
    // (波形・減衰カーブのプロット用)。
    static acoustics::AcousticResult<acoustics::RirAnalysisResult>
    analyzeFile(const OperaAcousticSettings &settings,
                std::vector<double> *outSamples = nullptr,
                double *outSampleRate = nullptr);

    // 広帯域 Schroeder 減衰カーブ (プロット用)
    static acoustics::SchroederResult
    decayCurve(const std::vector<double> &samples, double sampleRateHz,
               const OperaAcousticSettings &settings);
};

} // namespace ofd
