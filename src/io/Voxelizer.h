// Voxelizer.h — staircase voxelization of a triangle mesh onto the Yee grid.
//
// docs/libigl-integration.md が設計する「STL→Yee格子」のうち、libigl 非依存で
// 動作する staircase (階段近似) 版を実装する。各 Yee セル中心から +X 方向に
// レイを飛ばし、メッシュ三角形との交差回数の偶奇で内外を判定する
// (ray casting parity test)。占有セルは X 方向に連続する区間ごとに直方体
// (geometry shape=1) へまとめ、Project に追加できる形で返す。
//
// libigl ビルド (-DUSE_LIBIGL=ON) では fast_winding_number による
// より正確な内外判定・共形メッシュへ差し替え可能 (将来拡張)。
#pragma once
#include <QVector>
#include "../core/Geometry.h"

namespace ofd {

struct MeshAxis;
struct ImportedMesh;

struct VoxelResult {
    bool    ok = false;
    QString error;
    int     nx = 0, ny = 0, nz = 0;     // mesh cell counts used
    qint64  occupied = 0;               // number of occupied Yee cells
    QVector<Geometry> bricks;           // staircase geometry (X-runs merged)
};

class Voxelizer {
public:
    // Voxelize `mesh` onto the project's Yee grid (mx/my/mz).
    // Occupied cells are assigned `materialId`. `cellCap` guards against
    // runaway grids (returns ok=false with an error if exceeded).
    static VoxelResult voxelize(const ImportedMesh &mesh,
                                const MeshAxis &mx,
                                const MeshAxis &my,
                                const MeshAxis &mz,
                                int materialId,
                                qint64 cellCap = 8'000'000);
};

} // namespace ofd
