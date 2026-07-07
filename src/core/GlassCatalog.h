// GlassCatalog.h — optical glass catalog (Schott / Ohara / Hoya / CDGM …).
//
// 各銘柄は nd, vd と Sellmeier 係数 B1..3 / C1..3 を持ち、任意波長の屈折率を
//   n²(λ) = 1 + Σ Bᵢλ² / (λ² − Cᵢ)     (λ in μm)
// で計算できる。GlassCatalogTab (光ドメイン) と MaterialTab を連携させる共有
// データ。Zemax AGF / CSV カタログの取込にも対応する。
#pragma once
#include <QString>
#include <QVector>

namespace ofd {

struct Glass {
    QString maker;
    QString name;
    double  nd = 1.5;      // d線 (587.56 nm) 屈折率
    double  vd = 60.0;     // アッベ数
    double  B[3] = {0, 0, 0};   // Sellmeier B1..B3
    double  C[3] = {0, 0, 0};   // Sellmeier C1..C3 [μm²]
    QString price;         // "$".."$$$$$" (目安)
    QString note;

    bool hasSellmeier() const { return B[0] != 0 || B[1] != 0 || B[2] != 0; }

    // Sellmeier: n(λ). λ は μm。係数が無い銘柄は nd/vd による
    // 1次近似 (Cauchy 相当) で代用する。
    double n(double lambda_um) const;
};

struct GlassImportResult {
    bool    ok = false;
    QString error;
    int     imported = 0;
    int     skipped = 0;   // 対応外の分散式などで読み飛ばした銘柄数
};

class GlassCatalog {
public:
    // 内蔵カタログ + セッション中に取り込んだ銘柄 (取込分が後ろに付く)。
    static const QVector<Glass> &all();

    // Zemax AGF (.agf): "NM <name> <formula> ..." + "CD <c1>.." 形式。
    // 分散式コード 2 (Sellmeier1: CD = B1 C1 B2 C2 B3 C3) を取り込み、
    // それ以外の式は nd/vd のみで登録する (hasSellmeier()=false)。
    static GlassImportResult importAgf(const QString &path);

    // CSV: ヘッダ行に name / nd / vd / B1..B3 / C1..C3 (大文字小文字不問、
    // maker 任意)。Excel カタログは CSV 書き出しで取り込む。
    static GlassImportResult importCsv(const QString &path);

private:
    static QVector<Glass> &storage();
};

} // namespace ofd
