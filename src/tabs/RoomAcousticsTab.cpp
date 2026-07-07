// RoomAcousticsTab.cpp
#include "RoomAcousticsTab.h"
#include "../core/Project.h"
#include "../widgets/MiniPlot.h"
#include "../widgets/SectionBox.h"
#include "../I18n.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <cmath>

using namespace ofd;
using namespace ofd::roomac;

// 受音点の相対位置 (奥行き比, 幅比) — mock の P1..P4 に対応
static const struct { double dl, dw; const char *key; } kReceivers[4] = {
    { 0.30, 0.50, "ra_p1" },   // 中央前列
    { 0.60, 0.20, "ra_p2" },   // 左サイド
    { 0.60, 0.80, "ra_p3" },   // 右サイド
    { 0.85, 0.50, "ra_p4" },   // 後方中央
};

// ── CoverageMap ─────────────────────────────────────────────────────────────
CoverageMap::CoverageMap(Project *project, QWidget *parent)
    : QWidget(parent), m_p(project)
{
    setMinimumSize(360, 250);
    recompute();
}

double CoverageMap::cellValue(double r) const
{
    const AcousticOpts &a = m_p->acoustic();
    auto metricAt = [&](int band) {
        const double T = rt60(a, band);
        const SeatMetrics m = seatMetrics(r, T, a.volume);
        switch (m_metric) {
            case 0: return m.G;
            case 1: return m.C80;
            case 2: return m.STI;
            default: return m.RT;
        }
    };
    if (m_band >= 6) {   // 平均
        double s = 0;
        for (int b = 0; b < 6; ++b) s += metricAt(b);
        return s / 6.0;
    }
    return metricAt(m_band);
}

void CoverageMap::recompute()
{
    const AcousticOpts &a = m_p->acoustic();
    m_values.clear();
    double sum = 0, sum2 = 0;
    int n = 0;
    for (int row = 0; row <= 10; ++row) {
        const double t = row / 10.0;
        for (int col = 0; col <= 10; ++col) {
            const double halfW = (0.15 + 0.35 * t) * a.roomW;
            const double x = (col - 5) / 5.0 * halfW;
            if (std::fabs(x) > halfW + 1e-9) { m_values.push_back(NAN); continue; }
            const double y = 2.0 + t * (a.roomL - 4.0);   // 舞台前 2m から
            const double r = std::sqrt(x * x + y * y);
            const double v = cellValue(r);
            m_values.push_back(v);
            sum += v; sum2 += v * v; ++n;
        }
    }
    m_mean = n ? sum / n : 0;
    m_std = n ? std::sqrt(std::max(0.0, sum2 / n - m_mean * m_mean)) : 0;
    update();
}

