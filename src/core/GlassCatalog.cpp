// GlassCatalog.cpp
#include "GlassCatalog.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>
#include <cmath>

using namespace ofd;

double Glass::n(double lambda_um) const
{
    if (hasSellmeier()) {
        const double l2 = lambda_um * lambda_um;
        double n2 = 1.0;
        for (int i = 0; i < 3; ++i)
            n2 += B[i] * l2 / (l2 - C[i]);
        return std::sqrt(std::max(1.0, n2));
    }
    // nd/vd のみの簡易分散 (Cauchy 1次): n(λ) = A + B/λ²。
    // アッベ数の定義 vd = (nd−1)/(nF−nC) から B を逆算する。
    // λd=0.58756, λF=0.48613, λC=0.65627 μm。
    const double ld = 0.58756, lF = 0.48613, lC = 0.65627;
    const double dInv = 1.0 / (lF * lF) - 1.0 / (lC * lC);
    const double Bc = (vd > 0) ? (nd - 1.0) / (vd * dInv) : 0.0;
    const double Ac = nd - Bc / (ld * ld);
    return Ac + Bc / (lambda_um * lambda_um);
}

// ── built-in catalog ─────────────────────────────────────────────────────────
// Sellmeier 係数は自身の nd を 587.56nm で再現できる銘柄のみ保持する
// (selftest が |n(0.58756)−nd|<2e-3 を検証)。係数が確認できない銘柄は
// B=C=0 とし、nd/vd の1次近似分散 (Glass::n のフォールバック) を使う。
static QVector<Glass> builtinCatalog()
{
    auto g = [](const char *maker, const char *name, double nd, double vd,
                double b1, double b2, double b3,
                double c1, double c2, double c3,
                const char *price, const char *note) {
        Glass x;
        x.maker = QString::fromUtf8(maker);
        x.name = QString::fromUtf8(name);
        x.nd = nd; x.vd = vd;
        x.B[0] = b1; x.B[1] = b2; x.B[2] = b3;
        x.C[0] = c1; x.C[1] = c2; x.C[2] = c3;
        x.price = QString::fromUtf8(price);
        x.note = QString::fromUtf8(note);
        return x;
    };
    return {
        // Schott
        g("Schott","N-BK7",  1.51680,64.17, 1.03961212,0.231792344,1.01046945, 0.00600069867,0.0200179144,103.560653, "$","標準・最汎用"),
        g("Schott","N-SF11", 1.78472,25.68, 1.73759695,0.313747346,1.89878101, 0.013188707,0.0623068142,155.23629, "$$","高分散フリント"),
        g("Schott","N-SF6",  1.80518,25.36, 1.77931763,0.338149866,2.08734474, 0.0133714182,0.0617533621,174.01759, "$$","高屈折フリント"),
        g("Schott","N-LAK10",1.72003,50.62, 1.72878017,0.169257825,1.19386956, 0.00886014635,0.0363416509,82.9009069, "$$$","高屈折クラウン"),
        g("Schott","N-LASF9",1.85025,32.17, 2.00029547,0.298926886,1.80691843, 0.0121426017,0.0538736236,156.530829, "$$$$","超高屈折"),
        g("Schott","N-FK51A",1.48656,84.47, 0.971247817,0.216901417,0.904651666, 0.00472301995,0.0153575612,168.68133, "$$$$","異常分散・色消し"),
        g("Schott","SF2",    1.64769,33.85, 1.40301821,0.231767504,0.939056586, 0.0105795466,0.0493226978,112.405955, "$","古典フリント"),
        g("Schott","F2",     1.62004,36.37, 1.34533359,0.209073176,0.937357162, 0.00997743871,0.0470450767,111.886764, "$","標準フリント"),
        // Ohara
        g("Ohara","S-BSL7",  1.51633,64.06, 1.03595755,0.231809580,1.00916988, 0.00600291759,0.0201906283,103.510761, "$","BK7相当"),
        g("Ohara","S-LAH64", 1.78800,47.37, 0,0,0, 0,0,0, "$$$","高屈折ランタン"),
        g("Ohara","S-FPL53", 1.43875,94.93, 0,0,0, 0,0,0, "$$$$","蛍石代替・超低分散"),
        g("Ohara","S-TIH53", 1.84666,23.78, 0,0,0, 0,0,0, "$$","高分散"),
        // Hoya
        g("Hoya","BSC7",     1.51680,64.20, 1.03961212,0.231792344,1.01046945, 0.00600069867,0.0200179144,103.560653, "$","BK7相当"),
        g("Hoya","FD60",     1.80518,25.43, 1.77866611,0.337371236,2.06564513, 0.0133710783,0.0617123233,173.86435, "$$","高屈折フリント"),
        g("Hoya","TAC8",     1.72916,54.67, 0,0,0, 0,0,0, "$$$","低分散高屈折"),
        // CDGM
        g("CDGM","H-K9L",    1.51680,64.21, 1.03961212,0.231792344,1.01046945, 0.00600069867,0.0200179144,103.560653, "$","BK7相当・低価格"),
        g("CDGM","H-ZF52",   1.84666,23.78, 0,0,0, 0,0,0, "$","高分散・低価格"),
        // Specialty
        g("Corning","Fused Silica",1.45846,67.82, 0.6961663,0.4079426,0.8974794, 0.004679148,0.01351206,97.934, "$$","溶融石英・UV-IR"),
        g("Schott","ZERODUR",1.54270,56.0, 0,0,0, 0,0,0, "$$$$$","ゼロ膨張・ミラー基板"),
    };
}

