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
 * Stride Prefetcher template instantiations.
 */

#include "mem/cache/prefetch/ipcp.hh"

#include <cassert>

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "debug/HWPrefetch.hh"
#include "debug/IPCPPrefetcher.hh"
#include "debug/SMSPrefetcher.hh"
#include "mem/cache/prefetch/associative_set_impl.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "params/IPCPPrefetcher.hh"

// Difference between IPCP and IPCPCov: Training loads to all pfts, issue by priority
namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

IPCP::IPCPStats::IPCPStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(GS_issue, statistics::units::Count::get(), ""),
      ADD_STAT(CS_issue, statistics::units::Count::get(), ""),
      ADD_STAT(CPLX_issue, statistics::units::Count::get(), ""),
      ADD_STAT(table_miss, statistics::units::Count::get(), "")
{

}

IPCP::StrideEntry::StrideEntry()
  : TaggedEntry()
{
    invalidate();
}
void
IPCP::StrideEntry::invalidate()
{
    TaggedEntry::invalidate();
    lastLineAddr = 0;
    lastPrefetchedLineAddr = 0;
    stride = 0;
    confidence = 0;
}

IPCP::IPGSEntry::IPGSEntry()
  : TaggedEntry()
{
    invalidate();
}
void
IPCP::IPGSEntry::invalidate()
{
    TaggedEntry::invalidate();
    lastTrainAddr = 0;
    lastPrefetchedLineAddr = 0;
    streamValid = false;
    forward = false;
}

IPCP::RSTEntry::RSTEntry(int rSize, int pnCounts)
  : TaggedEntry(), regionSize(rSize), pnCountBits(pnCounts), pnCount(pnCountBits, (1 << (pnCountBits - 1)))
{
    invalidate();
}
void
IPCP::RSTEntry::invalidate()
{
    TaggedEntry::invalidate();
    lastLineOffset = 0;
    bitVector.resize(regionSize / 64, false);
    pnCount.reset();
    dense = false;
    trained = false;
    tentative = false;
    direction = 0;
}

IPCP::IPCPLXEntry::IPCPLXEntry()
  : TaggedEntry()
{
    invalidate();
}
void
IPCP::IPCPLXEntry::invalidate()
{
    TaggedEntry::invalidate();
    lastTrainAddr = 0;
    lastPrefetchedLineAddr = 0;
    lastSignature = 0;
    signature = 0;
}

IPCP::SandboxEntry::SandboxEntry()
  : TaggedEntry()
{
    invalidate();
}

void
IPCP::SandboxEntry::invalidate()
{
    TaggedEntry::invalidate();
    PCGS = 0;
    PCCS = 0;
    PCCPLX = 0;
    GS = false;
    CS = false;
    CPLX = false;
}

IPCP::IPCP(const IPCPPrefetcherParams &p)
  : Queued(p),
    benchmark(p.pgo_benchmark),
    enableRPG(p.enable_rpg),
    allMisses(0),
    use_sms(p.use_sms),
    use_berti(p.use_berti),
    region_size(p.region_size),
    region_blocks(p.region_size / p.block_size),
    act(p.act_entries, p.act_entries, p.act_indexing_policy,
          p.act_replacement_policy, ACTEntry(SatCounter8(2, 1))),
    pht(p.pht_assoc, p.pht_entries, p.pht_indexing_policy,
          p.pht_replacement_policy,
          PhtEntry(2 * (region_blocks - 1), SatCounter8(2, 0))),
    globalDegree(p.global_degree),
    regionSize(p.rst_region_size),
    historyTable(p.history_table_assoc, p.history_table_entries,
                   p.history_table_indexing_policy,
                   p.history_table_replacement_policy, HistoryTableEntry()),
    tableOfDeltas(p.table_of_deltas_entries, p.table_of_deltas_entries,
                    p.table_of_deltas_indexing_policy,
                    p.table_of_deltas_replacement_policy,
                    TableOfDeltasEntry()),
    statsIPCP(this)
{
    pcTable = new AssociativeSet<StrideEntry>(p.pc_assoc, p.pc_entries,
        p.pc_indexing_policy, p.pc_replacement_policy,
        StrideEntry());
    ipgsTable = new AssociativeSet<IPGSEntry>(p.ipgs_assoc, p.pc_entries,
        p.ipgs_indexing_policy, p.ipgs_replacement_policy,
        IPGSEntry());
    rstTable = new AssociativeSet<RSTEntry>(p.rst_assoc, p.rst_entries,
        p.rst_indexing_policy, p.rst_replacement_policy,
        RSTEntry(p.rst_region_size, p.pn_count_bits));
    ipcplxTable = new AssociativeSet<IPCPLXEntry>(p.ipcplx_assoc, p.pc_entries,
        p.ipcplx_indexing_policy, p.ipcplx_replacement_policy,
        IPCPLXEntry());
    sdbTable = new AssociativeSet<SandboxEntry>(p.sdb_assoc, p.sdb_entries,
        p.sdb_indexing_policy, p.sdb_replacement_policy,
        SandboxEntry());

    for (int i = 0; i < 128; i++) {
        csptTable[i].stride = 0;
        csptTable[i].confidence = 0;
    }

    // RPG logic
    if (enableRPG) {
        std::string dep_hint = "/home/mliet/gem5/pgo/identify_re/" + benchmark + "/hint.txt";
        std::string pc_file = "/home/mliet/gem5/pgo/identify_re/" + benchmark + "/result.txt";
        std::string distance_file = "/home/mliet/gem5/pgo/identify_re/" + benchmark + "/pgo.txt";

        interpretHintFile(dep_hint);

        std::ifstream pc_in(pc_file);
        std::ifstream dis_in(distance_file);
        if (!pc_in || !dis_in) {
            std::cerr << "Unable to open file" << std::endl;
            std::cerr << pc_file << std::endl;
            std::cerr << distance_file << std::endl;
            assert(false);
        }
        
        std::string line;
        while (std::getline(pc_in, line)) {
            // PC and degree are now split into two variables
            profiledAddrs.insert(std::stoull (line, nullptr ,16));
        }
        pc_in.close();

        std::getline(dis_in, line);
        std::istringstream lineStream(line);
        std::string dis;
        std::string ipc;

        if (std::getline(lineStream, dis, ',') && std::getline(lineStream, ipc)) {
            // PC and degree are now split into two variables
            distance = std::stoi (dis, nullptr ,10);
        } else {
            std::cerr << "Error: Incorrectly formatted line." << std::endl;
            assert(false);
        }
        dis_in.close();
        printf("enableRPG! distance is %s, ipc is %s\n", dis.c_str(), ipc.c_str());
    } else {
        distance = 0;
    }
    out_file = p.output_file + "/train.txt";

    // end RPG logic
}