void CoverageMap::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    p.setPen(QPen(palette().mid().color(), 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    const double W = width(), H = height();
    // 扇形ホール外形 (mock と同じ構図)
    QPolygonF hall;
    hall << QPointF(W/2, 18) << QPointF(W*0.17, H-30) << QPointF(W*0.83, H-30);
    p.setPen(QPen(palette().text().color(), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(hall);
    // 舞台
    p.setBrush(QColor(146, 64, 14, 150));
    p.setPen(Qt::NoPen);
    p.drawRect(QRectF(W/2 - 30, 12, 60, 14));
    p.setPen(palette().text().color());
    QFont f = p.font(); f.setPointSizeF(7.5); p.setFont(f);
    p.drawText(QRectF(W/2 - 30, 0, 60, 12), Qt::AlignCenter, "STAGE");

    // 値レンジ (色スケール正規化)
    double lo = 1e300, hi = -1e300;
    for (double v : m_values)
        if (!std::isnan(v)) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (lo >= hi) { lo -= 1; hi += 1; }

    // セル
    int idx = 0;
    for (int row = 0; row <= 10; ++row) {
        const double t = row / 10.0;
        for (int col = 0; col <= 10; ++col, ++idx) {
            const double v = m_values.value(idx, NAN);
            if (std::isnan(v)) continue;
            const double halfWpx = (0.13 + 0.33 * t) * W;
            const double cx = W/2 + (col - 5) / 5.0 * halfWpx;
            const double cy = 34 + t * (H - 70);
            if (std::fabs(cx - W/2) > halfWpx) continue;
            double norm = (v - lo) / (hi - lo);
            if (m_metric == 3) norm = 1.0 - norm;   // RT は短いほど「良」= 緑
            const QColor c = QColor::fromHsl(int(norm * 120), 178, 128);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(c.red(), c.green(), c.blue(), 200));
            p.drawRoundedRect(QRectF(cx - 9, cy - 7, 18, 15), 2, 2);
        }
    }

    // 受音点
    for (const auto &r : kReceivers) {
        const double t = r.dl;
        const double halfWpx = (0.13 + 0.33 * t) * W;
        const double cx = W/2 + (r.dw - 0.5) * 2.0 * halfWpx;
        const double cy = 34 + t * (H - 70);
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(cx, cy), 3.5, 3.5);
    }

    // カラースケール
    QLinearGradient grad(W - 130, 0, W - 10, 0);
    grad.setColorAt(0, QColor::fromHsl(0, 178, 128));
    grad.setColorAt(0.5, QColor::fromHsl(60, 178, 128));
    grad.setColorAt(1, QColor::fromHsl(120, 178, 128));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawRect(QRectF(W - 130, H - 22, 120, 10));
    p.setPen(palette().text().color());
    const bool inv = (m_metric == 3);
    p.drawText(QPointF(W - 130, H - 26), QString::number(inv ? hi : lo, 'g', 3));
    p.drawText(QPointF(W - 40, H - 26), QString::number(inv ? lo : hi, 'g', 3));
}

// ── RoomAcousticsTab ────────────────────────────────────────────────────────
RoomAcousticsTab::RoomAcousticsTab(Project *project, QWidget *parent)
    : QScrollArea(parent), m_p(project)
{
    auto *body = new QWidget(this);
    auto *v = new QVBoxLayout(body);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(8);

    auto *hint = new QLabel(I18n::tr("ra_model_hint"), body);
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_tabs = new QTabWidget(body);
    m_tabs->addTab(buildCoveragePage(), I18n::tr("ra_tab_coverage"));
    m_tabs->addTab(buildEchogramPage(), I18n::tr("ra_tab_echogram"));
    m_tabs->addTab(buildReverbPage(),   I18n::tr("ra_tab_reverb"));
    m_tabs->addTab(buildNoisePage(),    I18n::tr("ra_tab_noise"));
    m_tabs->addTab(buildDefectsPage(),  I18n::tr("ra_tab_defects"));
    v->addWidget(m_tabs);

    // export
    auto *sExp = new SectionBox(I18n::tr("ra_export"), body);
    auto *row = new QHBoxLayout();
    auto *repBtn = new QPushButton(I18n::tr("ra_export_report"), sExp);
    auto *pngBtn = new QPushButton(I18n::tr("ra_export_png"), sExp);
    row->addWidget(repBtn);
    row->addWidget(pngBtn);
    row->addStretch(1);
    sExp->vbox()->addLayout(row);
    v->addWidget(sExp);

    v->addStretch(1);
    setWidget(body);
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    connect(repBtn, &QPushButton::clicked, this, &RoomAcousticsTab::exportReport);
    connect(pngBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, I18n::tr("ra_export_png"), "coverage.png", "PNG (*.png)");
        if (!path.isEmpty()) m_map->grab().save(path);
    });

    connect(project, &Project::loaded, this, &RoomAcousticsTab::refresh);
    refresh();
}

void RoomAcousticsTab::sourcePos(double out[3]) const
{
    const AcousticOpts &a = m_p->acoustic();
    out[0] = 0.05 * a.roomL;
    out[1] = 0.50 * a.roomW;
    out[2] = 1.5;
}

void RoomAcousticsTab::receiverPos(int index, double out[3]) const
{
    const AcousticOpts &a = m_p->acoustic();
    index = qBound(0, index, 3);
    out[0] = kReceivers[index].dl * a.roomL;
    out[1] = kReceivers[index].dw * a.roomW;
    out[2] = 1.2;
}

