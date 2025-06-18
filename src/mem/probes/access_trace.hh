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

#ifndef __MEM_PROBES_ACCESS_TRACE_HH__
#define __MEM_PROBES_ACCESS_TRACE_HH__

#include <cassert>

#include "sim/probe/probe.hh"
#include "sim/clocked_object.hh"
#include "mem/packet.hh"
#include "mem/cache/base.hh"

namespace gem5
{

class BaseCache;
struct AccessTraceParams;

class AccessTrace : public ClockedObject
{

  class AccessListener : public ProbeListenerArgBase<PacketPtr>
  {
    public:
      AccessListener(AccessTrace &_parent, ProbeManager *pm,
                        const std::string &name, bool _miss = false, bool _prefetch = false)
          : ProbeListenerArgBase(pm, name),
            parent(_parent), miss(_miss), prefetch(_prefetch) {}
      void notify(const PacketPtr &pkt) override;
    protected:
      AccessTrace &parent;
      const bool miss;
      const bool prefetch;
  };
  std::vector<AccessListener *> listeners;

  public:
    AccessTrace(const AccessTraceParams &p);
    ~AccessTrace() = default;
    void setCache(BaseCache *_cache);
    void regProbeListeners() override; 

  private:
    BaseCache * cache;
    /** Block size of this cache */
    unsigned blkSize;
    unsigned cnt;
};

} // namespace gem5

#endif
