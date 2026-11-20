// Constrained Delaunay triangulation: insertVertices + insertEdges (+ erase*).
// Reaches vertex insertion (KD-tree locator), constraint-edge insertion, intersection resolving
// (TryResolve), pseudo-polygon triangulation and the erase/finalize paths. Input format:
// mayhem/harnesses/cdt_fuzz_common.h.
#include "cdt_fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    return cdtfuzz::runAny(
        data, size, [](auto& cdt, const auto& vv, const auto& ee, const cdtfuzz::Mode& m) {
            cdt.insertVertices(vv);
            cdt.insertEdges(ee);
            cdtfuzz::checkTopology(cdt);
            cdtfuzz::finalize(cdt, m.finalize);
        });
}