// convTable and sampleTable may need to be represented by AssociativeSet!!!
void
IPCP::calculatePrefetch(const PrefetchInfo &pfi,
                                    std::vector<AddrPriority> &addresses)
{
    if (!pfi.hasPC()) {
        DPRINTF(HWPrefetch, "Ignoring request with no PC.\n");
        return;
    }
    // Should using sandbox table update sample table first
    Addr train_addr = pfi.getAddr() & ~((1 << 6) - 1);
    Addr pc = pfi.getPC();
    bool is_secure = pfi.isSecure();
    bool miss = pfi.isCacheMiss();


    if (miss) {
        allMisses += 1;
        if (missTable.find(pc) == missTable.end())
            missTable[pc] = 1;
        else
            missTable[pc] += 1;
    }

    if (profiledAddrs.find(pc) != profiledAddrs.end()) {
        Addr new_addr = pfi.getAddr() + distance;
        addresses.push_back(AddrPriority(new_addr, 0));
    }

    if (sampleTable.find(pc) == sampleTable.end()) {
        sampleTable[pc] = new SampleEntry();
    }
    SampleEntry * sample = sampleTable[pc];
    SandboxEntry * sdbentry = sdbTable->findEntry(train_addr >> lBlkSize, is_secure);
    DPRINTF(IPCPPrefetcher, "Using Train Addr to search for %lx, pc is %lx.\n", train_addr, pc);
    if (sdbentry) {
        DPRINTF(IPCPPrefetcher, "Sandbox hit, GS: %d, PCGS: %lx, CS: %d, PCCS: %lx, CPLX: %d, PCCPLX: %lx.\n",
                    sdbentry->GS, sdbentry->PCGS, sdbentry->CS, sdbentry->PCCS, sdbentry->CPLX, sdbentry->PCCPLX);
        if (sdbentry->GS && sdbentry->PCGS == pc) {
            sample->confirmedGS++;
            sdbentry->GS = false;
        }
        if (sdbentry->CS && sdbentry->PCCS == pc) {
            sample->confirmedCS++;
            sdbentry->CS = false;
        }
        if (sdbentry->CPLX && sdbentry->PCCPLX == pc) {
            sample->confirmedCPLX++;
            sdbentry->CPLX = false;
        }
    }

    std::vector<AddrPriority> candicatesGS;
    std::vector<AddrPriority> candicatesCS;
    std::vector<AddrPriority> candicatesCPLX;

    DPRINTF(IPCPPrefetcher, "Notify All.\n");
    notifyGS(pfi, candicatesGS, false);
    if (use_berti)
        notifyBerti(pfi, candicatesCS, false);
    else
        notifyCS(pfi, candicatesCS, false);
    if (!use_sms)
        notifyCPLX(pfi, candicatesCPLX, false);
    else
        notifySMS(pfi, candicatesCPLX, false);

    if (!candicatesGS.empty()) {
        copyVector(candicatesGS, addresses, pc);
    } else if (!candicatesCS.empty()) {
        copyVector(candicatesCS, addresses, pc);
    } else {
        copyVector(candicatesCPLX, addresses, pc);
    }
}

