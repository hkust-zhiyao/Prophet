#ifndef __MEM_CACHE_PREFETCH_XIANGSHAN_HH__
#define __MEM_CACHE_PREFETCH_XIANGSHAN_HH__

#include "mem/cache/prefetch/queued.hh"
#include "mem/packet.hh"

namespace gem5
{

struct XiangshanPrefetcherParams;

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

class Xiangshan : public Queued
{
    private:

        /** Learning phase parameters */
        const unsigned int scoreMax;
        const unsigned int roundMax;
        const unsigned int badScore;
        /** Recent requests table parameteres */
        const unsigned int rrEntries;
        const unsigned int tagMask;

        std::vector<Addr> rrTable;

        /** Structure to save the offset and the score */
        typedef std::pair<int16_t, uint8_t> OffsetListEntry;
        std::vector<OffsetListEntry> offsetsList;

        /** Hardware prefetcher enabled */
        bool issuePrefetchRequests;
        /** Current best offset to issue prefetches */
        Addr bestOffset;
        /** Current best offset found in the learning phase */
        Addr phaseBestOffset;
        /** Current test offset index */
        std::vector<OffsetListEntry>::iterator offsetsListIterator;
        /** Max score found so far */
        unsigned int bestScore;
        /** Current round */
        unsigned int round;

        void resetScores();
        Addr tag(Addr addr) const;
        Addr lineTag(Addr addr) const;
        unsigned int hash(Addr addr) const;
        void insertIntoRR(Addr addr);
        bool testRR(Addr) const;
        void bestOffsetLearning(Addr);
        void notifyFill(const PacketPtr& pkt) override;
    
    public:
        Xiangshan(const XiangshanPrefetcherParams &p);
        ~Xiangshan() = default;
        void calculatePrefetch(const PrefetchInfo &pfi,
                std::vector<AddrPriority> &addresses) override;
};

} // namespace prefetch
} // namespace gem5

#endif /* __MEM_CACHE_PREFETCH_BOP_HH__ */