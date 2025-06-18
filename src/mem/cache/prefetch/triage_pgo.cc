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

#include "mem/cache/prefetch/triage_pgo.hh"

#include <cassert>

#include "mem/cache/base.hh"
#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "debug/HWPrefetch.hh"
#include "mem/cache/prefetch/associative_set_impl.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "params/TriagePGOPrefetcher.hh"
#include "debug/TriagePGO.hh"
#include "debug/TriagePGODebug.hh"
#include "mem/cache/prefetch/queued.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

TriagePGO::TriagePGOStats::TriagePGOStats(statistics::Group *parent)
    : statistics::Group(parent),
      ADD_STAT(metada_rp, statistics::units::Count::get(), ""),
      ADD_STAT(reuse_rp, statistics::units::Count::get(), ""),
      ADD_STAT(recv_trains, statistics::units::Count::get(), ""),
      ADD_STAT(meta_inserted, statistics::units::Count::get(), ""),
      ADD_STAT(meta_used, statistics::units::Count::get(), ""),
      ADD_STAT(metaTableUsedEntries, statistics::units::Count::get(), ""),
      ADD_STAT(meta_hit, statistics::units::Count::get(), ""),
      ADD_STAT(meta_access, statistics::units::Count::get(), ""),
      ADD_STAT(issued_by_reuse, statistics::units::Count::get(), ""),
      ADD_STAT(issued_by_metatable, statistics::units::Count::get(), "")
{
}

TriagePGO::TriagePGO(const TriagePGOPrefetcherParams &p)
  : Queued(p),
  cachetags(p.cachetags),
  benchmark(p.pgo_benchmark),
  enableInsertFilter(p.enable_insert_filter),
  enablePGLRU(p.enable_pg_lru),
  enableDRA(p.enable_dra),
  numEntriesinTable(0),
  global_timestamp(0),
  allMisses(0),
  statsTriagePGO(this)
{
    metaTable = new AssociativeSet<MetaEntry>(p.meta_assoc, p.meta_entries,
        p.meta_indexing_policy, p.meta_replacement_policy,
        MetaEntry(p.candidates_per_meta_entry, 1));
    
    mrbTable = new AssociativeSet<MetaEntry>(p.metadata_reuse_assoc, p.metadata_reuse_entries,
        p.metadata_reuse_indexing_policy, p.metadata_reuse_replacement_policy,
        MetaEntry(p.candidates_per_reuse_entry, 2));

    inTraining = p.train;
    enableMRB = p.enable_mrb;
    if (!inTraining) {
        globalDegree = p.global_degree;
        std::string file_name = "/home/mliet/gem5/pgo/profile_re/" + benchmark + "/insertion.txt";
        std::ifstream pc_file(file_name);
        if (!pc_file) {
            std::cerr << "Unable to open file example.txt";
            assert(false);
        }
        
        std::string line;
        while (std::getline(pc_file, line)) {
            std::istringstream lineStream(line);
            std::string PC;
            std::string degree;

            if (std::getline(lineStream, PC, ',') && std::getline(lineStream, degree)) {
                // PC and degree are now split into two variables
                profileInsertTable.insert(std::stoull (PC, nullptr ,16));
                profileReplTable[std::stoull (PC, nullptr ,16)] = std::stoull (degree);
            } else {
                std::cerr << "Error: Incorrectly formatted line." << std::endl;
            }
        }
        pc_file.close();


        std::string file_name2 = "/home/mliet/gem5/pgo/profile_re/" + benchmark + "/partition.txt";
        std::ifstream partition_file(file_name2);
        if (!partition_file) {
            std::cerr << "Unable to open file example.txt";
            assert(false);
        }
        
        disablePF = false;
        while (std::getline(partition_file, line)) {
            if (line.compare("262144") == 0) {
                cachetags->setWayAllocationMax(8);
                metaTable->setWayAllocationMax(12);
            } else if (line.compare("131072") == 0) {
                cachetags->setWayAllocationMax(12);
                metaTable->setWayAllocationMax(12);
            } else if (line.compare("65536") == 0) {
                cachetags->setWayAllocationMax(14);
                metaTable->setWayAllocationMax(12);
            } else if (line.compare("32768") == 0) {
                cachetags->setWayAllocationMax(15);
                metaTable->setWayAllocationMax(12);
            } else if (line.compare("0") == 0) {
                cachetags->setWayAllocationMax(16);
                disablePF = true;
            } else {
                std::cerr << "Error: Incorrectly formatted line." << std::endl;
            }
        }
        partition_file.close();
    } else {
        globalDegree = 1;
        enableMRB = false;
    }
    out_file = p.output_file + "/train.txt";
}