// ── page builders ───────────────────────────────────────────────────────────
QWidget *RoomAcousticsTab::buildCoveragePage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setSpacing(8);

    auto *s = new SectionBox(I18n::tr("ra_coverage_section"), page);
    auto *hint = new QLabel(I18n::tr("ra_coverage_hint"), s);
    hint->setWordWrap(true);
    s->vbox()->addWidget(hint);

    m_metricBox = new QComboBox(s);
    m_metricBox->addItems({ "G (SPL) [dB]", "C80 [dB]", "STI", "RT60 [s]" });
    m_bandBox = new QComboBox(s);
    m_bandBox->addItems({ "125Hz", "250Hz", "500Hz", "1kHz", "2kHz", "4kHz",
                          I18n::tr("ra_band_avg") });
    m_bandBox->setCurrentIndex(3);
    s->form()->addRow(I18n::tr("ra_metric"), m_metricBox);
    s->form()->addRow(I18n::tr("ra_band"), m_bandBox);
    v->addWidget(s);

    auto *sm = new SectionBox(I18n::tr("ra_map_section"), page);
    auto *h = new QHBoxLayout();
    m_map = new CoverageMap(m_p, sm);
    h->addWidget(m_map, 1);
    m_covStats = new QLabel(sm);
    m_covStats->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    h->addWidget(m_covStats);
    sm->vbox()->addLayout(h);
    v->addWidget(sm);

    auto *st = new SectionBox(I18n::tr("ra_seat_section"), page);
    m_seatTable = new QTableWidget(4, 7, st);
    m_seatTable->setHorizontalHeaderLabels({
        I18n::tr("ra_receiver"), "G [dB]", "C80 [dB]", "D50", "STI",
        "RT60 [s]", I18n::tr("ra_verdict") });
    m_seatTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_seatTable->verticalHeader()->setVisible(false);
    m_seatTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_seatTable->setMinimumHeight(140);
    st->vbox()->addWidget(m_seatTable);
    v->addWidget(st);
    v->addStretch(1);

    connect(m_metricBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_map->setMetric(i);
        recomputeAll();
    });
    connect(m_bandBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_map->setBand(i);
        recomputeAll();
    });
    return page;
}

QWidget *RoomAcousticsTab::buildEchogramPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setSpacing(8);

    auto *s = new SectionBox(I18n::tr("ra_echo_section"), page);
    auto *hint = new QLabel(I18n::tr("ra_echo_hint"), s);
    hint->setWordWrap(true);
    s->vbox()->addWidget(hint);
    m_rcvBox = new QComboBox(s);
    for (const auto &r : kReceivers)
        m_rcvBox->addItem(I18n::tr(r.key));
    s->form()->addRow(I18n::tr("ra_receiver"), m_rcvBox);
    v->addWidget(s);

    auto *sp = new SectionBox(I18n::tr("ra_reflectogram"), page);
    m_echoPlot = new MiniPlot(sp);
    m_echoPlot->setImpulseMode(true);
    m_echoPlot->setLabels(I18n::tr("ra_time_ms"), I18n::tr("ra_level_db"));
    m_echoPlot->setMinimumHeight(150);
    sp->vbox()->addWidget(m_echoPlot);
    m_itdgLabel = new QLabel(sp);
    sp->vbox()->addWidget(m_itdgLabel);
    v->addWidget(sp);

    auto *st = new SectionBox(I18n::tr("ra_refl_section"), page);
    m_reflTable = new QTableWidget(0, 5, st);
    m_reflTable->setHorizontalHeaderLabels({
        I18n::tr("ra_reflection"), I18n::tr("ra_delay_ms"),
        I18n::tr("ra_level_db"), I18n::tr("ra_refl_surface"),
        I18n::tr("ra_verdict") });
    m_reflTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_reflTable->verticalHeader()->setVisible(false);
    m_reflTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_reflTable->setMinimumHeight(170);
    st->vbox()->addWidget(m_reflTable);
    v->addWidget(st);
    v->addStretch(1);

    connect(m_rcvBox, &QComboBox::currentIndexChanged, this,
            &RoomAcousticsTab::recomputeAll);
    return page;
}

