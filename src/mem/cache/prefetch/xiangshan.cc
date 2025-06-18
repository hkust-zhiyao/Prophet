#include "mem/cache/prefetch/xiangshan.hh"

#include "debug/HWPrefetch.hh"
#include "params/XiangshanPrefetcher.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

Xiangshan::Xiangshan(const XiangshanPrefetcherParams &p)
    : Queued(p),
      scoreMax(p.score_max), roundMax(p.round_max),
      badScore(p.bad_score), rrEntries(p.rr_size),
      tagMask((1 << p.tag_bits) - 1),
      issuePrefetchRequests(false), bestOffset(1), phaseBestOffset(0),
      bestScore(0), round(0)
{
    if (!isPowerOf2(rrEntries)) {
        fatal("%s: number of RR entries is not power of 2, now is %d\n", name(), rrEntries);
    }
    if (!isPowerOf2(blkSize)) {
        fatal("%s: cache line size is not power of 2\n", name());
    }
    rrTable.resize(rrEntries);

    offsetsList.clear();
    const int offsetVal[] = { 1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16 };
    for(int n: offsetVal) {
        offsetsList.push_back(OffsetListEntry(n, 0));
    }
    offsetsListIterator = offsetsList.begin();
}

inline Addr
Xiangshan::tag(Addr addr) const
{
    return ((addr >> lBlkSize) >> floorLog2(rrEntries)) & tagMask;
}

inline Addr
Xiangshan::lineTag(Addr addr) const
{
    return (addr >> floorLog2(rrEntries)) & tagMask;
}

//addr is line address bits, rather than tag
unsigned int
Xiangshan::hash(Addr addr) const
{
    const int rrIndexBits = floorLog2(rrEntries);
    Addr hash1Mask = (1 << rrIndexBits) - 1 ;

    Addr hash1 = addr & hash1Mask;
    Addr hash2 = (addr >> rrIndexBits) & hash1Mask;
    return (hash1 ^ hash2) ;
}

void
Xiangshan::insertIntoRR(Addr addr)
{
    DPRINTF(HWPrefetch, "insertIntoRR: %#lx\n", addr);
    rrTable[hash(addr)] = lineTag(addr);
}

bool
Xiangshan::testRR(Addr addr) const
{
    Addr foundTag = rrTable[hash(addr)];

    if(lineTag(addr) == foundTag) {
        return true;
    }

    return false;
}

void
Xiangshan::bestOffsetLearning(Addr x)
{
    DPRINTF(HWPrefetch, "Current round is %d, bestScore is %d\n", round, bestScore);
    Addr offset_addr = (*offsetsListIterator).first;
    Addr lookup_addr = (x >> lBlkSize) - offset_addr;

    // There was a hit in the RR table, increment the score for this offset
    if (testRR(lookup_addr)) {
        DPRINTF(HWPrefetch, "Address %#lx found in the RR table\n", x);
        (*offsetsListIterator).second++;
        if ((*offsetsListIterator).second > bestScore) {
            bestScore = (*offsetsListIterator).second;
            phaseBestOffset = (*offsetsListIterator).first;
            DPRINTF(HWPrefetch, "New best score is %lu\n", bestScore);
        }
    }

    offsetsListIterator++;

    // All the offsets in the list were visited meaning that a learning
    // phase finished. Check if
    if (offsetsListIterator == offsetsList.end()) {
        offsetsListIterator = offsetsList.begin();
        round++;

        // Check if the best offset must be updated if:
        // (1) One of the scores equals SCORE_MAX
        // (2) The number of rounds equals ROUND_MAX
        if ((bestScore >= scoreMax) || (round == roundMax)) {
            if (bestScore <= badScore) {
                issuePrefetchRequests = false;
            }
            else {
                issuePrefetchRequests = true;
            }
            bestOffset = phaseBestOffset;
            round = 0;
            bestScore = 0;
            phaseBestOffset = 0;
            resetScores();
        }
    }
}

void
Xiangshan::resetScores()
{
    for (auto& it : offsetsList) {
        it.second = 0;
    }
}

void
Xiangshan::calculatePrefetch(const PrefetchInfo &pfi,
        std::vector<AddrPriority> &addresses)
{
    Addr addr = pfi.getAddr();

    // Go through the nth offset and update the score, the best score and the
    // current best offset if a better one is found
    bestOffsetLearning(addr);

    // This prefetcher is a degree 1 prefetch, so it will only generate one
    // prefetch at most per access
    if (issuePrefetchRequests) {
        Addr prefetch_addr = addr + (bestOffset << lBlkSize);
        if(samePage(prefetch_addr, addr)) {
            addresses.push_back(AddrPriority(prefetch_addr, 0));
            DPRINTF(HWPrefetch, "Current address is %#lx, Generated prefetch is %#lx, stride is %#lx\n", addr, prefetch_addr, bestOffset);
        }
        else {
            DPRINTF(HWPrefetch, "Stop prefetch, prefetch address and demand request are not in the same page\n");
        }
    }
}

void
Xiangshan::notifyFill(const PacketPtr& pkt)
{
    // Only insert into the RR right way if it's the pkt is a HWP
    if (!pkt->cmd.isHWPrefetch()) return;
    DPRINTF(HWPrefetch, "Prepare to do insertintoRR\n");
    
    if (issuePrefetchRequests) {
        Addr inserted = (pkt->getAddr() >> lBlkSize) - bestOffset;
        if(samePage(inserted << lBlkSize, pkt->getAddr())) {
            insertIntoRR(inserted);
        }
    }
}

}
}
