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

#ifndef __MEM_CACHE_PREFETCH_IPCP_HH__
#define __MEM_CACHE_PREFETCH_IPCP_HH__

#include <string>
#include <unordered_map>
#include <vector>

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "mem/cache/tags/tagged_entry.hh"
#include "mem/cache/prefetch/associative_set.hh"
#include "mem/cache/prefetch/queued.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"
#include "mem/cache/tags/indexing_policies/set_associative.hh"
#include "mem/packet.hh"

namespace gem5
{

class BaseIndexingPolicy;
GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{
    class Base;
}
struct IPCPPrefetcherParams;

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

class IPCP : public Queued
{
  protected:
    const std::string benchmark;
    bool enableRPG;
    uint32_t allMisses;
    std::set<Addr> profiledAddrs;
    int distance;

    struct TrainEntry
    {
        TrainEntry() {
            issued = 0;
            solved = 0;
        }
        uint32_t issued;
        uint32_t solved;
    };

    std::map<Addr, TrainEntry> trainTable;

    std::map<Addr, uint32_t> missTable;

    bool use_sms;
    bool use_berti;

    const unsigned int region_size;
    const unsigned int region_blocks;
    Addr regionAddress(Addr a) { return a / region_size; };
    Addr regionOffset(Addr a) { return (a / blkSize) % region_blocks; }
    class ACTEntry : public TaggedEntry
    {
      public:
        Addr pc;
        bool is_secure;
        uint64_t region_bits;
        bool decr_mode;
        uint8_t access_cnt;
        uint64_t region_offset;
        ACTEntry(const SatCounter8 &conf)
            : TaggedEntry(),
              region_bits(0),
              decr_mode(false),
              access_cnt(0),
              region_offset(0)
        {
        }
    };

    AssociativeSet<ACTEntry> act;

    ACTEntry *actLookup(const PrefetchInfo &pfi);

    void updatePht(ACTEntry *act_entry);

    // pattern history table
    class PhtEntry : public TaggedEntry
    {
      public:
        std::vector<SatCounter8> hist;
        PhtEntry(const size_t sz, const SatCounter8 &conf)
            : TaggedEntry(), hist(sz, conf)
        {
        }
    };

    AssociativeSet<PhtEntry> pht;
    
    const int globalDegree;

    const int regionSize;

    /**  CS Component Table  */
    struct StrideEntry : public TaggedEntry
    {
        StrideEntry();

        void invalidate() override;

        Addr lastLineAddr;
        Addr lastPrefetchedLineAddr;
        int stride;
        int confidence;
    };
    AssociativeSet<StrideEntry>* pcTable;
    
    /* GS Component Table */
    struct IPGSEntry : public TaggedEntry
    {
        IPGSEntry();

        void invalidate() override;

        Addr lastTrainAddr;
        Addr lastPrefetchedLineAddr;
        bool streamValid;
        bool forward;
    };
    AssociativeSet<IPGSEntry>* ipgsTable;
    struct RSTEntry : public TaggedEntry
    {
        RSTEntry(int rSize, int pnCounts);

        const int regionSize;
        const int pnCountBits;
        void invalidate() override;

        Addr lastLineOffset;
        std::vector<bool> bitVector;
        SatCounter8 pnCount;
        bool dense;
        bool trained;
        bool tentative;
        int direction;
    };
    AssociativeSet<RSTEntry>* rstTable;

    /* CPLX Component Table */
    struct IPCPLXEntry : public TaggedEntry
    {
        IPCPLXEntry();
        void invalidate() override;

        Addr lastTrainAddr;
        Addr lastPrefetchedLineAddr;
        Addr lastSignature;
        Addr signature;
    };
    AssociativeSet<IPCPLXEntry>* ipcplxTable;
    struct CSPTEntry
    {
        int stride;
        int confidence;
    };
    CSPTEntry csptTable[128];

    /* Sandbox Table */
    struct SandboxEntry : public TaggedEntry
    {
        SandboxEntry();
        void invalidate() override;

        Addr PCGS; Addr PCCS; Addr PCCPLX;
        bool GS; bool CS; bool CPLX;
    };
    AssociativeSet<SandboxEntry>* sdbTable;

