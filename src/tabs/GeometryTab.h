// GeometryTab.h — geometry unit editor (物体形状タブ) + STL import + voxelize.
// Maps 1:1 to the "geometry =" lines; unit order = ユニット番号 (later wins).
#pragma once
#include <QScrollArea>
#include "../io/StlImporter.h"

class QTableWidget;
class QLabel;
class QSpinBox;
class QPushButton;

namespace ofd {

class Project;
class UnitNav;

class GeometryTab : public QScrollArea {
    Q_OBJECT
public:
    explicit GeometryTab(Project *project, QWidget *parent = nullptr);

private slots:
    void refresh();
    void importStl();
    void voxelizeImported();

private:
    void applyTable();

    Project      *m_p;
    bool          m_updating = false;
    QTableWidget *m_table;
    UnitNav      *m_nav;
    QLabel       *m_importInfo;
    QSpinBox     *m_voxMat = nullptr;     // material id assigned to voxels
    QPushButton  *m_voxBtn = nullptr;
    ImportedMesh  m_lastMesh;             // most recently imported STL
    bool          m_hasMesh = false;
};

} // namespace ofd