void
IPCP::notifyGS(const PrefetchInfo &pfi,
                  std::vector<AddrPriority> &addresses,
                  bool aggresive)
{
    // Get required packet info
    Addr train_addr = pfi.getAddr();
    Addr line_addr = train_addr >> lBlkSize;
    Addr pc = pfi.getPC();
    bool is_secure = pfi.isSecure();
    RSTEntry * rentry = rstTable->findEntry(line_addr, is_secure);
    IPGSEntry * ientry = ipgsTable->findEntry(pc, is_secure);

    if (rentry == nullptr) {
        statsIPCP.table_miss++;
        rentry = rstTable->findVictim(line_addr);

        rentry->lastLineOffset = lineOffset(line_addr);

        // May come to a new region
        if (ientry) {
            RSTEntry * last = rstTable->findEntry(ientry->lastTrainAddr >> lBlkSize, is_secure);
            if (last && last->trained) {
                rentry->tentative = true;
                rentry->direction = last->direction;
            }
        }
         else {
            rentry->bitVector[rentry->lastLineOffset] = true;
        }
        rstTable->insertEntry(line_addr, is_secure, rentry);
    }
    else {
        rstTable->accessEntry(rentry);

        // Only update rentry when region is not dense
        if (!rentry->trained) {
            Addr currentLineOffset = lineOffset(line_addr);
            rentry->bitVector[currentLineOffset] = true;
            if (currentLineOffset - rentry->lastLineOffset != 0) {
                if (currentLineOffset - rentry->lastLineOffset > 0) 
                    rentry->pnCount++;
                else
                    rentry->pnCount--;
            }
            rentry->lastLineOffset = currentLineOffset;
            double ratio = (double) std::count(rentry->bitVector.begin(), rentry->bitVector.end(), true) 
                            / rentry->bitVector.size();
            if (ratio >= 0.75) {
                rentry->trained = true;
                rentry->direction = rentry->pnCount.calcSaturation() > 0.5 ? 1 : -1;
            }
        }
    }
    bool replaced = false;
    if (ientry == nullptr) {
        statsIPCP.table_miss++;
        ientry = ipgsTable->findVictim(pc);
        replaced = true;
    }
    if (rentry->trained || rentry->tentative) {
        ientry->streamValid = true;
        ientry->forward = rentry->direction == 1;
    } else {
        ientry->streamValid = false;
    }
    ientry->lastTrainAddr = train_addr;

    if (ientry->streamValid) {
        Addr startLineAddr = ientry->lastPrefetchedLineAddr != 0 && aggresive? ientry->lastPrefetchedLineAddr : line_addr;
        for (int d = 1; d <= globalDegree; d++) {
            int direct = ientry->forward? 1 : -1;
            Addr new_line = startLineAddr + d * direct;
            Addr new_addr = new_line << lBlkSize;
            addresses.push_back(AddrPriority(new_addr, 0));
            if(!insertSdbTable(new_addr, pc, 3)) {
                sampleTable[pc]->issueByGS++;
                statsIPCP.GS_issue++;
            }
            ientry->lastPrefetchedLineAddr = startLineAddr + d;
        }
    }
    if (replaced)
        ipgsTable->insertEntry(pc, is_secure, ientry);
}

void
IPCP::notifyCS(const PrefetchInfo &pfi,
                  std::vector<AddrPriority> &addresses,
                  bool aggresive)
{
    // Get required packet info
    Addr train_addr = pfi.getAddr();
    Addr line_addr = train_addr >> lBlkSize;
    Addr pc = pfi.getPC();
    bool is_secure = pfi.isSecure();
    
    StrideEntry *sentry = pcTable->findEntry(pc, is_secure);
    if (sentry == nullptr) {
        statsIPCP.table_miss++;
        // Miss in pc table, only consider issue GS prefetch request
        DPRINTF(HWPrefetch, "Miss in pc table: PC %x pkt_addr %x (%s)\n", pc, train_addr,
                is_secure ? "s" : "ns");

        sentry = pcTable->findVictim(pc);

        // Insert new entry's data
        sentry->lastLineAddr = line_addr;
        pcTable->insertEntry(pc, is_secure, sentry);
    }
    else {
        pcTable->accessEntry(sentry);

        //update stride entry
        bool changed = updateStrideInfo(sentry, line_addr);
        if (changed) {
            sentry->lastPrefetchedLineAddr = 0;
        }
        bool enableBP = sentry->lastPrefetchedLineAddr != 0 && aggresive;
        //choose prefetching requests
        Addr startLineAddr = enableBP? sentry->lastPrefetchedLineAddr : line_addr;
        if (sentry->confidence >= 1){
            // Generate up to degree prefetches
            for (int d = 1; d <= globalDegree; d++) {
                int prefetch_stride = sentry->stride;
                Addr new_addr = (startLineAddr + prefetch_stride * d) << lBlkSize;

                if (enableBP) {
                    addresses.push_back(AddrPriority(new_addr, 0));
                    if(!insertSdbTable(new_addr, pc, 2)) {
                        sampleTable[pc]->issueByCS++;
                        statsIPCP.CS_issue++;
                    }
                    sentry->lastPrefetchedLineAddr = startLineAddr + prefetch_stride * d;
                } else {
                    addresses.push_back(AddrPriority(new_addr, 0));
                    if(!insertSdbTable(new_addr, pc, 2)) {
                        sampleTable[pc]->issueByCS++;
                        statsIPCP.CS_issue++;
                    }
                    sentry->lastPrefetchedLineAddr = startLineAddr + prefetch_stride * d;
                }
            }
        }
    }
}

