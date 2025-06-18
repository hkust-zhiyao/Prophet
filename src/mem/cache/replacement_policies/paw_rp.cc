#include "mem/cache/replacement_policies/paw_rp.hh"

#include <cassert>
#include <memory>
#include "params/PAWRP.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{


PAW::PAW(const Params &p)
  : BRRIP(p) 
{

}

void 
PAW::prefetchInsertion(const std::shared_ptr<ReplacementData>&
        replacement_data) const
{
    // std::shared_ptr<BRRIPReplData> casted_replacement_data =
    //     std::static_pointer_cast<BRRIPReplData>(replacement_data);

    // casted_replacement_data->rrpv.saturate();
    // casted_replacement_data->rrpv--;
}

void
PAW::touch(const std::shared_ptr<ReplacementData>& 
        replacement_data, const PacketPtr pkt)
{
    //if (!pkt->isFromPrefetcher())
    BRRIP::touch(replacement_data);
}

void
PAW::reset(const std::shared_ptr<ReplacementData>& replacement_data, int degree)
{
    std::shared_ptr<BRRIPReplData> casted_replacement_data =
        std::static_pointer_cast<BRRIPReplData>(replacement_data);

    casted_replacement_data->rrpv.saturate();
    if (degree > 0) {
        casted_replacement_data->rrpv--;
    }
    // Mark entry as ready to be used
    casted_replacement_data->valid = true;
}

void
PAW::reset(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    std::shared_ptr<BRRIPReplData> casted_replacement_data =
        std::static_pointer_cast<BRRIPReplData>(replacement_data);

    casted_replacement_data->rrpv.reset();
    // Mark entry as ready to be used
    casted_replacement_data->valid = true;
}



} // namespace replacement_policy
} // namespace gem5