QWidget *RoomAcousticsTab::buildReverbPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setSpacing(8);

    auto *s = new SectionBox(I18n::tr("ra_reverb_section"), page);
    auto *hint = new QLabel(I18n::tr("ra_reverb_hint"), s);
    hint->setWordWrap(true);
    s->vbox()->addWidget(hint);

    auto dsb = [&s](double lo, double hi, const char *suffix) {
        auto *w = new QDoubleSpinBox(s);
        w->setRange(lo, hi);
        w->setDecimals(1);
        w->setSuffix(QString::fromUtf8(suffix));
        return w;
    };
    m_roomL = dsb(2, 500, " m");
    m_roomW = dsb(2, 500, " m");
    m_roomH = dsb(2, 100, " m");
    m_volume = dsb(10, 1e6, " m³");
    m_surface = dsb(10, 1e6, " m²");
    auto *dims = new QHBoxLayout();
    dims->addWidget(new QLabel("L", s)); dims->addWidget(m_roomL);
    dims->addWidget(new QLabel("W", s)); dims->addWidget(m_roomW);
    dims->addWidget(new QLabel("H", s)); dims->addWidget(m_roomH);
    auto *fromDims = new QPushButton(I18n::tr("ra_from_dims"), s);
    dims->addWidget(fromDims);
    dims->addStretch(1);
    s->form()->addRow(I18n::tr("ra_room_dims"), dims);
    s->form()->addRow(I18n::tr("ra_volume"), m_volume);
    s->form()->addRow(I18n::tr("ra_surface"), m_surface);

    m_occupancy = new QComboBox(s);
    m_occupancy->addItems({ I18n::tr("ra_occ_empty"), I18n::tr("ra_occ_half"),
                            I18n::tr("ra_occ_full") });
    m_formula = new QComboBox(s);
    m_formula->addItems({ "Sabine", I18n::tr("ra_eyring") });
    s->form()->addRow(I18n::tr("ra_occupancy"), m_occupancy);
    s->form()->addRow(I18n::tr("ra_formula"), m_formula);
    v->addWidget(s);

    auto *sb = new SectionBox(I18n::tr("ra_budget_section"), page);
    m_budget = new QTableWidget(0, 8, sb);
    m_budget->setHorizontalHeaderLabels({
        "", I18n::tr("ra_element"), I18n::tr("ra_area"),
        "α125", "α500", "α1k", "α4k", "A@1k" });
    m_budget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_budget->horizontalHeader()->setStretchLastSection(true);
    m_budget->verticalHeader()->setVisible(false);
    m_budget->setMinimumHeight(200);
    sb->vbox()->addWidget(m_budget);
    v->addWidget(sb);

    auto *sr = new SectionBox(I18n::tr("ra_rt_section"), page);
    m_rtPlot = new MiniPlot(sr);
    m_rtPlot->setLabels("f [Hz]", "RT60 [s]");
    m_rtPlot->setXTickPow10(true);
    m_rtPlot->setMinimumHeight(140);
    sr->vbox()->addWidget(m_rtPlot);
    m_rtBadge = new QLabel(sr);
    m_rtBadge->setStyleSheet("font-weight:600; font-size:13px;");
    sr->vbox()->addWidget(m_rtBadge);
    auto *targets = new QLabel(I18n::tr("ra_rt_targets"), sr);
    targets->setWordWrap(true);
    sr->vbox()->addWidget(targets);
    v->addWidget(sr);
    v->addStretch(1);

    auto applyScalar = [this] {
        if (m_updating) return;
        AcousticOpts &a = m_p->acoustic();
        a.roomL = m_roomL->value();
        a.roomW = m_roomW->value();
        a.roomH = m_roomH->value();
        a.volume = m_volume->value();
        a.surface = m_surface->value();
        a.occupancy = m_occupancy->currentIndex();
        a.rtFormula = m_formula->currentIndex();
        refreshBudgetDerived();
        recomputeAll();
        m_p->touch();
    };
    for (auto *w : { m_roomL, m_roomW, m_roomH, m_volume, m_surface })
        connect(w, &QDoubleSpinBox::valueChanged, this, applyScalar);
    connect(m_occupancy, &QComboBox::currentIndexChanged, this, applyScalar);
    connect(m_formula, &QComboBox::currentIndexChanged, this, applyScalar);
    connect(fromDims, &QPushButton::clicked, this, [this] {
        AcousticOpts &a = m_p->acoustic();
        const double L = m_roomL->value(), W = m_roomW->value(),
                     H = m_roomH->value();
        a.volume = L * W * H;
        a.surface = 2.0 * (L * W + L * H + W * H);
        refresh();
        recomputeAll();
        m_p->touch();
    });
    connect(m_budget, &QTableWidget::cellChanged, this, [this] {
        if (m_updating) return;
        applyBudgetTable();
        refreshBudgetDerived();
        recomputeAll();
        m_p->touch();
    });
    return page;
}