// need to insertSdbTable
void
IPCP::notifyCPLX(const PrefetchInfo &pfi,
                  std::vector<AddrPriority> &addresses,
                  bool aggresive)
{
    Addr train_addr = pfi.getAddr();
    Addr line_addr = train_addr >> lBlkSize;
    Addr line_offset = (train_addr % 4096) / 64;
    Addr pc = pfi.getPC();
    Addr old_signature = 0;
    Addr last_line_addr = 0;
    Addr new_signature = 0;
    int delta = 0;

    // write signature table
    IPCPLXEntry * ientry = ipcplxTable->findEntry(pc, false);
    if (ientry) {
        old_signature = ientry->signature;
        last_line_addr = ientry->lastTrainAddr >> lBlkSize;
        delta = line_addr - last_line_addr;
        ientry->lastTrainAddr = train_addr;
        if (delta == 0) {
            return;
        }
        else if (std::abs(delta) > 63) {
            ientry->signature = 0;
            ientry->lastPrefetchedLineAddr = 0;
            return;
        }
        new_signature = (old_signature << 1) ^ delta;
        ientry->signature = new_signature;
    } else {
        statsIPCP.table_miss++;
        new_signature = old_signature ^ line_offset;
        ientry = ipcplxTable->findVictim(pc);
        ientry->lastTrainAddr = train_addr;
        ientry->lastPrefetchedLineAddr = 0;
        ientry->signature = new_signature;
        ipcplxTable->insertEntry(pc, false, ientry);
    }

    // use old siganture and new delta to update CSPT table
    if (delta != 0) {
        int idx = old_signature % 128;
        if (csptTable[idx].stride != delta) {
            if (csptTable[idx].confidence == 0) {
                csptTable[idx].stride = delta;
                csptTable[idx].confidence = 0;
            } else {
                csptTable[idx].confidence--;
            }
        } else {
            csptTable[idx].confidence = std::max(csptTable[idx].confidence + 1, 3);
        }
        assert(csptTable[idx].confidence >= 0);
    }
    // use new signature to index CSPT table, make sure using 7 bits to index the CSPT table
    // breakpoint for CPLX
    oldSignatures.push_back(new_signature);
    if (oldSignatures.size() == 5) {
        oldSignatures.erase(oldSignatures.begin());
        assert(oldSignatures.size() == 4);
    }
    int lookup_sig = new_signature % 128;
    bool match = false;
    for (auto s: oldSignatures) {
        if (lookup_sig == s % 128)
            match = true;
    }
    Addr startLineAddr = line_addr;
    Addr startSignature = lookup_sig;
    bool enableBP = ientry->lastPrefetchedLineAddr != 0 && aggresive && match;
    if (enableBP) {
        startLineAddr = ientry->lastPrefetchedLineAddr;
        startSignature = ientry->lastSignature;
    } else {
        ientry->lastPrefetchedLineAddr = 0;
        ientry->lastSignature = 0;
    }
    for (int i = 0; i < globalDegree; i++) {
        int stride = csptTable[startSignature % 128].stride;
        assert(abs(stride) < 64);
        int conf = csptTable[startSignature % 128].confidence;
        if (conf >= 1) {
            Addr new_addr = (startLineAddr + stride) << lBlkSize;
            addresses.push_back(AddrPriority(new_addr, 0));
            if(!insertSdbTable(new_addr, pc, 1)) {
                sampleTable[pc]->issueByCPLX++;
                statsIPCP.CPLX_issue++;
            }
            ientry->lastPrefetchedLineAddr = startLineAddr + stride;
            ientry->lastSignature = (startSignature << 1) ^ stride;
        } else {
            ientry->lastPrefetchedLineAddr = 0;
            ientry->lastSignature = 0;
            if (enableBP) {
                break;
            }
        }
        startLineAddr = startLineAddr + stride;
        startSignature = (startSignature << 1) ^ stride;
    }
}