    struct SampleEntry
    {
        SampleEntry() {
          issueByGS = 0; confirmedGS = 0;
          issueByCS = 0; confirmedCS = 0;
          issueByCPLX = 0; confirmedCPLX = 0;
        }
        void reset() {
          issueByGS = 0; confirmedGS = 0;
          issueByCS = 0; confirmedCS = 0;
          issueByCPLX = 0; confirmedCPLX = 0;
        }
        int issueByGS;
        int confirmedGS;
        int issueByCS;
        int confirmedCS;
        int issueByCPLX;
        int confirmedCPLX;
    };
    std::unordered_map<Addr, SampleEntry*> sampleTable;
    std::vector<Addr> oldSignatures;

    bool sameBlock(Addr a, Addr b) {
        Addr mask = ~((1 << lBlkSize) - 1);
        return (a & mask) == (b & mask);
    }

    //input: full physical address 
    bool sameRegion(Addr a, Addr b) {
        return (a >> 11) == (b >> 11);
    }

    Addr lineOffset(Addr lineAddr) {
        return lineAddr & ((1 << (floorLog2(regionSize) - lBlkSize)) - 1);
    }

    bool updateStrideInfo(StrideEntry * sentry, Addr currentLine);

    bool insertSdbTable(Addr pf_addr, Addr pc, int type) {
        Addr line_addr = pf_addr >> lBlkSize;
        SandboxEntry * sdbentry = sdbTable->findEntry(line_addr, false);
        bool miss = false;
        bool exist = false;
        if (sdbentry == nullptr) {
            sdbentry = sdbTable->findVictim(line_addr);
            miss = true;
        }
        switch(type) {
          case 1: 
            if (sdbentry->CPLX && sdbentry->PCCPLX == pc)
                exist = true;
            sdbentry->CPLX = true;
            sdbentry->PCCPLX = pc;
            break;
          case 2:
            if (sdbentry->CS && sdbentry->PCCS == pc)
                exist = true;
            sdbentry->CS = true;
            sdbentry->PCCS = pc;
            break;
          case 3:
            if (sdbentry->GS && sdbentry->PCGS == pc)
                exist = true;
            sdbentry->GS = true;
            sdbentry->PCGS = pc;
            break;
          default:
            assert(false);
        }
        if (miss) {
            sdbTable->insertEntry(line_addr, false, sdbentry);
        }
        return exist;
    }

    struct HistoryInfo
    {
        Addr lineAddr;
        Cycles timestamp;
    };

    class HistoryTableEntry : public TaggedEntry
    {
      public:
        /** FIFO of demand miss history. */
        std::vector<HistoryInfo> history;

        HistoryTableEntry() { history = std::vector<HistoryInfo>(); }
    };

    AssociativeSet<HistoryTableEntry> historyTable;

    enum DeltaStatus { L1_PREF, L2_PREF, NO_PREF };

    struct DeltaInfo
    {
        uint8_t coverageCounter;
        int64_t delta;
        DeltaStatus status;
    };

    class TableOfDeltasEntry : public TaggedEntry
    {
      public:
        std::vector<DeltaInfo> deltas;
        uint8_t counter;
        int64_t best_delta;

        void resetConfidence(bool reset_status)
        {
            counter = 0;
            for (auto &info : deltas) {
                info.coverageCounter = 0;
                if (reset_status) {
                    info.status = NO_PREF;
                }
            }
            if (reset_status) {
                best_delta = 0;
            }
        }

        void updateStatus()
        {
            for (auto &info : deltas) {
                info.status = info.coverageCounter >=
                                      counter * 0.35 ? L2_PREF : NO_PREF;
                info.coverageCounter = 0;
            }
            counter = 0;
        }

        TableOfDeltasEntry()
        {
            deltas = std::vector<DeltaInfo>(16);
            resetConfidence(true);
            best_delta = 0;
        }
    };

    AssociativeSet<TableOfDeltasEntry> tableOfDeltas;

    Cycles lastFillLatency;

    struct prefetch_fill_latency
    {
        Addr addr;
        bool is_secure;
        Cycles latency;
    };
    std::list<prefetch_fill_latency> prefetch_latency_container;