QWidget *RoomAcousticsTab::buildNoisePage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setSpacing(8);

    auto *s = new SectionBox(I18n::tr("ra_noise_section"), page);
    auto *hint = new QLabel(I18n::tr("ra_noise_hint"), s);
    hint->setWordWrap(true);
    s->vbox()->addWidget(hint);

    m_noise = new QTableWidget(1, 7, s);
    m_noise->setHorizontalHeaderLabels(
        { "63", "125", "250", "500", "1k", "2k", "4k" });
    m_noise->setVerticalHeaderLabels({ I18n::tr("ra_noise_row") });
    m_noise->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_noise->setMaximumHeight(70);
    s->vbox()->addWidget(m_noise);
    v->addWidget(s);

    auto *sp = new SectionBox(I18n::tr("ra_nc_section"), page);
    m_ncPlot = new MiniPlot(sp);
    m_ncPlot->setLabels(I18n::tr("ra_octave_hz"), "SPL [dB]");
    m_ncPlot->setXTickPow10(true);
    m_ncPlot->setMinimumHeight(160);
    sp->vbox()->addWidget(m_ncPlot);
    m_ncBadge = new QLabel(sp);
    m_ncBadge->setStyleSheet("font-weight:600; font-size:13px;");
    sp->vbox()->addWidget(m_ncBadge);
    auto *guide = new QLabel(I18n::tr("ra_nc_guide"), sp);
    guide->setWordWrap(true);
    sp->vbox()->addWidget(guide);
    v->addWidget(sp);
    v->addStretch(1);

    connect(m_noise, &QTableWidget::cellChanged, this, [this] {
        if (m_updating) return;
        AcousticOpts &a = m_p->acoustic();
        for (int b = 0; b < 7; ++b)
            if (auto *it = m_noise->item(0, b))
                a.noiseLevels[b] = it->text().toDouble();
        recomputeAll();
        m_p->touch();
    });
    return page;
}

QWidget *RoomAcousticsTab::buildDefectsPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setSpacing(8);

    auto *s = new SectionBox(I18n::tr("ra_defect_section"), page);
    auto *hint = new QLabel(I18n::tr("ra_defect_hint"), s);
    hint->setWordWrap(true);
    s->vbox()->addWidget(hint);
    m_defects = new QTableWidget(0, 4, s);
    m_defects->setHorizontalHeaderLabels({
        I18n::tr("ra_defect"), I18n::tr("ra_place"),
        I18n::tr("ra_cause"), I18n::tr("ra_severity") });
    m_defects->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_defects->verticalHeader()->setVisible(false);
    m_defects->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_defects->setMinimumHeight(140);
    s->vbox()->addWidget(m_defects);
    v->addWidget(s);

    auto *sr = new SectionBox(I18n::tr("ra_recommend_section"), page);
    m_recommend = new QLabel(sr);
    m_recommend->setWordWrap(true);
    m_recommend->setTextFormat(Qt::RichText);
    sr->vbox()->addWidget(m_recommend);
    v->addWidget(sr);
    v->addStretch(1);
    return page;
}

// ── model → widgets ─────────────────────────────────────────────────────────
void RoomAcousticsTab::refresh()
{
    m_updating = true;
    const AcousticOpts &a = m_p->acoustic();

    m_roomL->setValue(a.roomL);
    m_roomW->setValue(a.roomW);
    m_roomH->setValue(a.roomH);
    m_volume->setValue(a.volume);
    m_surface->setValue(a.surface);
    m_occupancy->setCurrentIndex(qBound(0, a.occupancy, 2));
    m_formula->setCurrentIndex(qBound(0, a.rtFormula, 1));

    // 吸音バジェット表
    m_budget->setRowCount(a.absorption.size());
    for (int r = 0; r < a.absorption.size(); ++r) {
        const AbsorptionRow &row = a.absorption[r];
        auto *en = new QTableWidgetItem;
        en->setCheckState(row.enabled ? Qt::Checked : Qt::Unchecked);
        en->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        m_budget->setItem(r, 0, en);
        auto *nm = new QTableWidgetItem(row.name);
        m_budget->setItem(r, 1, nm);
        const bool air = row.role == AbsorptionRow::Air;
        auto num = [air](double v, bool editable) {
            auto *it = new QTableWidgetItem(
                air && !editable ? QStringLiteral("—")
                                 : QString::number(v, 'g', 4));
            if (air && !editable)
                it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            return it;
        };
        m_budget->setItem(r, 2, num(row.area, false));
        m_budget->setItem(r, 3, num(row.alpha[0], false));
        m_budget->setItem(r, 4, num(row.alpha[2], false));
        m_budget->setItem(r, 5, num(row.alpha[3], false));
        m_budget->setItem(r, 6, num(row.alpha[5], false));
        // A@1k (Air は airA を直接編集)
        const double A1k = air
            ? row.airA
            : row.alpha[3] * row.area
              * (row.role == AbsorptionRow::Audience
                     ? occupancyFactor(a.occupancy) : 1.0);
        auto *aItem = new QTableWidgetItem(QString::number(A1k, 'f', 0));
        if (!air) aItem->setFlags(aItem->flags() & ~Qt::ItemIsEditable);
        m_budget->setItem(r, 7, aItem);
    }

    // 騒音レベル
    for (int b = 0; b < 7; ++b)
        m_noise->setItem(0, b, new QTableWidgetItem(
            QString::number(a.noiseLevels[b], 'f', 0)));

    m_updating = false;
    recomputeAll();
}

