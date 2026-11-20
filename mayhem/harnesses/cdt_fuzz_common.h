// Shared input decoding for the CDT fuzz harnesses.
//
// Input layout (same for every CDT target, so seeds are interchangeable):
//   byte 0  mode bits
//             bit 0     VertexInsertionOrder: 0 = Auto (KDTree BFS), 1 = AsProvided
//             bit 1     IntersectingConstraintEdges: 0 = NotAllowed, 1 = TryResolve
//             bit 2     minDistToConstraintEdge: 0 = 0.0, 1 = 1e-6
//             bits 3-4  finalization: 0 none, 1 eraseSuperTriangle,
//                       2 eraseOuterTriangles, 3 eraseOuterTrianglesAndHoles
//             bit 5     coordinate type: 0 = double, 1 = float
//             bits 6-7  harness-specific (refinement criterion / order)
//   byte 1  harness-specific parameter (refinement threshold)
//   rest    CDT's own text input format, exactly as in CDT/tests/inputs/*.txt
//           and read by readInputFromFile() in CDT/tests/cdt.test.cpp:
//             nVerts nEdges
//             x y        (nVerts lines)
//             v1 v2      (nEdges lines)
//
// IntersectingConstraintEdges::DontCheck is deliberately not reachable: it is a caller promise that
// no constraint edges intersect (Triangulation.h), and breaking it trips the library's own asserts.
#pragma once

#include <CDT.h>
#include <VerifyTopology.h>

#include <cmath>
#include <limits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace cdtfuzz
{

// Keep each exec fast: the KD-tree/triangulation cost grows super-linearly.
constexpr std::size_t kMaxVerts = 2000;
constexpr std::size_t kMaxEdges = 2000;

struct Mode
{
    CDT::VertexInsertionOrder::Enum order;
    CDT::IntersectingConstraintEdges::Enum strategy;
    double minDist;
    unsigned finalize; // 0..3
    bool useFloat;
    unsigned extra;    // bits 6-7
    uint8_t param;     // byte 1
};

inline Mode decodeMode(uint8_t b0, uint8_t b1)
{
    Mode m;
    m.order = (b0 & 1) ? CDT::VertexInsertionOrder::AsProvided
                       : CDT::VertexInsertionOrder::Auto;
    m.strategy = (b0 & 2) ? CDT::IntersectingConstraintEdges::TryResolve
                          : CDT::IntersectingConstraintEdges::NotAllowed;
    m.minDist = (b0 & 4) ? 1e-6 : 0.0;
    m.finalize = (b0 >> 3) & 3;
    m.useFloat = (b0 >> 5) & 1;
    m.extra = (b0 >> 6) & 3;
    m.param = b1;
    return m;
}

// Parse the text format. Returns false for input that is not a well-formed geometry description:
// - malformed numbers / truncated lists;
// - non-finite coordinates (NaN/Inf are not geometry);
// - coordinates whose super-triangle would overflow to Inf. Triangulation::insertVertices calls
//   addSuperTriangle(box) (Triangulation.hpp), which for |coord| <= M computes
//     w, h = max - min                <= 2M
//     r    = 2 * max(w, h)            <= 4M   (the "while(center.y <= center.y - r)" doubling
//                                              only runs while r < ulp(center), so stays tiny)
//     R    = 2 * r                    <= 8M
//     posV3.y = center.y + R          <= 9M,  posV1.x = center.x - R*cos30 >= -7.93M
//   so every super-triangle coordinate is finite iff 9M <= numeric_limits<T>::max(). We use
//   M = numeric_limits<T>::max() / 16 (a power of two, so the bound itself is exact). For the
//   float mode this also keeps the double->float conversion defined ([conv.double]: converting an
//   out-of-range value is UB). Everything below that bound, including magnitudes where the
//   predicates' own products may overflow, is handed to the library;
// - edges whose vertex index is out of range or degenerate (v1 == v2): Edge indices must refer to
//   inserted vertices (insertEdges/conformToEdges contract).
// Duplicate edges are deliberately kept (upstream fix 2068d01 is about a duplicated constraint edge).
template <typename T>
bool parse(
    const uint8_t* data,
    size_t size,
    std::vector<CDT::V2d<T> >& vv,
    std::vector<CDT::Edge>& ee)
{
    std::istringstream in(
        std::string(reinterpret_cast<const char*>(data), size));
    unsigned long long nV = 0, nE = 0;
    if(!(in >> nV >> nE))
        return false;
    if(nV == 0 || nV > kMaxVerts || nE > kMaxEdges)
        return false;
    vv.reserve(nV);
    for(unsigned long long i = 0; i < nV; ++i)
    {
        double x, y;
        if(!(in >> x >> y))
            return false;
        const double kMaxCoord =
            static_cast<double>(std::numeric_limits<T>::max()) / 16;
        if(!std::isfinite(x) || !std::isfinite(y) ||
           std::fabs(x) > kMaxCoord || std::fabs(y) > kMaxCoord)
            return false;
        vv.push_back(CDT::V2d<T>(static_cast<T>(x), static_cast<T>(y)));
    }
    ee.reserve(nE);
    for(unsigned long long i = 0; i < nE; ++i)
    {
        unsigned long long a, b;
        if(!(in >> a >> b))
            return false;
        if(a >= nV || b >= nV || a == b)
            return false;
        ee.push_back(CDT::Edge(CDT::VertInd(a), CDT::VertInd(b)));
    }
    return true;
}

// A topologically broken triangulation is a library bug (upstream's own tests REQUIRE this).
template <typename T>
void checkTopology(const CDT::Triangulation<T>& cdt)
{
    if(!CDT::verifyTopology(cdt))
        abort();
}

template <typename T>
void finalize(CDT::Triangulation<T>& cdt, unsigned how)
{
    switch(how)
    {
    case 1:
        cdt.eraseSuperTriangle();
        break;
    case 2:
        cdt.eraseOuterTriangles();
        break;
    case 3:
        cdt.eraseOuterTrianglesAndHoles();
        break;
    default:
        return;
    }
    checkTopology(cdt);
}

// Decode + dedupe, then call body(cdt, vv, ee, mode). CDT::Error (DuplicateVertexError,
// IntersectingConstraintsError, InvalidEdgeSplitVertex, ...) is the library's documented way of
// rejecting input, so it is caught; anything else propagates as a crash.
template <typename T, typename Body>
int run(const uint8_t* data, size_t size, Body body)
{
    if(size < 2)
        return -1;
    const Mode m = decodeMode(data[0], data[1]);
    std::vector<CDT::V2d<T> > vv;
    std::vector<CDT::Edge> ee;
    if(!parse<T>(data + 2, size - 2, vv, ee))
        return -1;
    CDT::RemoveDuplicatesAndRemapEdges(vv, ee);
    // remapping can collapse an edge onto a single vertex
    std::vector<CDT::Edge> kept;
    kept.reserve(ee.size());
    for(const CDT::Edge& e : ee)
        if(e.v1() != e.v2())
            kept.push_back(e);
    try
    {
        CDT::Triangulation<T> cdt(m.order, m.strategy, static_cast<T>(m.minDist));
        body(cdt, vv, kept, m);
    }
    catch(const CDT::Error&)
    {
    }
    return 0;
}

template <typename Body>
int runAny(const uint8_t* data, size_t size, Body body)
{
    if(size >= 1 && ((data[0] >> 5) & 1))
        return run<float>(data, size, body);
    return run<double>(data, size, body);
}

} // namespace cdtfuzz
