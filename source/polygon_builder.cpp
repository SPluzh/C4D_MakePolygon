#include "polygon_builder.h"
#include "c4d_libs/lib_modeling.h"
#include <algorithm>

namespace cinema
{

MakePolygonResult PolygonBuilder::Execute(BaseDocument* doc, PolygonObject* op)
{
    MakePolygonResult res;
    if (!doc || !op)
    {
        res.message = "No active polygon object."_s;
        return res;
    }

    BaseDraw* bd = doc->GetActiveBaseDraw();
    Int32 mode = doc->GetMode();

    if (mode == Mpoints)
    {
        return MakeFromPoints(doc, op, bd);
    }
    else if (mode == Medges)
    {
        return MakeFromEdges(doc, op, bd);
    }
    else
    {
        // If in polygon or object mode, check if points or edges are selected
        const BaseSelect* edgeSel = op->GetEdgeS();
        if (edgeSel && edgeSel->GetCount() > 0)
        {
            return MakeFromEdges(doc, op, bd);
        }

        const BaseSelect* ptSel = op->GetPointS();
        if (ptSel && ptSel->GetCount() >= 3)
        {
            return MakeFromPoints(doc, op, bd);
        }

        res.message = "Please switch to Point Mode (select 3+ points) or Edge Mode (select edges / hole)."_s;
        return res;
    }
}

MakePolygonResult PolygonBuilder::MakeFromPoints(BaseDocument* doc, PolygonObject* op, BaseDraw* bd)
{
    MakePolygonResult res;
    const BaseSelect* ptSel = op->GetPointS();
    if (!ptSel || ptSel->GetCount() < 3)
    {
        res.message = "Make Polygon: Select at least 3 points."_s;
        return res;
    }

    maxon::BaseArray<Int32> points;
    Int32 seg = 0, a, b;
    while (ptSel->GetRange(seg++, LIMIT<Int32>::MAX, &a, &b))
    {
        for (Int32 i = a; i <= b; ++i)
        {
            points.Append(i) iferr_ignore("MakePolygon");
        }
    }

    if (points.GetCount() < 3)
    {
        res.message = "Make Polygon: Select at least 3 points."_s;
        return res;
    }

    // Sort points cyclically around their planar centroid
    Vector normal(0.0);
    if (!SortPointsCyclic(op, points, normal))
    {
        res.message = "Make Polygon: Selected points are collinear or invalid."_s;
        return res;
    }

    // Initialize neighbor structure for normal alignment
    Int32 ptCount = op->GetPointCount();
    Int32 polyCount = op->GetPolygonCount();
    const CPolygon* polys = op->GetPolygonR();
    Neighbor neighbor;
    if (neighbor.Init(ptCount, polys, polyCount, nullptr))
    {
        EnsureCorrectWinding(op, neighbor, points, bd);
    }

    Int32 newPolyIdx = NOTOK;
    if (!InsertPolygon(doc, op, points, newPolyIdx))
    {
        res.message = "Make Polygon: Failed to create polygon."_s;
        return res;
    }

    // Select the new polygon
    BaseSelect* polySel = op->GetWritablePolygonS();
    if (polySel && newPolyIdx != NOTOK)
    {
        polySel->DeselectAll();
        polySel->Select(newPolyIdx);
    }

    res.success = true;
    res.newPolyIndex = newPolyIdx;
    res.message = "Make Polygon: Successfully created polygon."_s;
    return res;
}

MakePolygonResult PolygonBuilder::MakeFromEdges(BaseDocument* doc, PolygonObject* op, BaseDraw* bd)
{
    MakePolygonResult res;
    maxon::BaseArray<UndirectedEdge> edges;
    if (!GetSelectedUndirectedEdges(op, edges) || edges.GetCount() == 0)
    {
        res.message = "Make Polygon: No edges selected."_s;
        return res;
    }

    Int32 ptCount = op->GetPointCount();
    Int32 polyCount = op->GetPolygonCount();
    const CPolygon* polys = op->GetPolygonR();
    Neighbor neighbor;
    if (!neighbor.Init(ptCount, polys, polyCount, nullptr))
    {
        res.message = "Make Polygon: Failed to initialize mesh topology."_s;
        return res;
    }

    // Scenario 1: Exactly 1 edge selected -> Close Boundary Hole (Fill Hole)
    if (edges.GetCount() == 1)
    {
        const UndirectedEdge& e = edges[0];
        Int32 boundaryPoly = NOTOK;
        if (!IsBoundaryEdge(neighbor, e.u, e.v, boundaryPoly))
        {
            res.message = "Make Polygon: Selected edge is interior (not on an open boundary hole)."_s;
            return res;
        }

        maxon::BaseArray<Int32> loopVertices;
        if (!TraceBoundaryLoop(op, neighbor, e.u, e.v, loopVertices) || loopVertices.GetCount() < 3)
        {
            res.message = "Make Polygon: Could not trace a closed boundary hole from this edge."_s;
            return res;
        }

        EnsureCorrectWinding(op, neighbor, loopVertices, bd);

        Int32 newPolyIdx = NOTOK;
        if (!InsertPolygon(doc, op, loopVertices, newPolyIdx))
        {
            res.message = "Make Polygon: Failed to close polygon hole."_s;
            return res;
        }

        BaseSelect* polySel = op->GetWritablePolygonS();
        if (polySel && newPolyIdx != NOTOK)
        {
            polySel->DeselectAll();
            polySel->Select(newPolyIdx);
        }

        res.success = true;
        res.newPolyIndex = newPolyIdx;
        res.message = "Make Polygon: Hole closed successfully."_s;
        return res;
    }

    // Scenario 2: Exactly 2 edges selected -> Modo 14+ Make Quad
    if (edges.GetCount() == 2)
    {
        return MakeQuadFromTwoEdges(doc, op, neighbor, edges[0], edges[1], bd);
    }

    // Scenario 3: 3 or more edges selected -> check if they form a loop or multiple boundary edges
    Bool allBoundary = true;
    for (const auto& e : edges)
    {
        Int32 bp = NOTOK;
        if (!IsBoundaryEdge(neighbor, e.u, e.v, bp))
        {
            allBoundary = false;
            break;
        }
    }

    // Build vertex connectivity graph from selected edges
    maxon::BaseArray<Int32> cycleVertices;
    maxon::BaseArray<Int32> edgePts;
    for (const auto& e : edges)
    {
        Bool hasU = false, hasV = false;
        for (Int32 pt : edgePts)
        {
            if (pt == e.u) hasU = true;
            if (pt == e.v) hasV = true;
        }
        if (!hasU) edgePts.Append(e.u) iferr_ignore("MakePolygon");
        if (!hasV) edgePts.Append(e.v) iferr_ignore("MakePolygon");
    }

    // Try tracing a closed path through selected edges
    Int32 startV = edges[0].u;
    Int32 currV = edges[0].v;
    cycleVertices.Append(startV) iferr_ignore("MakePolygon");
    cycleVertices.Append(currV) iferr_ignore("MakePolygon");

    maxon::BaseArray<Bool> usedEdge;
    usedEdge.Resize(edges.GetCount()) iferr_ignore("MakePolygon");
    for (Int32 i = 0; i < edges.GetCount(); ++i) usedEdge[i] = false;
    usedEdge[0] = true;

    Bool closed = false;
    while (!closed)
    {
        Int32 nextEdgeIdx = NOTOK;
        Int32 nextV = NOTOK;
        for (Int32 i = 0; i < edges.GetCount(); ++i)
        {
            if (usedEdge[i]) continue;
            if (edges[i].u == currV)
            {
                nextEdgeIdx = i;
                nextV = edges[i].v;
                break;
            }
            else if (edges[i].v == currV)
            {
                nextEdgeIdx = i;
                nextV = edges[i].u;
                break;
            }
        }

        if (nextEdgeIdx == NOTOK) break;

        usedEdge[nextEdgeIdx] = true;
        if (nextV == startV)
        {
            closed = true;
            break;
        }
        cycleVertices.Append(nextV) iferr_ignore("MakePolygon");
        currV = nextV;
    }

    if (closed && cycleVertices.GetCount() >= 3)
    {
        EnsureCorrectWinding(op, neighbor, cycleVertices, bd);

        Int32 newPolyIdx = NOTOK;
        if (!InsertPolygon(doc, op, cycleVertices, newPolyIdx))
        {
            res.message = "Make Polygon: Failed to create polygon from selected edge loop."_s;
            return res;
        }

        BaseSelect* polySel = op->GetWritablePolygonS();
        if (polySel && newPolyIdx != NOTOK)
        {
            polySel->DeselectAll();
            polySel->Select(newPolyIdx);
        }

        res.success = true;
        res.newPolyIndex = newPolyIdx;
        res.message = "Make Polygon: Created polygon from selected edges."_s;
        return res;
    }

    // If edges don't form a closed selection, but are boundary edges, trace boundary loop from first
    if (allBoundary)
    {
        maxon::BaseArray<Int32> loopVertices;
        if (TraceBoundaryLoop(op, neighbor, edges[0].u, edges[0].v, loopVertices) && loopVertices.GetCount() >= 3)
        {
            EnsureCorrectWinding(op, neighbor, loopVertices, bd);

            Int32 newPolyIdx = NOTOK;
            if (InsertPolygon(doc, op, loopVertices, newPolyIdx))
            {
                BaseSelect* polySel = op->GetWritablePolygonS();
                if (polySel && newPolyIdx != NOTOK)
                {
                    polySel->DeselectAll();
                    polySel->Select(newPolyIdx);
                }

                res.success = true;
                res.newPolyIndex = newPolyIdx;
                res.message = "Make Polygon: Hole closed successfully."_s;
                return res;
            }
        }
    }

    res.message = "Make Polygon: Selected edges do not form a closed loop or valid hole."_s;
    return res;
}

Bool PolygonBuilder::GetSelectedUndirectedEdges(PolygonObject* op, maxon::BaseArray<UndirectedEdge>& edges)
{
    const BaseSelect* edgeSel = op->GetEdgeS();
    if (!edgeSel || edgeSel->GetCount() == 0) return false;

    const CPolygon* polys = op->GetPolygonR();
    Int32 polyCount = op->GetPolygonCount();

    Int32 seg = 0, a, b;
    while (edgeSel->GetRange(seg++, LIMIT<Int32>::MAX, &a, &b))
    {
        for (Int32 edgeIdx = a; edgeIdx <= b; ++edgeIdx)
        {
            Int32 polyIdx = edgeIdx / 4;
            Int32 side = edgeIdx % 4;
            if (polyIdx >= polyCount) continue;

            const CPolygon& p = polys[polyIdx];
            Int32 u = NOTOK, v = NOTOK;
            if (side == 0) { u = p.a; v = p.b; }
            else if (side == 1) { u = p.b; v = p.c; }
            else if (side == 2)
            {
                if (p.c == p.d) { u = p.c; v = p.a; } // Triangle side 2 is c-a
                else { u = p.c; v = p.d; }
            }
            else if (side == 3)
            {
                if (p.c != p.d) { u = p.d; v = p.a; } // Quad side 3 is d-a
            }

            if (u == NOTOK || v == NOTOK || u == v) continue;

            Int32 minV = LMin(u, v);
            Int32 maxV = LMax(u, v);

            Bool exists = false;
            for (const auto& e : edges)
            {
                if (e.u == minV && e.v == maxV)
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
            {
                UndirectedEdge ue;
                ue.u = minV;
                ue.v = maxV;
                ue.polyIndex = polyIdx;
                ue.edgeSide = side;
                edges.Append(ue) iferr_ignore("MakePolygon");
            }
        }
    }

    return edges.GetCount() > 0;
}

Bool PolygonBuilder::IsBoundaryEdge(Neighbor& neighbor, Int32 u, Int32 v, Int32& outPoly)
{
    Int32 first = NOTOK, second = NOTOK;
    neighbor.GetEdgePolys(u, v, &first, &second);
    if (first != NOTOK && second == NOTOK)
    {
        outPoly = first;
        return true;
    }
    return false;
}

Bool PolygonBuilder::TraceBoundaryLoop(PolygonObject* op, Neighbor& neighbor, Int32 startU, Int32 startV, maxon::BaseArray<Int32>& outLoopVertices)
{
    Int32 ptCount = op->GetPointCount();
    const CPolygon* polys = op->GetPolygonR();

    Int32 polyA = NOTOK, polyB = NOTOK;
    neighbor.GetEdgePolys(startU, startV, &polyA, &polyB);
    if (polyA == NOTOK || polyB != NOTOK) return false;

    const CPolygon& p0 = polys[polyA];
    Bool polyGoesUtoV = false;
    if (p0.a == startU && p0.b == startV) polyGoesUtoV = true;
    else if (p0.b == startU && p0.c == startV) polyGoesUtoV = true;
    else if (p0.c == startU && ((p0.c != p0.d && p0.d == startV) || (p0.c == p0.d && p0.a == startV))) polyGoesUtoV = true;
    else if (p0.c != p0.d && p0.d == startU && p0.a == startV) polyGoesUtoV = true;

    Int32 origin = polyGoesUtoV ? startV : startU;
    Int32 current = polyGoesUtoV ? startU : startV;

    outLoopVertices.Append(origin) iferr_ignore("MakePolygon");
    outLoopVertices.Append(current) iferr_ignore("MakePolygon");

    Int32 maxSteps = ptCount + 2;
    Int32 steps = 0;

    while (current != origin && steps++ < maxSteps)
    {
        Int32* pPolyList = nullptr;
        Int32 polyCnt = 0;
        neighbor.GetPointPolys(current, &pPolyList, &polyCnt);

        Int32 nextVertex = NOTOK;

        for (Int32 i = 0; i < polyCnt; ++i)
        {
            Int32 pIdx = pPolyList[i];
            const CPolygon& cp = polys[pIdx];

            Int32 cand[2] = { NOTOK, NOTOK };
            if (cp.a == current) { cand[0] = (cp.c == cp.d) ? cp.c : cp.d; cand[1] = cp.b; }
            else if (cp.b == current) { cand[0] = cp.a; cand[1] = cp.c; }
            else if (cp.c == current) { cand[0] = cp.b; cand[1] = (cp.c == cp.d) ? cp.a : cp.d; }
            else if (cp.c != cp.d && cp.d == current) { cand[0] = cp.c; cand[1] = cp.a; }

            for (Int32 k = 0; k < 2; ++k)
            {
                Int32 neighborPt = cand[k];
                if (neighborPt == NOTOK || neighborPt == current) continue;

                if (outLoopVertices.GetCount() >= 2 && neighborPt == outLoopVertices[outLoopVertices.GetCount() - 2])
                    continue;

                Int32 f = NOTOK, s = NOTOK;
                neighbor.GetEdgePolys(current, neighborPt, &f, &s);
                if (f != NOTOK && s == NOTOK)
                {
                    const CPolygon& fp = polys[f];
                    Bool fpGoesNtoC = false;
                    if (fp.a == neighborPt && fp.b == current) fpGoesNtoC = true;
                    else if (fp.b == neighborPt && fp.c == current) fpGoesNtoC = true;
                    else if (fp.c == neighborPt && ((fp.c != fp.d && fp.d == current) || (fp.c == fp.d && fp.a == current))) fpGoesNtoC = true;
                    else if (fp.c != fp.d && fp.d == neighborPt && fp.a == current) fpGoesNtoC = true;

                    if (fpGoesNtoC)
                    {
                        nextVertex = neighborPt;
                        break;
                    }
                }
            }

            if (nextVertex != NOTOK) break;
        }

        if (nextVertex == NOTOK) break;

        if (nextVertex == origin)
        {
            return outLoopVertices.GetCount() >= 3;
        }

        outLoopVertices.Append(nextVertex) iferr_ignore("MakePolygon");
        current = nextVertex;
    }

    return (outLoopVertices.GetCount() >= 3 && current == origin);
}

MakePolygonResult PolygonBuilder::MakeQuadFromTwoEdges(BaseDocument* doc, PolygonObject* op, Neighbor& neighbor, const UndirectedEdge& e1, const UndirectedEdge& e2, BaseDraw* bd)
{
    MakePolygonResult res;
    const Vector* pts = op->GetPointR();

    Int32 ptCount = op->GetPointCount();
    Int32 polyCount = op->GetPolygonCount();

    Int32 shared = NOTOK;
    Int32 v1 = NOTOK, v2 = NOTOK;

    if (e1.u == e2.u) { shared = e1.u; v1 = e1.v; v2 = e2.v; }
    else if (e1.u == e2.v) { shared = e1.u; v1 = e1.v; v2 = e2.u; }
    else if (e1.v == e2.u) { shared = e1.v; v1 = e1.u; v2 = e2.v; }
    else if (e1.v == e2.v) { shared = e1.v; v1 = e1.u; v2 = e2.u; }

    maxon::BaseArray<Int32> quadVerts;

    if (shared != NOTOK)
    {
        // 2 edges meet at a corner: v1 - shared - v2
        Vector posShared = pts[shared];
        Vector posV1 = pts[v1];
        Vector posV2 = pts[v2];
        Vector pos4 = posV1 + posV2 - posShared;

        Int32 fourthVertex = NOTOK;

        // Step 1: Check topological boundary completion
        // Look for any boundary vertex connected to v1 that is also connected to v2
        Int32* pPolys1 = nullptr; Int32 c1 = 0;
        neighbor.GetPointPolys(v1, &pPolys1, &c1);
        const CPolygon* oldPolys = op->GetPolygonR();

        for (Int32 i = 0; i < c1 && fourthVertex == NOTOK; ++i)
        {
            const CPolygon& pA = oldPolys[pPolys1[i]];
            Int32 cand[4] = { pA.a, pA.b, pA.c, (pA.c != pA.d) ? pA.d : NOTOK };
            for (Int32 k = 0; k < 4; ++k)
            {
                Int32 pt = cand[k];
                if (pt == NOTOK || pt == shared || pt == v1 || pt == v2) continue;

                Int32 bp1 = NOTOK, bp2 = NOTOK;
                if (IsBoundaryEdge(neighbor, v1, pt, bp1) && IsBoundaryEdge(neighbor, v2, pt, bp2))
                {
                    fourthVertex = pt;
                    break;
                }
            }
        }

        // Step 2: Check proximity to pos4 among all vertices
        if (fourthVertex == NOTOK)
        {
            Float edgeLen = LMax((posV1 - posShared).GetLength(), (posV2 - posShared).GetLength());
            Float maxDistSq = Sqr(LMax(1.0, edgeLen * 0.25));

            Float bestDistSq = maxDistSq;
            for (Int32 i = 0; i < ptCount; ++i)
            {
                if (i == shared || i == v1 || i == v2) continue;

                Float dSq = (pts[i] - pos4).GetSquaredLength();
                if (dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    fourthVertex = i;
                }
            }
        }

        // Step 3: If no existing 4th vertex found, create a new point at pos4 (Modo 14+ behavior)
        if (fourthVertex == NOTOK)
        {
            Int32 newPtIdx = ptCount;
            if (!op->ResizeObject(ptCount + 1, polyCount))
            {
                res.message = "Make Polygon: Failed to allocate new corner vertex."_s;
                return res;
            }
            Vector* ptsW = op->GetPointW();
            ptsW[newPtIdx] = pos4;
            fourthVertex = newPtIdx;
        }

        quadVerts.Append(shared) iferr_ignore("MakePolygon");
        quadVerts.Append(v1) iferr_ignore("MakePolygon");
        quadVerts.Append(fourthVertex) iferr_ignore("MakePolygon");
        quadVerts.Append(v2) iferr_ignore("MakePolygon");
    }
    else
    {
        Int32 A = e1.u;
        Int32 B = e1.v;
        Int32 C = e2.u;
        Int32 D = e2.v;

        Float dist1 = (pts[A] - pts[C]).GetSquaredLength() + (pts[B] - pts[D]).GetSquaredLength();
        Float dist2 = (pts[A] - pts[D]).GetSquaredLength() + (pts[B] - pts[C]).GetSquaredLength();

        if (dist1 < dist2)
        {
            quadVerts.Append(A) iferr_ignore("MakePolygon");
            quadVerts.Append(B) iferr_ignore("MakePolygon");
            quadVerts.Append(D) iferr_ignore("MakePolygon");
            quadVerts.Append(C) iferr_ignore("MakePolygon");
        }
        else
        {
            quadVerts.Append(A) iferr_ignore("MakePolygon");
            quadVerts.Append(B) iferr_ignore("MakePolygon");
            quadVerts.Append(C) iferr_ignore("MakePolygon");
            quadVerts.Append(D) iferr_ignore("MakePolygon");
        }
    }

    EnsureCorrectWinding(op, neighbor, quadVerts, bd);

    Int32 newPolyIdx = NOTOK;
    if (!InsertPolygon(doc, op, quadVerts, newPolyIdx))
    {
        res.message = "Make Polygon: Failed to build quad between the two edges."_s;
        return res;
    }

    BaseSelect* polySel = op->GetWritablePolygonS();
    if (polySel && newPolyIdx != NOTOK)
    {
        polySel->DeselectAll();
        polySel->Select(newPolyIdx);
    }

    res.success = true;
    res.newPolyIndex = newPolyIdx;
    res.message = "Make Polygon: Quad created from two edges (Modo style)."_s;
    return res;
}

Bool PolygonBuilder::SortPointsCyclic(PolygonObject* op, maxon::BaseArray<Int32>& points, Vector& outNormal)
{
    Int32 count = (Int32)points.GetCount();
    if (count < 3) return false;

    const Vector* pts = op->GetPointR();

    Vector center(0.0);
    for (Int32 idx : points)
    {
        center += pts[idx];
    }
    center /= Float(count);

    Vector normal(0.0);
    for (Int32 i = 0; i < count; ++i)
    {
        Int32 cur = points[i];
        Int32 nxt = points[(i + 1) % count];
        Vector p1 = pts[cur] - center;
        Vector p2 = pts[nxt] - center;
        normal.x += (p1.y - p2.y) * (p1.z + p2.z);
        normal.y += (p1.z - p2.z) * (p1.x + p2.x);
        normal.z += (p1.x - p2.x) * (p1.y + p2.y);
    }

    Float nSq = normal.GetSquaredLength();
    if (nSq < 1e-10)
    {
        normal = Cross(pts[points[1]] - pts[points[0]], pts[points[2]] - pts[points[0]]);
        nSq = normal.GetSquaredLength();
    }

    if (nSq < 1e-10)
    {
        return false;
    }

    normal = normal.GetNormalized();

    Vector uAxis = (pts[points[0]] - center).GetNormalized();
    if (uAxis.GetSquaredLength() < 1e-10)
    {
        uAxis = Cross(normal, Vector(0.0, 1.0, 0.0));
        if (uAxis.GetSquaredLength() < 1e-10)
            uAxis = Cross(normal, Vector(1.0, 0.0, 0.0));
        uAxis = uAxis.GetNormalized();
    }
    Vector vAxis = Cross(normal, uAxis).GetNormalized();

    struct PointAngle
    {
        Int32 ptIndex;
        Float angle;
    };
    maxon::BaseArray<PointAngle> sorted;
    for (Int32 idx : points)
    {
        Vector d = pts[idx] - center;
        Float angle = maxon::ATan2(Dot(d, vAxis), Dot(d, uAxis));
        sorted.Append(PointAngle{ idx, angle }) iferr_ignore("MakePolygon");
    }

    std::sort(sorted.Begin(), sorted.End(), [](const PointAngle& a, const PointAngle& b) {
        return a.angle < b.angle;
    });

    for (Int32 i = 0; i < count; ++i)
    {
        points[i] = sorted[i].ptIndex;
    }

    outNormal = normal;
    return true;
}

void PolygonBuilder::EnsureCorrectWinding(PolygonObject* op, Neighbor& neighbor, maxon::BaseArray<Int32>& loopVertices, BaseDraw* bd)
{
    Int32 count = (Int32)loopVertices.GetCount();
    if (count < 3) return;

    const CPolygon* polys = op->GetPolygonR();

    for (Int32 i = 0; i < count; ++i)
    {
        Int32 vA = loopVertices[i];
        Int32 vB = loopVertices[(i + 1) % count];

        Int32 polyA = NOTOK, polyB = NOTOK;
        neighbor.GetEdgePolys(vA, vB, &polyA, &polyB);

        Int32 existingPoly = (polyA != NOTOK) ? polyA : polyB;
        if (existingPoly != NOTOK)
        {
            const CPolygon& p = polys[existingPoly];
            Bool polyGoesAtoB = false;
            if (p.a == vA && p.b == vB) polyGoesAtoB = true;
            else if (p.b == vA && p.c == vB) polyGoesAtoB = true;
            else if (p.c == vA && ((p.c != p.d && p.d == vB) || (p.c == p.d && p.a == vB))) polyGoesAtoB = true;
            else if (p.c != p.d && p.d == vA && p.a == vB) polyGoesAtoB = true;

            if (polyGoesAtoB)
            {
                std::reverse(loopVertices.Begin(), loopVertices.End());
                return;
            }
            else
            {
                return;
            }
        }
    }

    if (bd)
    {
        const Vector* pts = op->GetPointR();
        Vector center(0.0);
        for (Int32 idx : loopVertices) center += pts[idx];
        center /= Float(count);

        Vector camLocal = (~op->GetMg()) * bd->GetMg().off;
        Vector toCam = (camLocal - center).GetNormalized();
        Vector polyNormal = CalculatePolygonNormal(op, loopVertices);

        if (Dot(polyNormal, toCam) < 0.0)
        {
            std::reverse(loopVertices.Begin(), loopVertices.End());
        }
    }
}

Vector PolygonBuilder::CalculatePolygonNormal(PolygonObject* op, const maxon::BaseArray<Int32>& vertices)
{
    Int32 count = (Int32)vertices.GetCount();
    if (count < 3) return Vector(0.0, 1.0, 0.0);

    const Vector* pts = op->GetPointR();
    Vector normal(0.0);
    for (Int32 i = 0; i < count; ++i)
    {
        Int32 cur = vertices[i];
        Int32 nxt = vertices[(i + 1) % count];
        Vector p1 = pts[cur];
        Vector p2 = pts[nxt];
        normal.x += (p1.y - p2.y) * (p1.z + p2.z);
        normal.y += (p1.z - p2.z) * (p1.x + p2.x);
        normal.z += (p1.x - p2.x) * (p1.y + p2.y);
    }

    if (normal.GetSquaredLength() < 1e-10)
    {
        normal = Cross(pts[vertices[1]] - pts[vertices[0]], pts[vertices[2]] - pts[vertices[0]]);
    }
    return normal.GetNormalized();
}

Bool PolygonBuilder::InsertPolygon(BaseDocument* doc, PolygonObject* op, const maxon::BaseArray<Int32>& vertices, Int32& outPolyIdx)
{
    Int32 count = (Int32)vertices.GetCount();
    if (count < 3) return false;

    // 1. Direct addition for Triangles and Quads
    if (count == 3 || count == 4)
    {
        Int32 oldPolyCount = op->GetPolygonCount();
        Int32 ptCount = op->GetPointCount();

        if (!op->ResizeObject(ptCount, oldPolyCount + 1))
            return false;

        CPolygon* polys = op->GetPolygonW();
        if (count == 3)
            polys[oldPolyCount] = CPolygon(vertices[0], vertices[1], vertices[2], vertices[2]);
        else
            polys[oldPolyCount] = CPolygon(vertices[0], vertices[1], vertices[2], vertices[3]);

        outPolyIdx = oldPolyCount;
        return true;
    }

    // 2. N-gons (count >= 5): use Modeling kernel
    Modeling* mod = Modeling::Alloc();
    if (mod)
    {
        if (mod->InitObject(op))
        {
            maxon::BaseArray<Int32> tempPadr;
            tempPadr.CopyFrom(vertices) iferr_ignore("MakePolygon");
            Int32 newNgon = mod->CreateNgon(op, tempPadr.GetFirst(), count, MODELING_SETNGON_FLAG_EMPTY);
            if (newNgon != NOTOK)
            {
                if (mod->Commit(op, MODELING_COMMIT_UPDATE))
                {
                    Modeling::Free(mod);
                    outPolyIdx = op->GetPolygonCount() - 1;
                    return true;
                }
            }
        }
        Modeling::Free(mod);
    }

    return false;
}

} // namespace cinema