void
TriagePGO::calculatePrefetch(const PacketPtr &pkt, const PrefetchInfo &pfi,
                                    std::vector<AddrPriority> &addresses)
{
    if (disablePF)
        return;
    if (enableDRA && (pkt->isFromPrefetcher() || !pkt->isUsedForPfTrain()))
        return;
    if (!pfi.hasPC()) {
        DPRINTF(HWPrefetch, "Ignoring request with no PC.\n");
        return;
    }

    // Get required packet info
    Addr pf_addr = pfi.getAddr();
    Addr line_addr = pf_addr >> lBlkSize;
    Addr pc = pfi.getPC();
    bool prefetchHit = pfi.isPrefetchHit();
    bool miss = pfi.isCacheMiss();
    DPRINTF(TriagePGO, "prefetchHit %d, miss %d\n", prefetchHit, miss);


    if (miss || prefetchHit) {
        allMisses += 1;
        if (missTable.find(pc) == missTable.end())
            missTable[pc] = 1;
        else
            missTable[pc] += 1;
        DPRINTF(TriagePGO, "Miss Table of PC %x is: %d\n", pc, missTable[pc]);
    }


    if (trainTable.find(pc) == trainTable.end()) {
        if (trainTable.size() < 128) {
            trainTable[pc] = TrainEntry();
        } else {
            std::map<Addr, TrainEntry>::iterator minPointer = trainTable.begin();
            for (std::map<Addr, TrainEntry>::iterator it=trainTable.begin(); it!=trainTable.end(); ++it) {
                if (it->second.solved < minPointer->second.solved) {
                    minPointer = it;
                }
            }
            if (!minPointer->second.protect) {
                trainTable.erase(minPointer);
                trainTable[pc] = TrainEntry();
            } else {
                minPointer->second.protect = false;
            }
        }
    } else {
        if (prefetchHit)
            trainTable[pc].solved += 1;
    }
    DPRINTF(TriagePGO, "trainTable: PC %x, solved is %d, issued is %d\n", pc, trainTable[pc].solved, trainTable[pc].issued);

    if (enableInsertFilter && !inTraining && profileInsertTable.find(pc) == profileInsertTable.end())
        return;

    statsTriagePGO.recv_trains++;
    global_timestamp++;
    DPRINTF(TriagePGO, "SEARCH: line %x, pc %x\n", line_addr, pc);
    
    // 1.search
    MetaEntry * metadata = metaTable->findEntry(line_addr, false);
    MetaEntry * reuseData = mrbTable->findEntry(line_addr, false);
    statsTriagePGO.metaTableUsedEntries = numEntriesinTable;
    Addr lastAddr = pcTable.find(pc) != pcTable.end() ? pcTable[pc] : 0;
    if (lastAddr == line_addr)
        return;
    statsTriagePGO.meta_access++;
    if (reuseData) {
        mrbTable->accessEntry(reuseData);
    }
    if (metadata) {
        long long diff = global_timestamp - metadata->touch_time;
        metadata->touch_time = global_timestamp;
        DPRINTFR(TriagePGODebug, "%ld\n", diff);

        statsTriagePGO.meta_hit++;
        metaTable->accessEntry(metadata);
        if (profileUtiTable.find(line_addr) != profileUtiTable.end()) {
            profileUtiTable[line_addr]++;
        }
        else {
            profileUtiTable[line_addr] = 0;
        }
        if (!metadata->used) {
            metadata->used = true;
            trainTable[metadata->pc].meta_used++;
            statsTriagePGO.meta_used++;
        }
    }

    // 2.issue: metadata table
    Addr lookup = line_addr;
    std::vector<AddrPriority> next_level_addrs;
    int issued_by_metatable = issue(metaTable, lookup, pc, addresses, next_level_addrs);
    statsTriagePGO.issued_by_metatable += issued_by_metatable;
    if (enableMRB) {
        int issued_by_reuse = issue(mrbTable, lookup, pc, addresses, next_level_addrs);
        statsTriagePGO.issued_by_reuse += issued_by_reuse;
    }
    

    // 3.update
    // 3.1 update the metaTable

    if (lastAddr != 0) {
        MetaEntry * lastMeta = metaTable->findEntry(lastAddr, false);
        if (lastMeta) {
            bool matched = false;
            for (int i = 0; i < lastMeta->entries.size(); i++) {
                if (lastMeta->entries[i].correlatedAddr == line_addr) {
                    matched = true;
                    if (lastMeta->entries[i].counter.isSaturated()) {
                        int minVal = 1000000;
                        for (int j = 0; j < lastMeta->entries.size(); j++) {
                            if (lastMeta->entries[j].counter < minVal)
                                minVal = lastMeta->entries[j].counter;
                        }
                        for (int j = 0; j < lastMeta->entries.size(); j++) {
                            lastMeta->entries[j].counter -= minVal;
                        }
                    }
                    lastMeta->entries[i].counter++;
                    break;
                }
            }
            if (!matched) {
                DPRINTF(TriagePGO, "No match: add new %x\n", line_addr);
                for (int i = 1; i < lastMeta->entries.size(); i++) {
                    lastMeta->entries[i].counter -= 1;
                }
                int repl = 0;
                int minVal = lastMeta->entries[0].counter;
                for (int i = 1; i < lastMeta->entries.size(); i++) {
                    if (lastMeta->entries[i].counter < minVal) {
                        minVal = lastMeta->entries[i].counter;
                        repl = i;
                    }
                }
                Addr replacedAddr = line_addr;

                if (minVal == 0) {
                    replacedAddr = lastMeta->entries[repl].correlatedAddr;
                    lastMeta->entries[repl].counter.reset();
                    lastMeta->entries[repl].counter += 1;
                    lastMeta->entries[repl].correlatedAddr = line_addr;
                }

                // victim buffer logic
                if (profileReplTable[pc] > 1) {
                    MetaEntry * victimMeta = mrbTable->findEntry(lastAddr, false);
                    if (!victimMeta) {
                        bool replaced = false;
                        victimMeta = mrbTable->findVictimHint(lastAddr, replaced);
                        victimMeta->entries[0].correlatedAddr = replacedAddr;
                        victimMeta->entries[0].counter.reset();
                        victimMeta->entries[0].counter += 1;
                        victimMeta->touch_time = global_timestamp;
                        victimMeta->pc = pc;
                        mrbTable->insertEntry(lastAddr, false, victimMeta, 1);
                        if (replaced)
                            statsTriagePGO.reuse_rp++;
                    } else {
                        int index = -1;
                        for (int i = 0; i < victimMeta->entries.size(); i++) {
                            if (victimMeta->entries[i].correlatedAddr == replacedAddr) {
                                index = i;
                                if (victimMeta->entries[i].counter.isSaturated()) {
                                    int minVal = 1000000;
                                    for (int j = 0; j < victimMeta->entries.size(); j++) {
                                        if (victimMeta->entries[j].counter < minVal)
                                            minVal = victimMeta->entries[j].counter;
                                    }
                                    for (int j = 0; j < victimMeta->entries.size(); j++) {
                                        victimMeta->entries[j].counter -= minVal;
                                    }
                                }
                                victimMeta->entries[i].counter++;
                                break;
                            }
                        }
                        if (index == -1) {
                            int repl = 0;
                            int minVal = victimMeta->entries[0].counter;
                            for (int i = 1; i < victimMeta->entries.size(); i++) {
                                if (victimMeta->entries[i].counter < minVal || victimMeta->entries[i].correlatedAddr == 0) {
                                    minVal = victimMeta->entries[i].counter;
                                    repl = i;
                                }
                            }
                            victimMeta->entries[repl].counter.reset();
                            victimMeta->entries[repl].counter += 1;
                            victimMeta->entries[repl].correlatedAddr = replacedAddr;
                        }
                    }
                    int max_counter = victimMeta->entries[0].counter;
                    for (int i = 1; i < victimMeta->entries.size(); i++) {
                        if (victimMeta->entries[i].counter > max_counter)
                            max_counter = victimMeta->entries[i].counter;
                    }
                    mrbTable->setPGODegree(victimMeta, max_counter);
                }

                // addToInsertedPool(lastAddr);
            }
        } else {
            DPRINTF(TriagePGO, "UPDATE: no last metadata, add new %x\n", line_addr);
            bool replaced = false;
            lastMeta = metaTable->findVictimHint(lastAddr, replaced);
            if (replaced)
                statsTriagePGO.metada_rp++;
            else
                numEntriesinTable++;
            lastMeta->pc = pc;
            lastMeta->entries[0].correlatedAddr = line_addr;
            lastMeta->entries[0].counter.reset();
            lastMeta->entries[0].counter += 1;
            lastMeta->touch_time = global_timestamp;
            if (!inTraining && enablePGLRU && profileReplTable.find(pc) != profileReplTable.end())
                metaTable->insertEntry(lastAddr, false, lastMeta, profileReplTable[pc]);
            else
                metaTable->insertEntry(lastAddr, false, lastMeta, 1);
            // addToInsertedPool(lastAddr);
            statsTriagePGO.meta_inserted++;
            trainTable[pc].meta_inserted++;
        }
    }
    
    // 3.2 update the pcTable
    pcTable[pc] = line_addr;

    if (gem5::prefetcher_array[cache->getLevel() + 1] != nullptr){
        gem5::prefetcher_array[cache->getLevel() + 1]->notifyCross(
            pkt, pfi, next_level_addrs);
    }
}

