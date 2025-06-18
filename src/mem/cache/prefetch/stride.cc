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

#include "mem/cache/prefetch/stride.hh"

#include <cassert>

#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "debug/HWPrefetch.hh"
#include "debug/PrefetchTrace.hh"
#include "mem/cache/prefetch/associative_set_impl.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "params/StridePrefetcher.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{

Stride::StrideEntry::StrideEntry(const SatCounter8& init_confidence)
  : TaggedEntry(), confidence(init_confidence)
{
    invalidate();
}

void
Stride::StrideEntry::invalidate()
{
    TaggedEntry::invalidate();
    lastAddr = 0;
    stride = 0;
    confidence.reset();
}

Stride::Stride(const StridePrefetcherParams &p)
  : Queued(p),
    benchmark(p.pgo_benchmark),
    enableRPG(p.enable_rpg),
    initConfidence(p.confidence_counter_bits, p.initial_confidence),
    threshConf(p.confidence_threshold/100.0),
    useRequestorId(p.use_requestor_id),
    degree(p.degree),
    pcTableInfo(p.table_assoc, p.table_entries, p.table_indexing_policy,
        p.table_replacement_policy)
{
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
}

Stride::PCTable*
Stride::findTable(int context)
{
    // Check if table for given context exists
    auto it = pcTables.find(context);
    if (it != pcTables.end())
        return &it->second;

    // If table does not exist yet, create one
    return allocateNewContext(context);
}

Stride::PCTable*
Stride::allocateNewContext(int context)
{
    // Create new table
    auto insertion_result = pcTables.insert(std::make_pair(context,
        PCTable(pcTableInfo.assoc, pcTableInfo.numEntries,
        pcTableInfo.indexingPolicy, pcTableInfo.replacementPolicy,
        StrideEntry(initConfidence))));

    DPRINTF(HWPrefetch, "Adding context %i with stride entries\n", context);

    // Get iterator to new pc table, and then return a pointer to the new table
    return &(insertion_result.first->second);
}

void
Stride::calculatePrefetch(const PrefetchInfo &pfi,
                                    std::vector<AddrPriority> &addresses)
{
    if (!pfi.hasPC()) {
        DPRINTF(HWPrefetch, "Ignoring request with no PC.\n");
        return;
    }

    // Get required packet info
    Addr pf_addr = pfi.getAddr();
    Addr pc = pfi.getPC();
    bool is_secure = pfi.isSecure();
    RequestorID requestor_id = useRequestorId ? pfi.getRequestorId() : 0;

    if (profiledAddrs.find(pc) != profiledAddrs.end()) {
        Addr new_addr = pf_addr + distance;
        addresses.push_back(AddrPriority(new_addr, 0));
    }

    // Get corresponding pc table
    PCTable* pcTable = findTable(requestor_id);

    // Search for entry in the pc table
    StrideEntry *entry = pcTable->findEntry(pc, is_secure);

    if (entry != nullptr) {
        pcTable->accessEntry(entry);

        // Hit in table
        int new_stride = pf_addr - entry->lastAddr;
        bool stride_match = (new_stride == entry->stride);

        // Adjust confidence for stride entry
        if (stride_match && new_stride != 0) {
            entry->confidence++;
        } else {
            entry->confidence--;
            // If confidence has dropped below the threshold, train new stride
            if (entry->confidence.calcSaturation() < threshConf) {
                entry->stride = new_stride;
            }
        }

        DPRINTF(HWPrefetch, "Hit: PC %x pkt_addr %x (%s) stride %d (%s), "
                "conf %d\n", pc, pf_addr, is_secure ? "s" : "ns",
                new_stride, stride_match ? "match" : "change",
                (int)entry->confidence);

        entry->lastAddr = pf_addr;

        // Abort prefetch generation if below confidence threshold
        if (entry->confidence.calcSaturation() < threshConf) {
            return;
        }

        // Generate up to degree prefetches
        for (int d = 1; d <= degree; d++) {
            // Round strides up to atleast 1 cacheline
            int prefetch_stride = new_stride;
            if (abs(new_stride) < blkSize) {
                prefetch_stride = (new_stride < 0) ? -blkSize : blkSize;
            }

            Addr new_addr = pf_addr + d * prefetch_stride;
            addresses.push_back(AddrPriority(new_addr, 0));
        }
    } else {
        // Miss in table
        DPRINTF(HWPrefetch, "Miss: PC %x pkt_addr %x (%s)\n", pc, pf_addr,
                is_secure ? "s" : "ns");

        StrideEntry* entry = pcTable->findVictim(pc);

        // Insert new entry's data
        entry->lastAddr = pf_addr;
        pcTable->insertEntry(pc, is_secure, entry);
    }
}

void
Stride::interpretHintFile(const std::string &hintContent)
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
                    printf("Added dependence: PC %lx -> %lx (%s),\n", 
                            pc, successorPc, successorinstruction.c_str());
                }
            }
        } 
    }
}

void
Stride::notifyFillPrefetchData(const PacketPtr& pkt)
{
    if (!enableRPG)
        return;

    Addr currentPC = pkt->req->getPC();
    Addr offset = pkt->getPfVaddr() & Addr((1 << lBlkSize) - 1);
    uint64_t * data = (uint64_t*)(pkt->getPtr<uint8_t>());

    // Look for this PC in the dependence graph
    if (dependenceGraph.find(currentPC) != dependenceGraph.end()) {
        auto it = dependenceGraph[currentPC];
        DPRINTF(PrefetchTrace, "Train Prefetch -> PC:%lx, addr:%lx, line:%lx\n", pkt->req->getPC(), pkt->getPfVaddr(), pkt->getPfVaddr() >> lBlkSize);
        DPRINTF(PrefetchTrace, "value0:%lx, value1:%lx, value2:%lx, value3:%lx, value4:%lx, value5:%lx, value6:%lx, value7:%lx\n", 
                                *data, *(data+1), *(data+2), *(data+3), *(data+4), *(data+5), *(data+6), *(data+7));

        // Extract the loaded data value - this will be used as the base address for prefetching
        // Convert byte offset to 64-bit word offset
        uint64_t loadedValue = data[offset / 8];
        processInstructionChain(pkt, it, loadedValue, currentPC);
    }
}

void 
Stride::processInstructionChain(const PacketPtr& pkt, 
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
            
            // For load instructions, generate prefetch requests
            issuePrefetch(pkt, addresses, nextPC);
        }
        // No further traversal for load instructions
    }
}

void 
Stride::issuePrefetch(PacketPtr pkt, const std::vector<AddrPriority>& addresses, Addr nextPC)
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

            DPRINTF(PrefetchTrace, "Issue-> PC:%lx, addr:%lx, line_addr:%lx\n", nextPC, addrPrio.first, addrPrio.first >> lBlkSize);
            
            notifyPrecomputation(pkt, pfi, prefetchAddresses);
        }
    }
}

uint32_t
StridePrefetcherHashedSetAssociative::extractSet(const Addr pc) const
{
    const Addr hash1 = pc >> 1;
    const Addr hash2 = hash1 >> tagShift;
    return (hash1 ^ hash2) & setMask;
}

Addr
StridePrefetcherHashedSetAssociative::extractTag(const Addr addr) const
{
    return addr;
}

} // namespace prefetch
} // namespace gem5