void
IPCP::notifySMS(const PrefetchInfo &pfi,
                  std::vector<AddrPriority> &addresses,
                  bool aggresive)
{
    Addr pc = pfi.getPC();
    Addr vaddr = pfi.getAddr();
    Addr block_addr = blockAddress(vaddr);
    // Addr region_addr = regionAddress(vaddr);
    Addr region_offset = regionOffset(vaddr);

    actLookup(pfi);

    // ***phtLookup***
    PhtEntry *pht_entry = pht.findEntry(pc, false);
    if (pht_entry) {
        pht.accessEntry(pht_entry);
        DPRINTF(SMSPrefetcher,
                "Pht lookup hit: pc: %x, vaddr: %x, offset: %x\n", pc, vaddr,
                region_offset);
        // find incr pattern
        int issued = 0;
        for (uint8_t i = 0; i < region_blocks - 1; i++) {
            if (pht_entry->hist[i + region_blocks - 1].calcSaturation() > 0.5) {
                if (issued == globalDegree)
                    break;
                issued++;
                Addr pf_tgt_addr = block_addr + (i + 1) * blkSize;
                addresses.push_back(AddrPriority(pf_tgt_addr, 0));
                if(!insertSdbTable(pf_tgt_addr, pc, 1)) {
                    sampleTable[pc]->issueByCPLX++;
                    statsIPCP.CPLX_issue++;
                }
            }
        }
        issued = 0;
        for (int i = region_blocks - 2, j = 1; i >= 0; i--, j++) {
            if (pht_entry->hist[i].calcSaturation() > 0.5) {
                if (issued == globalDegree)
                    break;
                issued++;
                Addr pf_tgt_addr = block_addr - j * blkSize;
                addresses.push_back(AddrPriority(pf_tgt_addr, 0));
                if(!insertSdbTable(pf_tgt_addr, pc, 1)) {
                    sampleTable[pc]->issueByCPLX++;
                    statsIPCP.CPLX_issue++;
                }
            }
        }
    }
}

IPCP::ACTEntry *
IPCP::actLookup(const PrefetchInfo &pfi)
{
    Addr pc = pfi.getPC();
    Addr vaddr = pfi.getAddr();
    Addr region_addr = regionAddress(vaddr);
    Addr region_offset = regionOffset(vaddr);
    bool secure = pfi.isSecure();

    ACTEntry *entry = act.findEntry(region_addr, secure);
    if (entry) {
        // act hit
        act.accessEntry(entry);
        uint64_t region_bit_accessed = 1 << region_offset;
        if (!(entry->region_bits & region_bit_accessed)) {
            entry->access_cnt += 1;
        }
        entry->region_bits |= region_bit_accessed;
        return entry;
    }

    entry = act.findEntry(region_addr - 1, secure);
    if (entry) {
        act.accessEntry(entry);
        // act miss, but cur_region - 1 = entry_region, => cur_region =
        // entry_region + 1
        entry = act.findVictim(0);
        // evict victim entry to pht
        updatePht(entry);
        // alloc new act entry
        entry->pc = pc;
        entry->is_secure = secure;
        entry->decr_mode = false;
        entry->region_bits = 1 << region_offset;
        entry->access_cnt = 0;
        entry->region_offset = region_offset;
        act.insertEntry(region_addr, secure, entry);
        return entry;
    }

    entry = act.findEntry(region_addr + 1, secure);
    if (entry) {
        act.accessEntry(entry);
        // act miss, but cur_region + 1 = entry_region, => cur_region =
        // entry_region - 1
        entry = act.findVictim(0);
        // evict victim entry to pht
        updatePht(entry);
        // alloc new act entry
        entry->pc = pc;
        entry->is_secure = secure;
        entry->decr_mode = true;
        entry->region_bits = 1 << region_offset;
        entry->access_cnt = 0;
        entry->region_offset = region_offset;
        act.insertEntry(region_addr, secure, entry);
        return entry;
    }

    // no matched entry, alloc new entry
    entry = act.findVictim(0);
    updatePht(entry);
    entry->pc = pc;
    entry->is_secure = secure;
    entry->decr_mode = false;
    entry->region_bits = 1 << region_offset;
    entry->access_cnt = 1;
    entry->region_offset = region_offset;
    act.insertEntry(region_addr, secure, entry);
    return nullptr;
}

void
IPCP::updatePht(IPCP::ACTEntry *act_entry)
{
    if (!act_entry->region_bits) {
        return;
    }
    PhtEntry *pht_entry = pht.findEntry(act_entry->pc, act_entry->is_secure);
    bool is_update = pht_entry != nullptr;
    if (!pht_entry) {
        pht_entry = pht.findVictim(act_entry->pc);
        for (uint8_t i = 0; i < 2 * (region_blocks - 1); i++) {
            pht_entry->hist[i].reset();
        }
    } else {
        pht.accessEntry(pht_entry);
    }
    Addr region_offset = act_entry->region_offset;
    // incr part
    for (uint8_t i = region_offset + 1, j = 0; i < region_blocks; i++, j++) {
        uint8_t hist_idx = j + (region_blocks - 1);
        bool accessed = (act_entry->region_bits >> i) & 1;
        if (accessed) {
            pht_entry->hist[hist_idx]++;
        } else {
            pht_entry->hist[hist_idx]--;
        }
    }
    // decr part
    for (int i = int(region_offset) - 1, j = region_blocks - 2; i >= 0;
         i--, j--) {
        bool accessed = (act_entry->region_bits >> i) & 1;
        if (accessed) {
            pht_entry->hist[j]++;
        } else {
            pht_entry->hist[j]--;
        }
    }
    if (!is_update) {
        pht.insertEntry(act_entry->pc, act_entry->is_secure, pht_entry);
    }
}

