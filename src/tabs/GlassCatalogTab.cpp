// GlassCatalogTab.cpp
#include "GlassCatalogTab.h"
#include "../core/Project.h"
#include "../widgets/MiniPlot.h"
#include "../widgets/SectionBox.h"
#include "../I18n.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace ofd;

// ── AbbeDiagram ──────────────────────────────────────────────────────────────
namespace {
const double kNdMin = 1.43, kNdMax = 1.90;
const double kVdMin = 20.0, kVdMax = 100.0;
}

AbbeDiagram::AbbeDiagram(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(340, 220);
    setCursor(Qt::PointingHandCursor);
}

void AbbeDiagram::setSelected(int index)
{
    m_selected = index;
    update();
}

QPointF AbbeDiagram::toScreen(double vd, double nd) const
{
    // vd 軸は反転 (右ほど低分散 = vd 大)
    const double x = width() - 30
        - (vd - kVdMin) / (kVdMax - kVdMin) * (width() - 50);
    const double y = 14 + (kNdMax - nd) / (kNdMax - kNdMin) * (height() - 40);
    return { x, y };
}

void AbbeDiagram::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    p.setPen(QPen(palette().mid().color(), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    QFont f = p.font();
    f.setPointSizeF(7.5);
    p.setFont(f);

    // grid
    for (double nd = 1.5; nd <= 1.9; nd += 0.1) {
        const QPointF a = toScreen(kVdMin, nd), b = toScreen(kVdMax, nd);
        p.setPen(QPen(palette().midlight().color(), 1, Qt::DashLine));
        p.drawLine(a, b);
        p.setPen(palette().text().color());
        p.drawText(QPointF(2, a.y() + 3), QString::number(nd, 'f', 1));
    }
    for (double vd : { 30.0, 50.0, 70.0, 90.0 }) {
        const QPointF a = toScreen(vd, kNdMin), b = toScreen(vd, kNdMax);
        p.setPen(QPen(palette().midlight().color(), 1, Qt::DashLine));
        p.drawLine(a, b);
        p.setPen(palette().text().color());
        p.drawText(QPointF(a.x() - 6, height() - 4), QString::number(int(vd)));
    }
    p.setPen(QColor("#0078D4"));
    p.drawText(QPointF(width() - 74, 24), QString::fromUtf8("← クラウン"));
    p.setPen(QColor("#F59E0B"));
    p.drawText(QPointF(26, 24), QString::fromUtf8("フリント →"));

    // points
    const auto &all = GlassCatalog::all();
    for (int i = 0; i < all.size(); ++i) {
        const Glass &g = all[i];
        const QPointF c = toScreen(g.vd, g.nd);
        const bool active = (i == m_selected);
        p.setPen(QPen(Qt::white, 0.6));
        p.setBrush(active ? QColor("#0078D4") : QColor("#B83280"));
        p.setOpacity(active ? 1.0 : 0.65);
        p.drawEllipse(c, active ? 5.0 : 3.0, active ? 5.0 : 3.0);
        p.setOpacity(1.0);
        if (active) {
            p.setPen(palette().text().color());
            p.drawText(c + QPointF(7, 3), g.name);
        }
    }
}

void AbbeDiagram::mousePressEvent(QMouseEvent *e)
{
    const auto &all = GlassCatalog::all();
    int best = -1;
    double bestD = 12.0 * 12.0;   // 12px capture radius
    for (int i = 0; i < all.size(); ++i) {
        const QPointF c = toScreen(all[i].vd, all[i].nd);
        const QPointF d = c - e->position();
        const double dd = d.x() * d.x() + d.y() * d.y();
        if (dd < bestD) { bestD = dd; best = i; }
    }
    if (best >= 0)
        emit glassClicked(best);
}

// ── GlassCatalogTab ─────────────────────────────────────────────────────────
GlassCatalogTab::GlassCatalogTab(Project *project, QWidget *parent)
    : QScrollArea(parent), m_p(project)
{
    auto *body = new QWidget(this);
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(8);

    // search + filter + import
    auto *sTop = new SectionBox(I18n::tr("gc_section"), body);
    auto *hint = new QLabel(I18n::tr("gc_hint"), sTop);
    hint->setWordWrap(true);
    sTop->vbox()->addWidget(hint);

    auto *row1 = new QHBoxLayout();
    m_search = new QLineEdit(sTop);
    m_search->setPlaceholderText(I18n::tr("gc_search_ph"));
    m_maker = new QComboBox(sTop);
    row1->addWidget(m_search, 1);
    row1->addWidget(m_maker);
    sTop->vbox()->addLayout(row1);

    auto *row2 = new QHBoxLayout();
    auto *impCsv = new QPushButton(I18n::tr("gc_import_csv"), sTop);
    auto *impAgf = new QPushButton(I18n::tr("gc_import_agf"), sTop);
    m_status = new QLabel(sTop);
    row2->addWidget(impCsv);
    row2->addWidget(impAgf);
    row2->addWidget(m_status, 1);
    sTop->vbox()->addLayout(row2);
    v->addWidget(sTop);

    // list + detail side by side
    auto *mid = new QHBoxLayout();
    auto *sList = new SectionBox(I18n::tr("gc_list"), body);
    m_table = new QTableWidget(0, 6, sList);
    m_table->setHorizontalHeaderLabels({
        I18n::tr("gc_maker"), I18n::tr("gc_name"), "nd", "vd",
        I18n::tr("gc_price"), I18n::tr("gc_note") });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMinimumHeight(280);
    sList->vbox()->addWidget(m_table);
    mid->addWidget(sList, 1);

    auto *sDet = new SectionBox(I18n::tr("gc_selected"), body);
    m_detTitle = new QLabel(sDet);
    m_detTitle->setStyleSheet("font-weight:600;");
    sDet->vbox()->addWidget(m_detTitle);
    m_detNd = new QLabel(sDet);
    m_detN1550 = new QLabel(sDet);
    m_detN633 = new QLabel(sDet);
    m_detB = new QLabel(sDet);
    m_detC = new QLabel(sDet);
    sDet->form()->addRow("nd / vd", m_detNd);
    sDet->form()->addRow("n @ 1550nm", m_detN1550);
    sDet->form()->addRow("n @ 633nm", m_detN633);
    sDet->form()->addRow("Sellmeier B", m_detB);
    sDet->form()->addRow("Sellmeier C", m_detC);

    m_dispersion = new MiniPlot(sDet);
    m_dispersion->setLabels(QString::fromUtf8("λ [nm]"), "n");
    sDet->vbox()->addWidget(new QLabel(I18n::tr("gc_dispersion"), sDet));
    sDet->vbox()->addWidget(m_dispersion);

    auto *addBtn = new QPushButton(I18n::tr("gc_add_material"), sDet);
    addBtn->setProperty("primary", true);
    sDet->vbox()->addWidget(addBtn);
    auto *addHint = new QLabel(I18n::tr("gc_add_hint"), sDet);
    addHint->setWordWrap(true);
    sDet->vbox()->addWidget(addHint);
    mid->addWidget(sDet);
    v->addLayout(mid);

    // Abbe diagram
    auto *sAbbe = new SectionBox(I18n::tr("gc_abbe"), body);
    auto *abbeHint = new QLabel(I18n::tr("gc_abbe_hint"), sAbbe);
    abbeHint->setWordWrap(true);
    sAbbe->vbox()->addWidget(abbeHint);
    m_abbe = new AbbeDiagram(sAbbe);
    sAbbe->vbox()->addWidget(m_abbe);
    v->addWidget(sAbbe);

    // import format doc
    auto *sFmt = new SectionBox(I18n::tr("gc_fmt"), body);
    auto *fmt = new QLabel(I18n::tr("gc_fmt_body"), sFmt);
    fmt->setWordWrap(true);
    sFmt->vbox()->addWidget(fmt);
    v->addWidget(sFmt);

    v->addStretch(1);
    setWidget(body);
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    connect(m_search, &QLineEdit::textChanged, this, &GlassCatalogTab::refreshList);
    connect(m_maker, &QComboBox::currentIndexChanged, this,
            &GlassCatalogTab::refreshList);
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { selectRow(row); });
    connect(m_abbe, &AbbeDiagram::glassClicked, this, [this](int index) {
        m_selIndex = index;
        showGlass(GlassCatalog::all()[index]);
        // reflect into the table if visible
        for (int r = 0; r < m_rowToIndex.size(); ++r)
            if (m_rowToIndex[r] == index) { m_table->selectRow(r); break; }
    });
    connect(impCsv, &QPushButton::clicked, this, [this] { importCatalog(false); });
    connect(impAgf, &QPushButton::clicked, this, [this] { importCatalog(true); });
    connect(addBtn, &QPushButton::clicked, this, &GlassCatalogTab::addToMaterials);

    refreshList();
}

