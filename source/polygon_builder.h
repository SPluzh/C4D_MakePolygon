#ifndef POLYGON_BUILDER_H__
#define POLYGON_BUILDER_H__

#include "c4d.h"
#include "c4d_basedraw.h"
#include "c4d_baseobject.h"

namespace cinema
{

struct MakePolygonResult
{
    Bool success = false;
    Int32 newPolyIndex = NOTOK;
    String message;
};

class PolygonBuilder
{
public:
    /// Main entry point: analyzes selection in Point or Edge mode and creates polygon / closes hole.
    static MakePolygonResult Execute(BaseDocument* doc, PolygonObject* op);

    /// Point Mode handler: creates Tri/Quad/N-gon from selected points.
    static MakePolygonResult MakeFromPoints(BaseDocument* doc, PolygonObject* op, BaseDraw* bd);

    /// Edge Mode handler: closes hole from boundary loop or builds quad from 2 edges.
    static MakePolygonResult MakeFromEdges(BaseDocument* doc, PolygonObject* op, BaseDraw* bd);

public:
    struct UndirectedEdge
    {
        Int32 u = NOTOK;
        Int32 v = NOTOK;
        Int32 polyIndex = NOTOK;
        Int32 edgeSide = NOTOK;

        Bool operator==(const UndirectedEdge& other) const
        {
            return (u == other.u && v == other.v) || (u == other.v && v == other.u);
        }
    };

    /// Extracts unique selected edges from op->GetEdgeS().
    static Bool GetSelectedUndirectedEdges(PolygonObject* op, maxon::BaseArray<UndirectedEdge>& edges);

    /// Checks if edge (u, v) is on an open mesh boundary.
    static Bool IsBoundaryEdge(Neighbor& neighbor, Int32 u, Int32 v, Int32& outPoly);

    /// Walks along connected boundary edges to find a closed loop of vertices.
    static Bool TraceBoundaryLoop(PolygonObject* op, Neighbor& neighbor, Int32 startU, Int32 startV, maxon::BaseArray<Int32>& outLoopVertices);

    /// Modo 14+ feature: creates a quad between two selected edges.
    static MakePolygonResult MakeQuadFromTwoEdges(BaseDocument* doc, PolygonObject* op, Neighbor& neighbor, const UndirectedEdge& e1, const UndirectedEdge& e2, BaseDraw* bd);

    /// Cyclic sorting of coplanar vertices around their centroid to prevent self-intersecting quads/ngons.
    static Bool SortPointsCyclic(PolygonObject* op, maxon::BaseArray<Int32>& points, Vector& outNormal);

    /// Ensures winding order matches adjacent faces (manifold consistency) or faces viewport camera.
    static void EnsureCorrectWinding(PolygonObject* op, Neighbor& neighbor, maxon::BaseArray<Int32>& loopVertices, BaseDraw* bd);

    /// Computes face normal for a list of polygon vertices.
    static Vector CalculatePolygonNormal(PolygonObject* op, const maxon::BaseArray<Int32>& vertices);

    /// Inserts the polygon (Tri, Quad, or N-gon) into the mesh and commits changes.
    static Bool InsertPolygon(BaseDocument* doc, PolygonObject* op, const maxon::BaseArray<Int32>& vertices, Int32& outPolyIdx);
};

} // namespace cinema

#endif // POLYGON_BUILDER_H__