bool
IPCP::updateStrideInfo(StrideEntry * sentry, Addr currentLine) {
    int new_stride = currentLine - sentry->lastLineAddr;
    bool stride_match = (new_stride == sentry->stride);
    bool changed = false;
    if (stride_match && new_stride != 0) {
        if (sentry->confidence < 2) {
            sentry->confidence++;
        }
    } else {
        changed = true;
        if (sentry->confidence == 0) {
            sentry->stride = new_stride;
            sentry->confidence = 0;
        }
        else {
            sentry->confidence--;
        }
    }
    sentry->lastLineAddr = currentLine;
    return changed;
}

void
IPCP::notifyBerti(const PrefetchInfo &pfi,
                  std::vector<AddrPriority> &addresses,
                  bool aggresive)
{

    cleanPrefetchLatency();

    // 2. Learning timely deltas

    //A new entry is inserted in the history table either (1) on-demand misses or (2) on hits for prefetched cache lines.
    if(pfi.isCacheMiss()||(!pfi.isCacheMiss()&&hasBeenPrefetched(pfi.getAddr(), pfi.isSecure())))
    {
        updateHistoryTable(pfi);
    }

    // The search for timely deltas is performed either (1) on a fill due to a demand access or (2) on a hit due to a prefetched cache line
    if(!pfi.isCacheMiss()&&hasBeenPrefetched(pfi.getAddr(), pfi.isSecure()))
    {
        HistoryTableEntry *hist_entry = historyTable.findEntry(
            pfi.getPC(), pfi.isSecure());
        if (hist_entry) {

            bool found = false;
            std::list<prefetch_fill_latency>::iterator it;
            for (it = prefetch_latency_container.begin(); it != prefetch_latency_container.end() && !found; it++) {
                found = (it->addr == blockAddress(pfi.getAddr()));
            }
            assert(found);
            Cycles PrefetchFillLatency = it->latency;
            std::vector<int64_t> deltas;
            searchTimelyDeltas(*hist_entry, PrefetchFillLatency,
                               curCycle(),
                               blockIndex(pfi.getAddr()), deltas);
            updateTableOfDeltas(pfi.getPC(), pfi.isSecure(), deltas);
        }
    }


    // Issuing prefetch requests

    TableOfDeltasEntry *entry =
        tableOfDeltas.findEntry(pfi.getPC(), pfi.isSecure());
    int issued = 0;
    if (entry) {
        tableOfDeltas.accessEntry(entry);
        for (auto &delta_info : entry->deltas) {
            if (delta_info.status == L2_PREF) {
                if (issued == globalDegree)
                    break;
                int64_t delta = delta_info.delta;
                Addr pf_addr =
                    (blockIndex(pfi.getAddr()) + delta) << lBlkSize;
            
                issued++;
                addresses.push_back(AddrPriority(pf_addr, 0));
            }
        }
    }
}

void IPCP::searchTimelyDeltas(
    const HistoryTableEntry &entry,
    const Cycles &latency,
    const Cycles &demand_cycle,
    const Addr &blk_addr,
    std::vector<int64_t> &deltas)
{
    for (auto it = entry.history.rbegin(); it != entry.history.rend(); it++) {
        // if not timely, skip and continue
        if (it->timestamp + latency > demand_cycle)
            continue;
        int64_t delta = blk_addr - it->lineAddr;
        if (delta != 0) {
            deltas.push_back(delta);
            // We don't want to many deltas
            if (deltas.size() == 8)
                break;
        }
    }
}


void IPCP::notifyFill4Miss(const PacketPtr &pkt)
{
    notifyFill(pkt);
}

void IPCP::notifyFill(const PacketPtr &pkt)
{
    if (pkt->req->isInstFetch() ||
        !pkt->req->hasVaddr() || !pkt->req->hasPC()) {
        return;
    }
    cleanPrefetchLatency();

    // 1. Measuring fetch latency
    
    Cycles latency = ticksToCycles(curTick() - pkt->req->time());
    lastFillLatency = latency;
    if (pkt->req->isPrefetch()) {
        prefetch_fill_latency new_latency;
        new_latency.addr = blockAddress(pkt->req->getVaddr());
        new_latency.is_secure = pkt->req->isSecure();
        new_latency.latency = lastFillLatency;
        prefetch_latency_container.push_back(new_latency);
    }


    /** Search history table, find deltas. */
    Cycles demand_cycle = ticksToCycles(pkt->req->time());
    Cycles wrappedLatency;

    if (latency % 10 == 0) {
        wrappedLatency = Cycles((latency / 10) * 10);
    } else {
        wrappedLatency = Cycles( ((latency / 10) + 1) * 10 );
    }
    

    // 2. Learning timely deltas

    // The search for timely deltas is performed either (1) on a fill due to a demand access or (2) on a hit due to a prefetched cache line
    if (!pkt->req->isPrefetch()) {
        HistoryTableEntry *entry =
                 historyTable.findEntry(pkt->req->getPC(), pkt->req->isSecure());
        if (!entry)
            return;
        std::vector<int64_t> timely_deltas = std::vector<int64_t>();
        searchTimelyDeltas(*entry, lastFillLatency, demand_cycle,
                       blockIndex(pkt->req->getVaddr()),
                       timely_deltas);
        updateTableOfDeltas(pkt->req->getPC(), pkt->req->isSecure(),
                        timely_deltas);
    }
}

