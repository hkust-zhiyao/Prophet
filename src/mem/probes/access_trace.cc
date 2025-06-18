/*
 * Copyright (c) 2013 ARM Limited
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

#include "mem/probes/access_trace.hh"

#include "base/trace.hh"
#include "debug/AccessTrace.hh"
#include "debug/PrefetchIssueTrace.hh"
#include "params/AccessTrace.hh"

namespace gem5
{

void
AccessTrace::AccessListener::notify(const PacketPtr &pkt)
{
    if (pkt->cmd.isDemand() && pkt->req->hasPC() && !prefetch) {
        /*
        if (miss) {
            DPRINTFR(AccessTrace, "1,%x,%x,%x,%x\n", 
                        pkt->req->getPC(), 
                        pkt->getBlockAddr(parent.blkSize),
                        pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0,
                        pkt->req->hasVaddr() ? pkt->req->getVaddr() / 64 : 0);
        } else {
            DPRINTFR(AccessTrace, "0,%x,%x,%x,%x\n", 
                        pkt->req->getPC(), 
                        pkt->getBlockAddr(parent.blkSize),
                        pkt->req->hasVaddr() ? pkt->req->getVaddr() : 0,
                        pkt->req->hasVaddr() ? pkt->req->getVaddr() / 64 : 0);
        }*/
        if (!pkt->isFromPrefetcher()) {
            parent.cnt++;
            if (miss)
                DPRINTFR(AccessTrace, "1,%d,%x,%x\n", 
                            parent.cnt,
                            pkt->req->getPC(),
                            pkt->req->hasVaddr() ? pkt->req->getVaddr() / 64 : 0);
            else
                DPRINTFR(AccessTrace, "0,%d,%x,%x\n", 
                            parent.cnt,
                            pkt->req->getPC(),
                            pkt->req->hasVaddr() ? pkt->req->getVaddr() / 64 : 0);
        }
    }
    if (prefetch) {
        DPRINTFR(PrefetchIssueTrace, "%x\n", pkt->req->hasVaddr() ? pkt->req->getVaddr() / 64 : 0);
    }
}

AccessTrace::AccessTrace(const AccessTraceParams &p)
: ClockedObject(p), listeners(), cache(nullptr), blkSize(0), cnt(0)
{

}

void
AccessTrace::setCache(BaseCache *_cache)
{
    assert(!cache);
    cache = _cache;
    blkSize = cache->getBlockSize();
}

void
AccessTrace::regProbeListeners()
{
    /**
     * If no probes were added by the configuration scripts, connect to the
     * parent cache using the probe "Miss". Also connect to "Hit", if the
     * cache is configured to prefetch on accesses.
     */
    if (listeners.empty() && cache != nullptr) {
        ProbeManager *pm(cache->getProbeManager());
        listeners.push_back(new AccessListener(*this, pm, "Miss", true));
        listeners.push_back(new AccessListener(*this, pm, "Hit", false));
        listeners.push_back(new AccessListener(*this, pm, "Prefetch", false, true));
    }
}

} // namespace gem5
