// RoomAcousticsTab.h — ホール解析タブ (room-acoustics.jsx 相当)。
// 5つのサブタブ:
//   客席カバレッジ — Barron統計モデルで G/C80/STI/RT を客席分布表示
//   エコーグラム   — シューボックス1次鏡像法の反射音列 + ITDG
//   残響計算       — Sabine/Eyring + 吸音バジェット + 帯域別 RT60
//   暗騒音 NC/NR   — オクターブ帯域騒音 vs NC 曲線
//   音響障害診断   — フラッター/ロングディレイエコー検出 + 改善提案
// 音響ドメイン選択時のみ表示される。設定は AcousticOpts (.ofdx) に永続化。
#pragma once
#include <QScrollArea>
#include "../core/RoomAcoustics.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace ofd {

class Project;
class MiniPlot;

// 扇形ホールの客席分布マップ (Barron 推定値をセル色で表示)
class CoverageMap : public QWidget {
    Q_OBJECT
public:
    explicit CoverageMap(Project *project, QWidget *parent = nullptr);
    void setMetric(int m)  { m_metric = m; recompute(); }
    void setBand(int b)    { m_band = b; recompute(); }
    void recompute();
    double mean() const  { return m_mean; }
    double stddev() const { return m_std; }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    double cellValue(double r) const;
    Project *m_p;
    int m_metric = 0;    // 0=G(SPL), 1=C80, 2=STI, 3=RT
    int m_band = 3;      // 0..5 帯域 / 6=平均
    QVector<double> m_values;   // 計算済みセル値 (描画とセットで更新)
    double m_mean = 0, m_std = 0;
};

class RoomAcousticsTab : public QScrollArea {
    Q_OBJECT
public:
    explicit RoomAcousticsTab(Project *project, QWidget *parent = nullptr);

private slots:
    void refresh();          // model → widgets
    void recomputeAll();     // 派生値 (RT/エコーグラム/NC/障害) を再計算
    void exportReport();

private:
    QWidget *buildCoveragePage();
    QWidget *buildEchogramPage();
    QWidget *buildReverbPage();
    QWidget *buildNoisePage();
    QWidget *buildDefectsPage();
    void applyBudgetTable();
    void refreshBudgetDerived();
    void receiverPos(int index, double out[3]) const;
    void sourcePos(double out[3]) const;

    Project    *m_p;
    bool        m_updating = false;
    QTabWidget *m_tabs;

    // coverage
    CoverageMap *m_map;
    QComboBox   *m_metricBox, *m_bandBox;
    QLabel      *m_covStats;
    QTableWidget *m_seatTable;

    // echogram
    QComboBox *m_rcvBox;
    MiniPlot  *m_echoPlot;
    QLabel    *m_itdgLabel;
    QTableWidget *m_reflTable;

    // reverb
    QDoubleSpinBox *m_roomL, *m_roomW, *m_roomH;
    QDoubleSpinBox *m_volume, *m_surface;
    QComboBox *m_occupancy, *m_formula;
    QTableWidget *m_budget;
    MiniPlot  *m_rtPlot;
    QLabel    *m_rtBadge;

    // noise
    QTableWidget *m_noise;
    MiniPlot  *m_ncPlot;
    QLabel    *m_ncBadge;

    // defects
    QTableWidget *m_defects;
    QLabel    *m_recommend;
};

} // namespace ofd
