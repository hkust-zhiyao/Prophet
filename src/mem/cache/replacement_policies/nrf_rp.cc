#include "mem/cache/replacement_policies/nrf_rp.hh"

#include <cassert>
#include <memory>
#include "params/NRFRP.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{


NRF::NRF(const Params &p)
  : BRRIP(p) 
{

}

void 
NRF::reset(const std::shared_ptr<ReplacementData>&
            replacement_data, const PacketPtr pkt) 
{
    std::shared_ptr<BRRIPReplData> casted_replacement_data =
        std::static_pointer_cast<BRRIPReplData>(replacement_data);

    // Reset RRPV
    // Replacement data is inserted as "long re-reference" if lower than btp,
    // "distant re-reference" otherwise
    
    if (pkt->isTriped()) {
        casted_replacement_data->rrpv.reset();
    }
    else {
        casted_replacement_data->rrpv.saturate();
        casted_replacement_data->rrpv--;
    }

    casted_replacement_data->valid = true;
}

void
NRF::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    
}


} // namespace replacement_policy
} // namespace gem5