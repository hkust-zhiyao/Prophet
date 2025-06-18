#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_NRF_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_NRF_RP_HH__

#include "base/types.hh"
#include "mem/cache/replacement_policies/brrip_rp.hh"

namespace gem5
{

struct NRFRPParams;

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

class NRF : public BRRIP
{
  public:
    typedef NRFRPParams Params;
    NRF(const Params &p);
    ~NRF() = default;

    /**
     * Reset replacement data. Used when an entry is inserted.
     * Sets its last touch tick as the current tick.
     *
     * @param replacement_data Replacement data to be reset.
     */

    void reset(const std::shared_ptr<ReplacementData>&
        replacement_data, const PacketPtr pkt) override; 
        
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
                                                                     override;
};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_NRF_RP_HH__