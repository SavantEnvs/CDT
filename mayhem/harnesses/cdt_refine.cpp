// Delaunay refinement: insertVertices + insertEdges, then refineTriangles (optionally skipping the
// triangles a collect* call marked for erasure), findUnrefinedTriangles, findEncroachedFixedEdges
// and finalization. Input format: mayhem/harnesses/cdt_fuzz_common.h; mode bit 6 selects the
// criterion (SmallestAngle / LargestArea), bit 7 the order (FIFO / WorstFirst), byte 1 the threshold.
#include "cdt_fuzz_common.h"

#include <algorithm>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    return cdtfuzz::runAny(
        data, size, [](auto& cdt, const auto& vv, const auto& ee, const cdtfuzz::Mode& m) {
            typedef typename std::decay<decltype(vv[0].x)>::type T;
            cdt.insertVertices(vv);
            cdt.insertEdges(ee);
            cdtfuzz::checkTopology(cdt);

            const CDT::RefinementCriterion::Enum crit =
                (m.extra & 1) ? CDT::RefinementCriterion::LargestArea
                              : CDT::RefinementCriterion::SmallestAngle;
            const CDT::RefinementOrder::Enum order =
                (m.extra & 2) ? CDT::RefinementOrder::WorstFirst
                              : CDT::RefinementOrder::FIFO;
            T threshold;
            if(crit == CDT::RefinementCriterion::SmallestAngle)
            {
                // 5..35 degrees (beyond ~33 degrees Ruppert refinement need not terminate; the
                // vertex budget below still bounds it)
                threshold = CDT::degToRad(T(5 + (m.param % 31)));
            }
            else
            {
                // a fraction of the input's bounding-box area
                CDT::Box2d<T> box;
                box.envelopPoints(vv);
                const T area = std::max(
                    (box.max.x - box.min.x) * (box.max.y - box.min.y), T(1e-12));
                threshold = area / T(1 + 4 * int(m.param));
            }
            const CDT::VertInd budget(500);

            if(m.finalize >= 2)
            {
                CDT::TriIndUSet toErase = (m.finalize == 2)
                                              ? cdt.collectOuterTriangles()
                                              : cdt.collectOuterTrianglesAndHoles();
                cdt.refineTriangles(budget, crit, threshold, &toErase, T(1e-6), order);
                cdtfuzz::checkTopology(cdt);
                (void)cdt.findUnrefinedTriangles(crit, threshold);
                (void)cdt.findEncroachedFixedEdges();
                cdt.finalizeTriangulation(toErase);
                cdtfuzz::checkTopology(cdt);
            }
            else
            {
                cdt.refineTriangles(budget, crit, threshold, NULL, T(1e-6), order);
                cdtfuzz::checkTopology(cdt);
                (void)cdt.findUnrefinedTriangles(crit, threshold);
                (void)cdt.findEncroachedFixedEdges();
                cdtfuzz::finalize(cdt, m.finalize);
            }
        });
}