    /** Update history table on demand miss. */
    void updateHistoryTable(const PrefetchInfo &pfi);

    /** Update table of deltas on cache fill. */
    void updateTableOfDeltas(const Addr pc, const bool isSecure,
                             const std::vector<int64_t> &new_deltas);

    /** Search for timely deltas. */
    void searchTimelyDeltas(const HistoryTableEntry &entry,
                            const Cycles &latency,
                            const Cycles &demand_cycle,
                            const Addr &blk_addr,
                            std::vector<int64_t> &deltas);

    void notifyFill(const PacketPtr &pkt) override;

    void notifyFill4Miss(const PacketPtr &pkt) override;

    void cleanPrefetchLatency()
    {
        std::list<prefetch_fill_latency>::iterator it;
        for (it = prefetch_latency_container.begin(); it != prefetch_latency_container.end();) 
        {
            if(!inCache(it->addr,it->is_secure))
            {
                it = prefetch_latency_container.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

    void notifyCS(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool aggresive);
    void notifyGS(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool aggresive);
    void notifyCPLX(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool aggresive);
    void notifySMS(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool aggresive);
    void notifyBerti(const PrefetchInfo &pfi, std::vector<AddrPriority> &addresses, bool aggresive);

    void copyVector(std::vector<AddrPriority> &a, std::vector<AddrPriority> &b, Addr pc) {
        for (auto addr: a) {
            bool exist = false;
            for (auto addr2: b) {
                if (addr.first == addr2.first) {
                    exist = true;
                    break;
                }
            }
            if (!exist) {
                b.push_back(addr);
                trainTable[pc].issued += 1;
            }
        }
    }

    std::string out_file;

    struct IPCPStats : public statistics::Group
    {
        IPCPStats(statistics::Group *parent);
        statistics::Scalar GS_issue;
        statistics::Scalar CS_issue;
        statistics::Scalar CPLX_issue;
        statistics::Scalar table_miss;
    } statsIPCP;

    void interpretHintFile(const std::string &hintContent);
    
    void notifyFillPrefetchData(const PacketPtr& pkt) override;

    struct DepSucEntry
    {
        DepSucEntry() {
            this->suc_inst = "";
            this->suc_pc = 0;
        }
        DepSucEntry(const std::string suc_inst, Addr suc_pc) {
            this->suc_inst = suc_inst;
            this->suc_pc = suc_pc;
        }
        std::string suc_inst;
        Addr suc_pc;
    };

    void processInstructionChain(const PacketPtr& pkt,
        const DepSucEntry& depPair,
        uint64_t baseAddr,
        Addr currentPC);
    
    void issuePrefetch(PacketPtr pkt, const std::vector<AddrPriority>& addresses, Addr nextPC);
    
    std::map<Addr, DepSucEntry> dependenceGraph;

  public:
    IPCP(const IPCPPrefetcherParams &p);

    void calculatePrefetch(const PrefetchInfo &pfi,
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
            double ratio = (double) miss/allMisses;
            if (ratio < 0.0001) {
                continue;
            }
            if (ratio >= 0.01) {
                f << "************";
            }
            f << std::hex << pc << std::dec;
            // accuracy
            f << ","<< it->second.solved << "," << it->second.issued << "," << miss << "," << ratio*100 << std::endl;
            
        }
        f.close();
    }
    
    virtual void
    pfUntimely(PacketPtr pkt)
    {
        Base::pfUntimely(pkt);
        Addr pc = pkt->req->hasPC() ? pkt->req->getPC() : 0;
        if (trainTable.find(pc) == trainTable.end()) {
            trainTable[pc] = TrainEntry();
        } else {
            trainTable[pc].solved += 1;
        }
    }

    virtual void
    pfTimely(PacketPtr pkt)
    {
        Base::pfTimely(pkt);
        Addr pc = pkt->req->hasPC() ? pkt->req->getPC() : 0;
        if (trainTable.find(pc) == trainTable.end()) {
            trainTable[pc] = TrainEntry();
        } else {
            trainTable[pc].solved += 1;
        }
    }
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_STRIDE_HH__