void
IPCP::updateHistoryTable(const PrefetchInfo &pfi)
{
    HistoryTableEntry *entry =
        historyTable.findEntry(pfi.getPC(), pfi.isSecure());
    HistoryInfo new_info = {blockIndex(pfi.getAddr()), curCycle()};
    if (entry) {
        if (entry->history.size() == 16) {
            entry->history.erase(entry->history.begin());
        }
        entry->history.push_back(new_info);
    } else {
        entry = historyTable.findVictim(pfi.getPC());
        historyTable.invalidate(entry);
        entry->history.clear();
        entry->history.push_back(new_info);
        historyTable.insertEntry(pfi.getPC(), pfi.isSecure(), entry);
    }
}

void
IPCP::updateTableOfDeltas(
    const Addr pc, const bool isSecure,
    const std::vector<int64_t> &new_deltas)
{
    if (new_deltas.empty())
        return;

    TableOfDeltasEntry *entry =
        tableOfDeltas.findEntry(pc, isSecure);
    if (!entry) {
        entry = tableOfDeltas.findVictim(pc);
        tableOfDeltas.invalidate(entry);
        entry->resetConfidence(true);
        tableOfDeltas.insertEntry(pc, isSecure, entry);
    }

    entry->counter++;
    for (auto &delta : new_deltas) {
        bool miss = true;
        for (auto &delta_info : entry->deltas) {
            if (delta_info.coverageCounter != 0 && delta_info.delta == delta) {
                delta_info.coverageCounter++;
                miss = false;
                break;
            }
        }
        // miss
        if (miss) {
            int replace_idx = 0;
            for (auto i = 1; i < entry->deltas.size(); i++) {
                if (entry->deltas[replace_idx].coverageCounter
                    >= entry->deltas[i].coverageCounter) {
                    replace_idx = i;
                }
            }
            entry->deltas[replace_idx].delta = delta;
            entry->deltas[replace_idx].coverageCounter = 1;
            entry->deltas[replace_idx].status = NO_PREF;
        }
    }

    if (entry->counter >= 8) {
        entry->updateStatus();
        if (entry->counter == 16) {
            /** Start a new learning phase. */
            entry->resetConfidence(false);
        }
    }
}

void
IPCP::interpretHintFile(const std::string &hintContent)
{   
    // Map from PC to (instruction, successor PC) pairs
    std::ifstream stream(hintContent);
    std::string line;
    
    // Process each line in the hint file
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        
        // Indirect chain format: 0:start_pc,end_pc; pc1,instruction1 -> pc2,instruction2 -> ...
        if (line[0] == '0') {
            
            // Extract the chain part after the semicolon
            size_t semicolonPos = line.find(';');
            if (semicolonPos == std::string::npos) continue;
            
            std::string startEndPcStr = line.substr(2, semicolonPos - 2);
            std::string startPcStr = startEndPcStr.substr(0, startEndPcStr.find(','));
            std::string endPcStr = startEndPcStr.substr(startEndPcStr.find(',') + 1);
            printf("start_pc: %s, end_pc: %s\n", startPcStr.c_str(), endPcStr.c_str());

            std::string chainPart = line.substr(semicolonPos + 1);
            
            // Split the chain into instruction segments
            std::vector<std::string> segments;
            size_t arrowPos = 0;
            size_t startPos = 0;
            
            while ((arrowPos = chainPart.find("->", startPos)) != std::string::npos) {
                segments.push_back(chainPart.substr(startPos, arrowPos - startPos));
                startPos = arrowPos + 2; // Skip "->"
            }
            // Add the last segment
            segments.push_back(chainPart.substr(startPos));
            
            // Process each segment to extract PC and instruction
            for (size_t i = 0; i < segments.size() - 1; i++) {
                std::string currentSegment = segments[i];
                std::string nextSegment = segments[i + 1];
                
                // Extract PC from current segment
                size_t commaPos = currentSegment.find(',');
                if (commaPos == std::string::npos) continue;
                
                std::string pcStr = currentSegment.substr(0, commaPos);
                std::string instruction = currentSegment.substr(commaPos + 1);
                
                // Trim whitespace
                pcStr.erase(0, pcStr.find_first_not_of(" \t"));
                pcStr.erase(pcStr.find_last_not_of(" \t") + 1);
                instruction.erase(0, instruction.find_first_not_of(" \t"));
                instruction.erase(instruction.find_last_not_of(" \t") + 1);

                printf("pcStr: %s, instruction: %s\n", pcStr.c_str(), instruction.c_str());
                
                // Extract successor PC from next segment
                size_t nextCommaPos = nextSegment.find(',');
                if (nextCommaPos == std::string::npos) continue;

                std::string successorPcStr = nextSegment.substr(0, nextCommaPos);
                std::string successorinstruction = nextSegment.substr(nextCommaPos + 1);

                // Trim whitespace
                successorPcStr.erase(0, successorPcStr.find_first_not_of(" \t"));
                successorPcStr.erase(successorPcStr.find_last_not_of(" \t") + 1);
                successorinstruction.erase(0, successorinstruction.find_first_not_of(" \t"));
                successorinstruction.erase(successorinstruction.find_last_not_of(" \t") + 1);
                
                // Convert PC strings to Addr
                Addr pc = std::stoul(pcStr, nullptr, 16);
                Addr successorPc = std::stoul(successorPcStr, nullptr, 16);
                
                // Add to dependence graph
                bool exist = dependenceGraph.find(pc) != dependenceGraph.end();
                if (!exist) {
                    dependenceGraph[pc] = DepSucEntry(successorinstruction, successorPc);
                }
            }
        } 
    }
}