// A=αS@1k 派生列のみ再表示 (cellChanged の再入は m_updating で防ぐ)
void RoomAcousticsTab::refreshBudgetDerived()
{
    const AcousticOpts &a = m_p->acoustic();
    m_updating = true;
    for (int r = 0; r < m_budget->rowCount() && r < a.absorption.size(); ++r) {
        const AbsorptionRow &row = a.absorption[r];
        if (row.role == AbsorptionRow::Air) continue;   // airA は編集セル
        const double A1k = row.alpha[3] * row.area
            * (row.role == AbsorptionRow::Audience
                   ? occupancyFactor(a.occupancy) : 1.0);
        if (auto *it = m_budget->item(r, 7))
            it->setText(QString::number(A1k, 'f', 0));
    }
    m_updating = false;
}

void RoomAcousticsTab::applyBudgetTable()
{
    AcousticOpts &a = m_p->acoustic();
    for (int r = 0; r < m_budget->rowCount() && r < a.absorption.size(); ++r) {
        AbsorptionRow &row = a.absorption[r];
        if (auto *en = m_budget->item(r, 0))
            row.enabled = en->checkState() == Qt::Checked;
        if (auto *nm = m_budget->item(r, 1))
            row.name = nm->text();
        auto cell = [this, r](int c) {
            auto *it = m_budget->item(r, c);
            return it ? it->text() : QString();
        };
        if (row.role == AbsorptionRow::Air) {
            row.airA = cell(7).toDouble();
        } else {
            row.area = cell(2).toDouble();
            row.alpha[0] = cell(3).toDouble();
            row.alpha[2] = cell(4).toDouble();
            row.alpha[3] = cell(5).toDouble();
            row.alpha[5] = cell(6).toDouble();
            // 未表示帯域 (250Hz, 2kHz) は隣接帯域の平均で補間
            row.alpha[1] = (row.alpha[0] + row.alpha[2]) / 2.0;
            row.alpha[4] = (row.alpha[3] + row.alpha[5]) / 2.0;
        }
    }
}

