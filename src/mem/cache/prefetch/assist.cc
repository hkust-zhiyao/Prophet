#include "mem/cache/prefetch/assist.hh"

#include "base/random.hh"
#include "debug/HWPrefetch.hh"
#include "params/AssistPrefetcher.hh"
#include "debug/PfObservedTrace.hh"

/* calculatePrefetch->findStream->(allocateStream)->(deallocateStream) */

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

Assist::Assist(const AssistPrefetcherParams &p)
    : Queued(p)
{
    cnt = 0;
    allMisses = 0;
    out_file = p.output_file + "/train.txt";
}

void
Assist::calculatePrefetch(const PrefetchInfo &pfi,
                                    std::vector<AddrPriority> &addresses)
{
    if (!pfi.hasPC()) {
        return;
    }
    cnt++;

    // Get required packet info
    Addr pf_addr = pfi.getAddr();
    Addr line_addr = pf_addr >> lBlkSize;
    Addr pc = pfi.getPC();
    bool miss = pfi.isCacheMiss();
    if (miss) {
        allMisses++;
        if (missMap.find(pc) != missMap.end()) {
            missMap[pc]++;
        }
        else {
            missMap[pc] = 1;
        }
    }
    DPRINTFR(PfObservedTrace, "%d,%x,%x,%d\n", cnt, pc, line_addr, miss);
}
}
}