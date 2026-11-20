// Conforming Delaunay triangulation: insertVertices + conformToEdges (+ erase*), then the
// piece/split-vertex bookkeeping helpers. Input format: mayhem/harnesses/cdt_fuzz_common.h.
#include "cdt_fuzz_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    return cdtfuzz::runAny(
        data, size, [](auto& cdt, const auto& vv, const auto& ee, const cdtfuzz::Mode& m) {
            cdt.insertVertices(vv);
            cdt.conformToEdges(ee);
            cdtfuzz::checkTopology(cdt);
            const auto pieces = CDT::EdgeToPiecesMapping(cdt.pieceToOriginals);
            const auto splits = CDT::EdgeToSplitVertices(pieces, cdt.vertices);
            (void)splits;
            cdtfuzz::finalize(cdt, m.finalize);
        });
}
