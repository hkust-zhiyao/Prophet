#ifndef __MEM_CACHE_PREFETCH_ASSIST_HH__
#define __MEM_CACHE_PREFETCH_ASSIST_HH__

#include <map>
#include <vector>

#include "mem/cache/prefetch/queued.hh"
#include "mem/packet.hh"

namespace gem5
{

struct AssistPrefetcherParams;

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

class Assist : public Queued
{
    public:
        Assist(const AssistPrefetcherParams &p);
        ~Assist() = default;

        void calculatePrefetch(const PrefetchInfo &pfi,
                std::vector<AddrPriority> &addresses) override;

        void outPrefetcherPGOInfo() override {
            std::ofstream f;
            f.open(out_file, std::ios::trunc);
            // f << allMisses << std::endl;
            std::cout << "allMisses:" << allMisses << std::endl;
            for (std::map<Addr, uint64_t>::iterator it=missMap.begin(); it!=missMap.end(); ++it) {
                // PC, solved, issued, misses
                Addr pc = it->first;
                // assert(missTable.find(pc) != missTable.end());
                uint64_t miss = it->second;
                double ratio = (double) miss/allMisses;
                if (ratio > 0.05) {
                    f << std::hex << pc << std::dec << std::endl;
                }
                std::cout << std::hex << pc << std::dec;
                std::cout << "," << miss << std::endl;
            }
            f.close();
        }

    private:
        uint64_t cnt;
        uint64_t allMisses;
        std::string out_file;
        std::map<Addr, uint64_t> missMap;
};

} // namespace prefetch
} // namespace gem5

#endif /* __MEM_CACHE_PREFETCH_BOP_HH__ */