// ── 派生値の再計算 ──────────────────────────────────────────────────────────
void RoomAcousticsTab::recomputeAll()
{
    const AcousticOpts &a = m_p->acoustic();

    // カバレッジ
    m_map->recompute();
    static const char *metricName[4] = { "G", "C80", "STI", "RT60" };
    const double sd = m_map->stddev();
    const bool uniform = (m_metricBox->currentIndex() == 2)
        ? sd < 0.08 : sd < 3.0;
    m_covStats->setText(QStringLiteral(
        "<b>%1</b><br>%2: %3<br>σ: ±%4<br>%5: %6<br><br>○ = %7 (4)")
        .arg(QString::fromUtf8(metricName[m_metricBox->currentIndex()]),
             I18n::tr("ra_mean"), QString::number(m_map->mean(), 'f', 2),
             QString::number(sd, 'f', 2),
             I18n::tr("ra_uniformity"),
             uniform ? I18n::tr("ra_good") : I18n::tr("ra_check"),
             I18n::tr("ra_receiver")));

    // 席別表
    double src[3];
    sourcePos(src);
    const int band = qMin(5, m_bandBox->currentIndex());
    const double T = rt60(a, band);
    for (int i = 0; i < 4; ++i) {
        double rcv[3];
        receiverPos(i, rcv);
        const double dx = rcv[0]-src[0], dy = rcv[1]-src[1], dz = rcv[2]-src[2];
        const double r = std::sqrt(dx*dx + dy*dy + dz*dz);
        const SeatMetrics m = seatMetrics(r, T, a.volume);
        m_seatTable->setItem(i, 0, new QTableWidgetItem(
            I18n::tr(kReceivers[i].key)));
        m_seatTable->setItem(i, 1, new QTableWidgetItem(QString::number(m.G, 'f', 1)));
        m_seatTable->setItem(i, 2, new QTableWidgetItem(QString::number(m.C80, 'f', 1)));
        m_seatTable->setItem(i, 3, new QTableWidgetItem(QString::number(m.D50, 'f', 2)));
        m_seatTable->setItem(i, 4, new QTableWidgetItem(QString::number(m.STI, 'f', 2)));
        m_seatTable->setItem(i, 5, new QTableWidgetItem(QString::number(m.RT, 'f', 2)));
        const QString verdict = m.STI >= 0.60 ? I18n::tr("ra_excellent")
                              : m.STI >= 0.45 ? I18n::tr("ra_good")
                                              : I18n::tr("ra_fair");
        m_seatTable->setItem(i, 6, new QTableWidgetItem(verdict));
    }

    // エコーグラム
    double rcv[3];
    receiverPos(m_rcvBox->currentIndex(), rcv);
    const QVector<Reflection> refl = echogram(a, src, rcv);
    MiniSeries direct, earlyS, lateS;
    direct.color = QColor("#DC2626");
    earlyS.color = QColor("#2563EB");
    lateS.color = QColor("#9CA3AF");
    direct.label = I18n::tr("ra_direct");
    earlyS.label = I18n::tr("ra_early");
    lateS.label = I18n::tr("ra_late");
    for (const Reflection &r : refl) {
        if (r.surface.isEmpty()) direct.pts.push_back({ r.timeMs, 0.0 });
        else if (r.early) earlyS.pts.push_back({ r.timeMs, r.levelDb });
        else lateS.pts.push_back({ r.timeMs, r.levelDb });
    }
    m_echoPlot->setYRange(-30, 2);
    m_echoPlot->setSeries({ lateS, earlyS, direct });
    m_itdgLabel->setText(QStringLiteral("ITDG = %1 ms  (%2)")
        .arg(QString::number(itdgMs(refl), 'f', 1),
             itdgMs(refl) < 25 ? I18n::tr("ra_itdg_good")
                               : I18n::tr("ra_itdg_far")));

    m_reflTable->setRowCount(refl.size());
    for (int i = 0; i < refl.size(); ++i) {
        const Reflection &r = refl[i];
        m_reflTable->setItem(i, 0, new QTableWidgetItem(
            r.surface.isEmpty() ? I18n::tr("ra_direct")
                                : QStringLiteral("R%1").arg(i)));
        m_reflTable->setItem(i, 1, new QTableWidgetItem(
            QString::number(r.timeMs, 'f', 1)));
        m_reflTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(r.levelDb, 'f', 1)));
        m_reflTable->setItem(i, 3, new QTableWidgetItem(
            r.surface.isEmpty() ? QStringLiteral("—") : r.surface));
        QString verdict = QStringLiteral("—");
        if (!r.surface.isEmpty()) {
            if (r.timeMs > 50 && r.levelDb > -10)
                verdict = I18n::tr("ra_echo_risk");
            else if (r.timeMs <= 50)
                verdict = I18n::tr("ra_beneficial");
        }
        m_reflTable->setItem(i, 4, new QTableWidgetItem(verdict));
    }

    // RT60 帯域プロット
    MiniSeries rt;
    rt.color = QColor("#2E8B57");
    rt.markers = true;
    for (int b = 0; b < 6; ++b)
        rt.pts.push_back({ std::log10(kBandHz[b]), rt60(a, b) });
    m_rtPlot->setYRange(0, 2.6);
    m_rtPlot->setSeries({ rt });
    const double tMid = (rt60(a, 2) + rt60(a, 3)) / 2.0;
    m_rtBadge->setText(QStringLiteral("RT60(mid) = %1 s   (A@1k = %2 Sabin)")
        .arg(QString::number(tMid, 'f', 2))
        .arg(QString::number(totalAbsorption(a, 3), 'f', 0)));

    // NC
    const int nc = ncRating(a.noiseLevels);
    MiniSeries meas, ref;
    meas.color = QColor("#2E8B57");
    meas.markers = true;
    meas.label = I18n::tr("ra_measured");
    const int refNc = qBound(15, ((nc + 4) / 5) * 5, 70);
    ref.color = QColor("#DC2626");
    ref.dashed = true;
    ref.label = QStringLiteral("NC-%1").arg(refNc);
    const QVector<double> curve = ncCurve(refNc);
    static const double octHz[7] = { 63, 125, 250, 500, 1000, 2000, 4000 };
    for (int b = 0; b < 7; ++b) {
        meas.pts.push_back({ std::log10(octHz[b]), a.noiseLevels[b] });
        if (b < curve.size())
            ref.pts.push_back({ std::log10(octHz[b]), curve[b] });
    }
    m_ncPlot->setSeries({ ref, meas });
    m_ncBadge->setText(QStringLiteral("NC-%1   (%2)")
        .arg(nc)
        .arg(nc <= 25 ? I18n::tr("ra_nc_hall_ok") : I18n::tr("ra_nc_high")));

    // 障害検出
    const QVector<Defect> defects = detectDefects(a, src, rcv);
    m_defects->setRowCount(defects.size());
    static const char *sev[3] = { "低", "中", "高" };
    for (int i = 0; i < defects.size(); ++i) {
        m_defects->setItem(i, 0, new QTableWidgetItem(defects[i].name));
        m_defects->setItem(i, 1, new QTableWidgetItem(defects[i].place));
        m_defects->setItem(i, 2, new QTableWidgetItem(defects[i].cause));
        m_defects->setItem(i, 3, new QTableWidgetItem(
            QString::fromUtf8(sev[qBound(0, defects[i].severity, 2)])));
    }
    QString rec;
    bool flutter = false, echo = false;
    for (const Defect &d : defects) {
        if (d.name.contains(QString::fromUtf8("フラッター"))) flutter = true;
        else echo = true;
    }
    if (flutter) rec += "• " + I18n::tr("ra_rec_flutter") + "<br>";
    if (echo)    rec += "• " + I18n::tr("ra_rec_echo") + "<br>";
    if (rec.isEmpty()) rec = I18n::tr("ra_rec_none");
    rec += "<br><i>" + I18n::tr("ra_defect_note") + "</i>";
    m_recommend->setText(rec);
}