QVector<Glass> &GlassCatalog::storage()
{
    static QVector<Glass> s = builtinCatalog();
    return s;
}

const QVector<Glass> &GlassCatalog::all()
{
    return storage();
}

// ── Zemax AGF ────────────────────────────────────────────────────────────────
// 例:
//   NM N-BK7 2 0 1.5168 64.17 0 ...
//   CD 1.03961212 0.00600069867 0.231792344 0.0200179144 1.01046945 103.560653
// NM: <name> <formula> <MIL> <nd> <vd> ...   formula 2 = Sellmeier1
// CD: 式の係数列 (Sellmeier1 は B1 C1 B2 C2 B3 C3)
GlassImportResult GlassCatalog::importAgf(const QString &path)
{
    GlassImportResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.error = f.errorString();
        return r;
    }
    QTextStream in(&f);
    static const QRegularExpression ws("\\s+");

    Glass cur;
    bool inGlass = false;
    int formula = 0;
    auto flush = [&] {
        if (!inGlass) return;
        if (formula == 2 && cur.hasSellmeier()) ++r.imported;
        else if (cur.nd > 1.0) { cur.B[0]=cur.B[1]=cur.B[2]=0;
                                 cur.C[0]=cur.C[1]=cur.C[2]=0; ++r.skipped; ++r.imported; }
        else { inGlass = false; return; }
        cur.maker = "AGF";
        cur.price = "?";
        storage().push_back(cur);
        inGlass = false;
    };

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const QStringList t = line.split(ws, Qt::SkipEmptyParts);
        if (t.isEmpty()) continue;
        if (t[0] == "NM" && t.size() >= 3) {
            flush();
            cur = Glass{};
            cur.name = t[1];
            formula = t[2].toInt();
            if (t.size() >= 5) cur.nd = t[4].toDouble();
            if (t.size() >= 6) cur.vd = t[5].toDouble();
            inGlass = true;
        } else if (t[0] == "CD" && inGlass && t.size() >= 7 && formula == 2) {
            // Sellmeier1: B1 C1 B2 C2 B3 C3
            cur.B[0] = t[1].toDouble(); cur.C[0] = t[2].toDouble();
            cur.B[1] = t[3].toDouble(); cur.C[1] = t[4].toDouble();
            cur.B[2] = t[5].toDouble(); cur.C[2] = t[6].toDouble();
        }
    }
    flush();

    r.ok = r.imported > 0;
    if (!r.ok && r.error.isEmpty())
        r.error = "no glasses found (expected Zemax AGF 'NM'/'CD' records)";
    return r;
}

// ── CSV ──────────────────────────────────────────────────────────────────────
GlassImportResult GlassCatalog::importCsv(const QString &path)
{
    GlassImportResult r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        r.error = f.errorString();
        return r;
    }
    QTextStream in(&f);
    if (in.atEnd()) { r.error = "empty file"; return r; }

    // header → column map (case-insensitive; 銘柄/νd 等の別名も受ける)
    const QStringList head = in.readLine().split(',');
    auto col = [&head](const QStringList &names) {
        for (int i = 0; i < head.size(); ++i) {
            const QString h = head[i].trimmed().toLower();
            for (const QString &n : names)
                if (h == n) return i;
        }
        return -1;
    };
    const int cName = col({ "glass", "name", QString::fromUtf8("銘柄") });
    const int cMaker= col({ "maker", "manufacturer", QString::fromUtf8("メーカー") });
    const int cNd = col({ "nd" });
    const int cVd = col({ "vd", QString::fromUtf8("νd"), "abbe" });
    const int cB[3] = { col({"b1"}), col({"b2"}), col({"b3"}) };
    const int cC[3] = { col({"c1"}), col({"c2"}), col({"c3"}) };
    if (cName < 0 || cNd < 0) {
        r.error = "CSV needs at least 'name' and 'nd' columns";
        return r;
    }

    while (!in.atEnd()) {
        const QStringList t = in.readLine().split(',');
        if (t.size() <= cName || t[cName].trimmed().isEmpty()) continue;
        if (cNd >= t.size()) { ++r.skipped; continue; }
        Glass g;
        g.name = t[cName].trimmed();
        g.maker = (cMaker >= 0 && cMaker < t.size()) ? t[cMaker].trimmed() : "CSV";
        if (g.maker.isEmpty()) g.maker = "CSV";
        g.nd = t[cNd].toDouble();
        if (cVd >= 0 && cVd < t.size()) g.vd = t[cVd].toDouble();
        bool sell = true;
        for (int i = 0; i < 3; ++i) {
            if (cB[i] >= 0 && cB[i] < t.size() && cC[i] >= 0 && cC[i] < t.size()) {
                g.B[i] = t[cB[i]].toDouble();
                g.C[i] = t[cC[i]].toDouble();
            } else sell = false;
        }
        if (!sell) { g.B[0]=g.B[1]=g.B[2]=0; g.C[0]=g.C[1]=g.C[2]=0; ++r.skipped; }
        if (g.nd <= 1.0) continue;
        g.price = "?";
        storage().push_back(g);
        ++r.imported;
    }
    r.ok = r.imported > 0;
    if (!r.ok && r.error.isEmpty()) r.error = "no data rows";
    return r;
}