void
IPCP::notifyFillPrefetchData(const PacketPtr& pkt)
{
    if (!enableRPG)
        return;
    
    Addr currentPC = pkt->req->getPC();
    Addr offset = pkt->getPfVaddr() & Addr((1 << lBlkSize) - 1);
    uint64_t * data = (uint64_t*)(pkt->getPtr<uint8_t>());

    // Look for this PC in the dependence graph
    if (dependenceGraph.find(currentPC) != dependenceGraph.end()) {
        auto it = dependenceGraph[currentPC];

        // Extract the loaded data value - this will be used as the base address for prefetching
        // Convert byte offset to 64-bit word offset
        uint64_t loadedValue = data[offset / 8];
        processInstructionChain(pkt, it, loadedValue, currentPC);
    }
}

void 
IPCP::processInstructionChain(const PacketPtr& pkt, 
                                  const DepSucEntry& depPair,
                                  uint64_t baseAddr,
                                  Addr currentPC)
{
    // For each dependent instruction in the chain
    const std::string& instruction = depPair.suc_inst;
    Addr nextPC = depPair.suc_pc;
    std::vector<AddrPriority> addresses;
    
    // Check if this is a load instruction by examining the first word of the instruction
    std::string op = instruction.substr(0, instruction.find('\t'));

    bool isLoad = (op == "ld" || op == "lw" || op == "lh" || op == "lhu" || 
                    op == "lbu" || op == "lwu" || op == "fld" || op == "flw");
    
    if (isLoad) {
        // Extract the offset from the instruction
        // Format: ld rd, offset(rs)
        size_t openParenPos = instruction.find('(');
        size_t closeParenPos = instruction.find(')');
        
        if (openParenPos != std::string::npos && closeParenPos != std::string::npos) {
            // Extract the offset part
            std::string offsetPart = instruction.substr(instruction.find(',') + 1, 
                                                        openParenPos - instruction.find(',') - 1);
            offsetPart = offsetPart.substr(offsetPart.find_first_not_of(" \t"));
            
            // Convert offset to value
            int64_t offsetValue = 0;
            if (!offsetPart.empty()) {
                offsetValue = std::stoll(offsetPart);
            }
            
            // Calculate address using baseAddr + offset
            Addr addr = baseAddr + offsetValue;
            Addr line_addr = addr >> lBlkSize;
            Addr offset = addr & Addr((1 << lBlkSize) - 1);

            // Add to prefetch addresses
            addresses.push_back(AddrPriority((line_addr << lBlkSize) | offset, 0));
            issuePrefetch(pkt, addresses, nextPC);
        }
        // No further traversal for load instructions
    }
}

void 
IPCP::issuePrefetch(PacketPtr pkt, const std::vector<AddrPriority>& addresses, Addr nextPC)
{
    if (!addresses.empty()) {
        // Create prefetch info for each address
        for (const auto& addrPrio : addresses) {
            // Create PrefetchInfo using the packet and the calculated address
            PrefetchInfo pfi(pkt, pkt->getPfVaddr() & ~Addr((1 << lBlkSize) - 1), false);
            pfi.setPC(nextPC);
            
            // Issue the prefetch request
            std::vector<AddrPriority> prefetchAddresses;
            prefetchAddresses.push_back(addrPrio);
            
            notifyPrecomputation(pkt, pfi, prefetchAddresses);
        }
    }
}

} // namespace prefetch
} // namespace gem5
