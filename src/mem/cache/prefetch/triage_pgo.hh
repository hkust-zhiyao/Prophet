/*
 * Copyright (c) 2018 Inria
 * Copyright (c) 2012-2013, 2015 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * Describes a strided prefetcher.
 */

#ifndef __MEM_CACHE_PREFETCH_TRIAGEPGO_HH__
#define __MEM_CACHE_PREFETCH_TRIAGEPGO_HH__

#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "mem/cache/prefetch/associative_set.hh"
#include "mem/cache/prefetch/queued.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"
#include "mem/cache/tags/indexing_policies/set_associative.hh"
#include "mem/packet.hh"
#include "mem/cache/tags/tagged_entry.hh"
#include "mem/cache/tags/base.hh"
#include "params/TriagePGOHashedSetAssociative.hh"

namespace gem5
{

class BaseIndexingPolicy;
GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{
    class Base;
}
struct TriagePGOPrefetcherParams;

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

class TriagePGOHashedSetAssociative : public SetAssociative
{
  protected:
    // uint32_t extractSet(const Addr addr) const override;

  public:
    TriagePGOHashedSetAssociative(
        const TriagePGOHashedSetAssociativeParams &p)
      : SetAssociative(p)
    {
        uint32_t numEntries = numSets * assoc;
        uint32_t roundedBytes = numEntries * 4;
        uint32_t actualEntries = roundedBytes / 64 * 12;
        std::cout<< actualEntries << std::endl;
    }
    ~TriagePGOHashedSetAssociative() = default;
};

class TriagePGO : public Queued
{
  protected:
    bool inTraining;
    bool disablePF;
    BaseTags* cachetags;
    const std::string benchmark;
    bool enableInsertFilter;
    bool enablePGLRU;
    std::map<Addr, int> profileReplTable;
    std::map<Addr, uint32_t> profileUtiTable;
    std::set<Addr> profileInsertTable;
    const bool enableDRA;
    bool enableMRB;
    int globalDegree;
    uint32_t numEntriesinTable;
    long long global_timestamp;
    /**
     * Information used to create a new PC table. All of them behave equally.
     */
    struct SingleMetaEntry
    {
        Addr correlatedAddr;
        SatCounter8 counter;

        SingleMetaEntry(unsigned bits) : correlatedAddr(0), counter(bits) {}
    };

    struct MetaEntry : public TaggedEntry
    {
        /** group of stides */
        std::vector<SingleMetaEntry> entries;
        bool used;
        Addr pc;
        long long touch_time;
        MetaEntry(size_t num_strides, unsigned counter_bits)
          : TaggedEntry(), entries(num_strides, counter_bits), used(false), pc(0), touch_time(0)
        {
        }

        /** Reset the entries to their initial values */
        void
        invalidate() override
        {
            TaggedEntry::invalidate();
            used = false;
            pc = 0;
            touch_time = 0;
            for (auto &entry : entries) {
                entry.correlatedAddr = 0;
                entry.counter.reset();
            }
        }
    };

    struct TrainEntry
    {
        TrainEntry() {
            issued = 0;
            solved = 0;
            meta_inserted = 0;
            meta_used = 0;
            protect = true;
        }
        uint32_t issued;
        uint32_t solved;
        uint32_t meta_inserted;
        uint32_t meta_used;
        bool protect;
    };

    std::map<Addr, TrainEntry> trainTable;

    std::map<Addr, uint32_t> missTable;

    AssociativeSet<MetaEntry>* metaTable;

    AssociativeSet<MetaEntry>* mrbTable;
    
    std::unordered_map<Addr, Addr> pcTable;

    std::set<Addr> metaUsedPool;

    std::set<Addr> metaInsertedPool;

    uint32_t allMisses;

    void addToUsedPool(Addr a) {
        if (metaUsedPool.find(a) == metaUsedPool.end()) {
            metaUsedPool.insert(a);
            // statsTriagePGO.meta_used++;
        }
    }

    void addToInsertedPool(Addr a) {
        if (metaInsertedPool.find(a) == metaInsertedPool.end()) {
            metaInsertedPool.insert(a);
            // statsTriagePGO.meta_inserted++;
        }
    }

    bool isAlreadyInQueue(std::vector<AddrPriority> &addresses, Addr addr) {
        for(AddrPriority& a : addresses) {
            if (a.first == addr)
                return true;
        }
        return false;
    }

    int issue(AssociativeSet<MetaEntry>* metaTable, Addr lookup, Addr pc, std::vector<AddrPriority> &addresses, std::vector<AddrPriority> &next_level_addrs);


    std::string out_file;

    struct TriagePGOStats : public statistics::Group
    {
        TriagePGOStats(statistics::Group *parent);
        statistics::Scalar metada_rp;
        statistics::Scalar reuse_rp;
        statistics::Scalar recv_trains;
        statistics::Scalar meta_inserted;
        statistics::Scalar meta_used;
        statistics::Scalar metaTableUsedEntries;
        statistics::Scalar meta_hit;
        statistics::Scalar meta_access;
        statistics::Scalar issued_by_reuse;
        statistics::Scalar issued_by_metatable;
    } statsTriagePGO;

  public:
    TriagePGO(const TriagePGOPrefetcherParams &p);

    void calculatePrefetch(const PacketPtr &pkt, const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses) override;
    void outPrefetcherPGOInfo() override {
        std::ofstream f;
        f.open(out_file, std::ios::trunc);
        f << allMisses << std::endl;
        std::cout << "allMisses:" << allMisses << std::endl;
        for (std::map<Addr, TrainEntry>::iterator it=trainTable.begin(); it!=trainTable.end(); ++it) {
            // PC, solved, issued, misses
            Addr pc = it->first;
            // assert(missTable.find(pc) != missTable.end());
            uint32_t miss = 0;
            if (missTable.find(pc) != missTable.end()) {
                miss = missTable[pc];
            }
            f << std::hex << pc << std::dec;
            // accuracy
            f << ","<< it->second.solved << "," << it->second.issued << "," << miss;
            // utilization
            // f << ","<< it->second.meta_used << "," << it->second.meta_inserted;
            f << std::endl;
            std::cout << std::hex << pc << std::dec;
            std::cout << "," << it->second.solved << "," << it->second.issued << "," << miss << std::endl;
        }

        // metadata entries utilization
        std::vector<std::pair<Addr, int> > pairs;
        for (auto& it : profileUtiTable) {
            pairs.push_back(it);
        }
        std::sort(pairs.begin(), pairs.end(), [](auto& a, auto& b) {
            return a.second > b.second;
        });
        std::cout << "Metadata Entries Utilization:" << std::endl;
        for (auto& pair : pairs) {
            if (pair.second > 0)
                std::cout << pair.first << ": " << pair.second << std::endl;
        }
        f.close();
    }
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_TriagePGO_HH__
