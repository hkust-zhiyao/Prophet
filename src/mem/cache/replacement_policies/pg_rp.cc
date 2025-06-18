/**
 * Copyright (c) 2018-2020 Inria
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

#include "mem/cache/replacement_policies/pg_rp.hh"

#include <cassert>
#include <memory>

#include "params/PGLRURP.hh"
#include "sim/cur_tick.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

PGLRU::PGLRU(const Params &p)
  : Base(p)
{
}

void
PGLRU::invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Reset last touch timestamp
    std::static_pointer_cast<PGLRUReplData>(
        replacement_data)->lastTouchTick = Tick(0);
}

void
PGLRU::touch(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // Update last touch timestamp
    std::static_pointer_cast<PGLRUReplData>(
        replacement_data)->lastTouchTick = curTick();
    // if (std::static_pointer_cast<PGLRUReplData>(replacement_data)->degree < 4)
    //     std::static_pointer_cast<PGLRUReplData>(
    //     replacement_data)->degree += 1;
}

int
PGLRU::getPGODegree(const std::shared_ptr<ReplacementData>& replacement_data)
{
    // Update last touch timestamp
    return std::static_pointer_cast<PGLRUReplData>(
        replacement_data)->degree;
}

void
PGLRU::setPGODegree(const std::shared_ptr<ReplacementData>& replacement_data, int n)
{
    // Update last touch timestamp
    std::static_pointer_cast<PGLRUReplData>(replacement_data)->degree = n;
}

void
PGLRU::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    panic("Cant train PGLRU's predictor without hint information.");
}

void
PGLRU::reset(const std::shared_ptr<ReplacementData>& replacement_data, int degree)
{
    // Set last touch timestamp
    std::static_pointer_cast<PGLRUReplData>(
        replacement_data)->lastTouchTick = curTick();
    std::static_pointer_cast<PGLRUReplData>(
        replacement_data)->degree = degree;
}

ReplaceableEntry*
PGLRU::getVictim(const ReplacementCandidates& candidates) const
{
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);
    ReplacementCandidates selected;

    int min_degree = 10000;
    for (const auto& candidate : candidates) {
        if (std::static_pointer_cast<PGLRUReplData>(
                    candidate->replacementData)->degree < min_degree)
            min_degree = std::static_pointer_cast<PGLRUReplData>(
                    candidate->replacementData)->degree;
    }
    for (const auto candidate : candidates) {
        if (std::static_pointer_cast<PGLRUReplData>(
                    candidate->replacementData)->degree == min_degree)
            selected.push_back(candidate);
    }

    // Visit all selected candidates to find victim
    ReplaceableEntry* victim = selected[0];
    for (const auto& candidate : selected) {
        // Update victim entry if necessary
        if (std::static_pointer_cast<PGLRUReplData>(
                    candidate->replacementData)->lastTouchTick <
                std::static_pointer_cast<PGLRUReplData>(
                    victim->replacementData)->lastTouchTick) {
            victim = candidate;
        }
    }

    return victim;
}

std::shared_ptr<ReplacementData>
PGLRU::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new PGLRUReplData());
}

} // namespace replacement_policy
} // namespace gem5