// ── レポート出力 ────────────────────────────────────────────────────────────
void RoomAcousticsTab::exportReport()
{
    const QString path = QFileDialog::getSaveFileName(
        this, I18n::tr("ra_export_report"), "room_acoustics_report.md",
        "Markdown (*.md);;Text (*.txt)");
    if (path.isEmpty()) return;

    const AcousticOpts &a = m_p->acoustic();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);

    out << "# " << I18n::tr("ra_report_title") << "\n\n";
    out << "## " << I18n::tr("ra_reverb_section") << "\n";
    out << QStringLiteral("- V = %1 m³, S = %2 m², %3\n")
        .arg(a.volume).arg(a.surface)
        .arg(a.rtFormula ? "Eyring" : "Sabine");
    out << "\n| f [Hz] | A [Sabin] | RT60 [s] |\n|---|---|---|\n";
    for (int b = 0; b < 6; ++b)
        out << QStringLiteral("| %1 | %2 | %3 |\n")
            .arg(kBandHz[b])
            .arg(totalAbsorption(a, b), 0, 'f', 0)
            .arg(rt60(a, b), 0, 'f', 2);

    double src[3], rcv[3];
    sourcePos(src);
    out << "\n## " << I18n::tr("ra_seat_section") << "\n";
    out << "| " << I18n::tr("ra_receiver")
        << " | G | C80 | D50 | STI | RT60 |\n|---|---|---|---|---|---|\n";
    const double T = rt60(a, 3);
    for (int i = 0; i < 4; ++i) {
        receiverPos(i, rcv);
        const double dx = rcv[0]-src[0], dy = rcv[1]-src[1], dz = rcv[2]-src[2];
        const SeatMetrics m = seatMetrics(std::sqrt(dx*dx+dy*dy+dz*dz), T, a.volume);
        out << QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 |\n")
            .arg(I18n::tr(kReceivers[i].key))
            .arg(m.G, 0, 'f', 1).arg(m.C80, 0, 'f', 1)
            .arg(m.D50, 0, 'f', 2).arg(m.STI, 0, 'f', 2)
            .arg(m.RT, 0, 'f', 2);
    }

    out << "\n## NC\n- " << QStringLiteral("NC-%1\n").arg(ncRating(a.noiseLevels));

    receiverPos(m_rcvBox->currentIndex(), rcv);
    const QVector<Defect> defects = detectDefects(a, src, rcv);
    out << "\n## " << I18n::tr("ra_defect_section") << "\n";
    if (defects.isEmpty()) out << "- " << I18n::tr("ra_rec_none") << "\n";
    for (const Defect &d : defects)
        out << QStringLiteral("- %1 (%2): %3\n")
            .arg(d.name, d.place, d.cause);
    out << "\n> " << I18n::tr("ra_model_hint") << "\n";
}