void GlassCatalogTab::refreshList()
{
    const auto &all = GlassCatalog::all();

    // maker filter combo (preserve selection)
    const QString cur = m_maker->currentText();
    QStringList makers { I18n::tr("gc_all_makers") };
    for (const Glass &g : all)
        if (!makers.contains(g.maker)) makers << g.maker;
    m_maker->blockSignals(true);
    m_maker->clear();
    m_maker->addItems(makers);
    if (makers.contains(cur)) m_maker->setCurrentText(cur);
    m_maker->blockSignals(false);

    const QString q = m_search->text().trimmed();
    const QString mk = m_maker->currentIndex() <= 0 ? QString()
                                                    : m_maker->currentText();
    m_rowToIndex.clear();
    m_table->setRowCount(0);
    for (int i = 0; i < all.size(); ++i) {
        const Glass &g = all[i];
        if (!mk.isEmpty() && g.maker != mk) continue;
        if (!q.isEmpty() && !g.name.contains(q, Qt::CaseInsensitive)) continue;
        const int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem(g.maker));
        auto *name = new QTableWidgetItem(g.name);
        QFont f = name->font(); f.setBold(true); name->setFont(f);
        m_table->setItem(r, 1, name);
        m_table->setItem(r, 2, new QTableWidgetItem(QString::number(g.nd, 'f', 4)));
        m_table->setItem(r, 3, new QTableWidgetItem(QString::number(g.vd, 'f', 1)));
        m_table->setItem(r, 4, new QTableWidgetItem(g.price));
        m_table->setItem(r, 5, new QTableWidgetItem(g.note));
        m_rowToIndex.push_back(i);
    }
    if (m_table->rowCount() > 0)
        m_table->selectRow(0);
}

