// AcousticPreflight.cpp — 外部音響ソルバー起動前の入力点検。
#include "AcousticPreflight.h"

#include "Project.h"
#include "../I18n.h"

namespace ofd {
namespace preflight {

namespace {
const bool s_i18n = [] {
    I18n::reg("pfl_mesh",
        "ソルバ領域 (メッシュ) が不正です。③ ソルバ領域タブで "
        "X/Y/Z の範囲と分割数を設定してください。",
        "The solver region (mesh) is invalid. Set the X/Y/Z extents and "
        "divisions in the Solver region tab.");
    I18n::reg("pfl_nofeed",
        "音源 (feed) がありません。④ 音源 (励振) タブで点音源を追加するか、"
        "🎤 音源/WAV/指向性 タブの「⚡ 有効な音源をソルバ波源へ反映」を"
        "使ってください。",
        "There is no source (feed). Add a point source in the Sources "
        "(excitation) tab, or use “Apply enabled sources to solver feeds” in "
        "the Source/WAV/Directivity tab.");
    I18n::reg("pfl_nopoint",
        "受音点 (観測点) がありません。④ 音源 (励振) タブの「観測点」に "
        "1 つ以上追加してください (RIR はここで受音します)。",
        "There is no receiver (observation point). Add at least one in the "
        "“Observation points” list of the Sources tab — that is where the RIR "
        "is picked up.");
    I18n::reg("pfl_feed_out",
        "音源 #%1 の位置 (%2, %3, %4) が室の外です。室は "
        "X [%5, %6] · Y [%7, %8] · Z [%9, %10] m です — 位置を室内へ直すか、"
        "③ ソルバ領域を広げてください。",
        "Source #%1 at (%2, %3, %4) is outside the room. The room is "
        "X [%5, %6] · Y [%7, %8] · Z [%9, %10] m — move it inside or enlarge "
        "the solver region.");
    I18n::reg("pfl_point_out",
        "受音点 #%1 の位置 (%2, %3, %4) が室の外です。室は "
        "X [%5, %6] · Y [%7, %8] · Z [%9, %10] m です。",
        "Receiver #%1 at (%2, %3, %4) is outside the room. The room is "
        "X [%5, %6] · Y [%7, %8] · Z [%9, %10] m.");
    I18n::reg("pfl_cells",
        "セル総数 %1 がソルバーの上限 %2 を超えています。③ ソルバ領域の"
        "分割数を減らす (刻みを粗くする) か、室を小さくしてください。",
        "The cell count %1 exceeds the solver limit %2. Reduce the divisions "
        "(coarser grid) in the Solver region tab, or shrink the room.");
    I18n::reg("pfl_dir_aim",
        "音源 #%1 は指向性 (%2) なのに向き (aim) がありません。"
        "🔌 音響ソルバ連携タブの feed 表で「向き」に 0 でない [x, y, z] を"
        "入れるか、指向性を「無指向」に戻してください。",
        "Source #%1 is directional (%2) but has no aim. Enter a non-zero "
        "[x, y, z] in the “Aim” column of the feed table in the Acoustic "
        "Solver tab, or set the directivity back to omni.");
    I18n::reg("pfl_dir_name",
        "音源 #%1 の指向性 \"%2\" はソルバーが知らない名前です "
        "(omni / subcardioid / cardioid / supercardioid / hypercardioid / "
        "figure8)。🔌 音響ソルバ連携タブの feed 表で選び直してください。",
        "The directivity \"%2\" of source #%1 is not a name the solvers know "
        "(omni / subcardioid / cardioid / supercardioid / hypercardioid / "
        "figure8). Pick one in the feed table of the Acoustic Solver tab.");
    return true;
}();

QString num(double v)
{
    return QString::number(v, 'g', 6);
}
} // namespace

long long maxCells() { return 30000000LL; }

QStringList acousticRunProblems(const Project &p)
{
    QStringList out;

    // 1) メッシュ
    bool meshOk = true;
    for (int a = 0; a < 3; ++a)
        if (!p.mesh(a).isValid()) meshOk = false;
    if (!meshOk) {
        out << I18n::tr("pfl_mesh");
        return out;    // 範囲が取れないので以降の判定は行わない
    }

    const double lo[3] = { p.mesh(0).min(), p.mesh(1).min(), p.mesh(2).min() };
    const double hi[3] = { p.mesh(0).max(), p.mesh(1).max(), p.mesh(2).max() };

    // 2) 音源 / 3) 受音点の有無
    if (p.feeds().isEmpty()) out << I18n::tr("pfl_nofeed");
    if (p.probes().isEmpty()) out << I18n::tr("pfl_nopoint");

    // 4) 音源が室内か
    const auto outside = [&lo, &hi](double x, double y, double z) {
        const double v[3] = { x, y, z };
        for (int a = 0; a < 3; ++a)
            if (v[a] < lo[a] || v[a] > hi[a]) return true;
        return false;
    };
    const auto rangeArgs = [&lo, &hi](QString s) {
        return s.arg(num(lo[0]), num(hi[0]), num(lo[1]), num(hi[1]),
                     num(lo[2]), num(hi[2]));
    };
    for (int i = 0; i < p.feeds().size(); ++i) {
        const Feed &f = p.feeds().at(i);
        if (!outside(f.x, f.y, f.z)) continue;
        out << rangeArgs(I18n::tr("pfl_feed_out")
                             .arg(QString::number(i + 1), num(f.x), num(f.y),
                                  num(f.z)));
    }
    // 5) 受音点が室内か
    for (int i = 0; i < p.probes().size(); ++i) {
        const Probe &r = p.probes().at(i);
        if (!outside(r.x, r.y, r.z)) continue;
        out << rangeArgs(I18n::tr("pfl_point_out")
                             .arg(QString::number(i + 1), num(r.x), num(r.y),
                                  num(r.z)));
    }

    // 7) 指向性 (ADR-0014) : 無指向でない音源に向きが無い / 未知の名前。
    //    ソルバーは発火しない feed (multi_source オフの #2 以降) の指定も
    //    検査するので、feed 行のある全行を見る。
    {
        const auto &fd = p.acoustic().feedDrives;
        const int n = qMin(fd.size(), p.feeds().size());
        for (int i = 0; i < n; ++i) {
            const AcousticOpts::FeedDrive &d = fd.at(i);
            if (d.directivity.isString()
                && !AcousticOpts::FeedDrive::isKnownDirectivityName(
                       d.directivity.toString())) {
                out << I18n::tr("pfl_dir_name")
                           .arg(QString::number(i + 1), d.directivity.toString());
                continue;
            }
            if (d.isDirectional() && d.aimIsZero()) {
                const QString what = d.directivity.isString()
                    ? d.directivity.toString()
                    : QStringLiteral("a < 1");
                out << I18n::tr("pfl_dir_aim").arg(QString::number(i + 1), what);
            }
        }
    }

    // 6) セル総数
    const long long cells = static_cast<long long>(p.mesh(0).totalCells()) *
                            p.mesh(1).totalCells() * p.mesh(2).totalCells();
    if (cells > maxCells())
        out << I18n::tr("pfl_cells").arg(QString::number(cells),
                                         QString::number(maxCells()));
    return out;
}

} // namespace preflight
} // namespace ofd