int
TriagePGO::issue(AssociativeSet<MetaEntry>* metaTable, Addr lookup, Addr pc, std::vector<AddrPriority> &addresses, std::vector<AddrPriority> &next_level_addrs) {
    int issued = 0;
    for (int i = 0; i < globalDegree; i++) {
        MetaEntry * candidates = metaTable->findEntry(lookup, false);
        if (candidates == nullptr)
            break;
        addToUsedPool(lookup);
        int counter = -1;
        DPRINTF(TriagePGO, "Lookup %x\n", lookup);
        for (int j = 0; j < candidates->entries.size(); j++) {
            if (candidates->entries[j].correlatedAddr != 0) {
                if (candidates->entries[j].counter > counter) {
                    lookup = candidates->entries[j].correlatedAddr;
                    counter = candidates->entries[j].counter;
                }
                trainTable[pc].issued += 1;
                issued++;
                if (i < globalDegree) {
                    if (!isAlreadyInQueue(addresses, candidates->entries[j].correlatedAddr << lBlkSize)) {
                        addresses.push_back(AddrPriority(candidates->entries[j].correlatedAddr << lBlkSize, 0));
                    }
                } else {
                    next_level_addrs.push_back(AddrPriority(candidates->entries[j].correlatedAddr << lBlkSize, 0));
                }
                
                DPRINTF(TriagePGO, "ISSUE: %x\n", candidates->entries[j].correlatedAddr);
            }
        }
    }
    return issued;
}

} // namespace prefetch
} // namespace gem5