int GlassCatalogTab::catalogIndexForRow(int row) const
{
    return (row >= 0 && row < m_rowToIndex.size()) ? m_rowToIndex[row] : -1;
}

void GlassCatalogTab::selectRow(int row)
{
    const int idx = catalogIndexForRow(row);
    if (idx < 0) return;
    m_selIndex = idx;
    showGlass(GlassCatalog::all()[idx]);
}

void GlassCatalogTab::showGlass(const Glass &g)
{
    m_detTitle->setText(QStringLiteral("%1 %2").arg(g.maker, g.name));
    m_detNd->setText(QStringLiteral("%1 / %2")
        .arg(QString::number(g.nd, 'f', 5), QString::number(g.vd, 'f', 2)));
    m_detN1550->setText(QString::number(g.n(1.55), 'f', 5));
    m_detN633->setText(QString::number(g.n(0.633), 'f', 5));
    if (g.hasSellmeier()) {
        m_detB->setText(QStringLiteral("%1, %2, %3")
            .arg(g.B[0], 0, 'f', 4).arg(g.B[1], 0, 'f', 4).arg(g.B[2], 0, 'f', 4));
        m_detC->setText(QStringLiteral("%1, %2, %3")
            .arg(g.C[0], 0, 'f', 5).arg(g.C[1], 0, 'f', 5).arg(g.C[2], 0, 'f', 5));
    } else {
        m_detB->setText(I18n::tr("gc_no_sellmeier"));
        m_detC->setText(QStringLiteral("—"));
    }

    // dispersion curve 0.4–1.6 μm
    MiniSeries s;
    s.color = QColor("#B83280");
    for (int i = 0; i < 50; ++i) {
        const double lam = 0.4 + i * 0.024;
        s.pts.push_back({ lam * 1000.0, g.n(lam) });
    }
    m_dispersion->setSeries({ s });

    m_abbe->setSelected(m_selIndex);
}

void GlassCatalogTab::importCatalog(bool agf)
{
    const QString path = QFileDialog::getOpenFileName(this,
        agf ? I18n::tr("gc_import_agf") : I18n::tr("gc_import_csv"), {},
        agf ? "Zemax AGF (*.agf);;All files (*)"
            : "CSV (*.csv);;All files (*)");
    if (path.isEmpty()) return;

    const GlassImportResult r = agf ? GlassCatalog::importAgf(path)
                                    : GlassCatalog::importCsv(path);
    if (!r.ok) {
        m_status->setText("error: " + r.error);
        return;
    }
    m_status->setText(I18n::tr("gc_imported")
        .arg(r.imported).arg(r.skipped));
    refreshList();
}

void GlassCatalogTab::addToMaterials()
{
    const auto &all = GlassCatalog::all();
    if (m_selIndex < 0 || m_selIndex >= all.size()) return;
    const Glass &g = all[m_selIndex];

    // FDTD は単一走査で εr を使うため、光解析タブの波長範囲の中心波長で
    // n を評価して εr = n² に変換する (分散は中心波長近傍で有効)。
    const OpticalOpts &o = m_p->optical();
    const double lc_um = (o.lambdaMin + o.lambdaMax) / 2.0 / 1000.0;
    const double n = g.n(lc_um);

    Material m;
    m.type = 1;
    m.epsr = n * n;
    m.name = QStringLiteral("%1 (n=%2 @%3nm)")
        .arg(g.name, QString::number(n, 'f', 4))
        .arg(qRound(lc_um * 1000));
    m_p->materials().push_back(m);
    emit m_p->materialsEdited();
    m_p->touch();

    m_status->setText(I18n::tr("gc_added")
        .arg(g.name)
        .arg(m_p->materials().size() + 1));   // 表示ID (0,1 組込みの次から)
}
