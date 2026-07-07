// Voxelizer.cpp
#include "Voxelizer.h"
#include "StlImporter.h"
#include "../core/MeshAxis.h"

#include <QObject>
#include <cmath>

using namespace ofd;

// Expand a MeshAxis into the list of cell boundary coordinates
// (totalCells()+1 entries) and the matching cell centers.
static void expandAxis(const MeshAxis &ax,
                       QVector<double> &bounds, QVector<double> &centers)
{
    bounds.clear();
    centers.clear();
    if (ax.nodes.isEmpty()) return;
    bounds.push_back(ax.nodes[0]);
    for (int i = 0; i < ax.divs.size(); ++i) {
        const double a = ax.nodes[i], b = ax.nodes[i + 1];
        const int n = ax.divs[i];
        for (int k = 1; k <= n; ++k)
            bounds.push_back(a + (b - a) * k / n);
    }
    centers.reserve(bounds.size() - 1);
    for (int i = 0; i + 1 < bounds.size(); ++i)
        centers.push_back(0.5 * (bounds[i] + bounds[i + 1]));
}

// Ray (origin o, direction RAY_DIR) vs triangle (v0,v1,v2), Möller–Trumbore.
// Returns true if the ray crosses the triangle at parametric distance > 0.
//
// RAY_DIR is tilted slightly off the X axis so that for axis-aligned input
// meshes (e.g. a box STL) the ray does not graze a face's shared edges or
// vertices — the classic degeneracy that makes a pure (1,0,0) ray miss the
// triangulation diagonal and mis-classify whole rows of cells.
static const double RAY_DIR[3] = { 1.0, 7.3e-4, 3.1e-4 };

static bool rayHitsForward(double px, double py, double pz, const float *t)
{
    const double e1x = t[3] - t[0], e1y = t[4] - t[1], e1z = t[5] - t[2];
    const double e2x = t[6] - t[0], e2y = t[7] - t[1], e2z = t[8] - t[2];
    const double dx = RAY_DIR[0], dy = RAY_DIR[1], dz = RAY_DIR[2];
    const double hx = dy * e2z - dz * e2y;
    const double hy = dz * e2x - dx * e2z;
    const double hz = dx * e2y - dy * e2x;
    const double a = e1x * hx + e1y * hy + e1z * hz;
    if (std::fabs(a) < 1e-18) return false;             // ray parallel to tri
    const double f = 1.0 / a;
    const double sx = px - t[0], sy = py - t[1], sz = pz - t[2];
    const double u = f * (sx * hx + sy * hy + sz * hz);
    if (u < 0.0 || u > 1.0) return false;
    const double qx = sy * e1z - sz * e1y;
    const double qy = sz * e1x - sx * e1z;
    const double qz = sx * e1y - sy * e1x;
    const double v = f * (dx * qx + dy * qy + dz * qz);
    if (v < 0.0 || u + v > 1.0) return false;
    const double dist = f * (e2x * qx + e2y * qy + e2z * qz);
    return dist > 1e-12;                                 // forward only
}

VoxelResult Voxelizer::voxelize(const ImportedMesh &mesh,
                                const MeshAxis &mx,
                                const MeshAxis &my,
                                const MeshAxis &mz,
                                int materialId,
                                qint64 cellCap)
{
    VoxelResult r;
    if (mesh.numTriangles <= 0) { r.error = QObject::tr("empty mesh"); return r; }
    if (!mx.isValid() || !my.isValid() || !mz.isValid()) {
        r.error = QObject::tr("invalid mesh axes");
        return r;
    }

    QVector<double> xb, yb, zb, xc, yc, zc;
    expandAxis(mx, xb, xc);
    expandAxis(my, yb, yc);
    expandAxis(mz, zb, zc);
    r.nx = xc.size(); r.ny = yc.size(); r.nz = zc.size();

    const qint64 cells = qint64(r.nx) * r.ny * r.nz;
    if (cells > cellCap) {
        r.error = QObject::tr("grid too large for staircase voxelization "
                              "(%1 cells > cap %2); coarsen the mesh or build "
                              "with -DUSE_LIBIGL=ON").arg(cells).arg(cellCap);
        return r;
    }

    // Skip cells entirely outside the mesh bounding box (fast reject).
    const float *V = mesh.vertices.constData();
    const int T = mesh.numTriangles;

    for (int k = 0; k < r.nz; ++k) {
        const double pz = zc[k];
        if (pz < mesh.bbox[2] || pz > mesh.bbox[5]) continue;
        for (int j = 0; j < r.ny; ++j) {
            const double py = yc[j];
            if (py < mesh.bbox[1] || py > mesh.bbox[4]) continue;

            // Count forward crossings once per (j,k) ray is not possible
            // because parity depends on x; instead test each cell's center.
            int runStart = -1;
            for (int i = 0; i < r.nx; ++i) {
                bool inside = false;
                const double px = xc[i];
                if (px >= mesh.bbox[0] && px <= mesh.bbox[3]) {
                    int crossings = 0;
                    for (int t = 0; t < T; ++t)
                        if (rayHitsForward(px, py, pz, V + 9 * t))
                            ++crossings;
                    inside = (crossings & 1) != 0;
                }
                if (inside && runStart < 0) {
                    runStart = i;
                } else if (!inside && runStart >= 0) {
                    Geometry g;
                    g.shape = 1;
                    g.materialId = materialId;
                    g.g[0] = xb[runStart]; g.g[1] = xb[i];
                    g.g[2] = yb[j];        g.g[3] = yb[j + 1];
                    g.g[4] = zb[k];        g.g[5] = zb[k + 1];
                    r.bricks.push_back(g);
                    r.occupied += i - runStart;
                    runStart = -1;
                }
            }
            if (runStart >= 0) {
                Geometry g;
                g.shape = 1;
                g.materialId = materialId;
                g.g[0] = xb[runStart]; g.g[1] = xb[r.nx];
                g.g[2] = yb[j];        g.g[3] = yb[j + 1];
                g.g[4] = zb[k];        g.g[5] = zb[k + 1];
                r.bricks.push_back(g);
                r.occupied += r.nx - runStart;
            }
        }
    }

    r.ok = true;
    return r;
}
