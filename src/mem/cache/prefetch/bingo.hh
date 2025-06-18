#ifndef __MEM_CACHE_PREFETCH_BINGO_HH__
#define __MEM_CACHE_PREFETCH_BINGO_HH__

#include <iomanip>

#include "base/types.hh"
#include "mem/cache/prefetch/associative_set.hh"
#include "mem/cache/prefetch/associative_set_impl.hh"
#include "mem/cache/prefetch/queued.hh"
#include "mem/cache/replacement_policies/replaceable_entry.hh"
#include "mem/cache/tags/indexing_policies/set_associative.hh"
#include "mem/packet.hh"
#include "sim/clocked_object.hh"

namespace gem5
{

class BaseIndexingPolicy;
GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{
    class Base;
}
struct BingoPrefetcherParams;

GEM5_DEPRECATED_NAMESPACE(Prefetcher, prefetch);
namespace prefetch
{



class FilterTableEntry  : public TaggedEntry
{
  public:
    Addr key;
    Addr pc;
    int offset;
    FilterTableEntry()
        :TaggedEntry(), key(0), pc(0), offset(0) { }
};

class FilterTable
{
 private:
   AssociativeSet<FilterTableEntry> ft_cache;

  public:
    FilterTable(int filter_table_assoc, int filter_table_entry_count,
                BaseIndexingPolicy*  filter_table_indexing_policy,
                replacement_policy::Base*  filter_table_replacement_policy)
                : ft_cache(filter_table_assoc, filter_table_entry_count,
                  filter_table_indexing_policy,
                  filter_table_replacement_policy)
        { assert(__builtin_popcount(filter_table_entry_count) == 1); }

    FilterTableEntry *find(Addr region_number, bool is_secure) {
        FilterTableEntry *entry = ft_cache.findEntry(region_number, is_secure);
        if (!entry)
            return nullptr;
        ft_cache.accessEntry(entry);
        return entry;
    }

    void insert(Addr region_number, Addr pc, int offset, bool is_secure) {
        assert(!ft_cache.findEntry(region_number, is_secure));
        FilterTableEntry *entry = ft_cache.findVictim(region_number);
        assert(entry != nullptr);
        entry->key = region_number;
        entry->pc = pc;
        entry->offset = offset;
        ft_cache.insertEntry(region_number, is_secure, entry);
        //ft_cache.accessEntry(entry);
    }

    void erase(Addr region_number, bool is_secure){
        FilterTableEntry *entry = ft_cache.findEntry(region_number, is_secure);
        assert(entry);
        ft_cache.invalidate(entry);
    }

};

class AccumulationTableEntry : public TaggedEntry
{
  public:
    Addr key;
    Addr pc;
    int offset;
    std::vector<bool> pattern;
    AccumulationTableEntry()
    :TaggedEntry(), key(0), pc(0), offset(0), pattern({0}){}
};

class AccumulationTable
{
    unsigned pattern_len;
    AssociativeSet<AccumulationTableEntry> at_cache;

  public:
    AccumulationTable(int accumulation_table_assoc,
         int accumulation_table_entry_count,
         BaseIndexingPolicy* accumulation_table_indexing_policy,
         replacement_policy::Base* accumulation_table_replacement_policy,
         unsigned p_len)
        :pattern_len(p_len),
         at_cache(accumulation_table_assoc, accumulation_table_entry_count,
         accumulation_table_indexing_policy,
         accumulation_table_replacement_policy)
     {
        assert(__builtin_popcount(accumulation_table_entry_count) == 1);
        assert(__builtin_popcount(pattern_len) == 1);
    }

    /**
     * @return A return value of false means that
     * the tag wasn't found in the table and true means success.
     */
    bool set_pattern(Addr region_number, int offset, bool is_secure) {
        AccumulationTableEntry *entry =
                at_cache.findEntry(region_number, is_secure);
        if (!entry)
            return false;
        entry->pattern[offset] = true;
        at_cache.accessEntry(entry);
        return true;
    }

    AccumulationTableEntry insert(FilterTableEntry &entry, bool is_secure) {
        assert(!at_cache.findEntry(entry.key, is_secure));
        std::vector<bool> pattern(this->pattern_len, false);
        pattern[entry.offset] = true;
        AccumulationTableEntry* victim = at_cache.findVictim(entry.key);
        assert(victim != nullptr);
        AccumulationTableEntry old_entry = *victim;
        victim->key = entry.key;
        victim->pc = entry.pc;
        victim->offset = entry.offset;
        victim->pattern = pattern;
        assert(victim->replacementData != 0);
        at_cache.insertEntry(entry.key, is_secure, victim);
        return old_entry;
    }

};

class PatternHistoryTableEntry : public TaggedEntry
{
  public:
    Addr key;
    std::vector<bool> pattern;
    PatternHistoryTableEntry()
                :TaggedEntry(), key(0), pattern({0}){}
};
class PatternHistoryTable
{
  private:
    //Event last_event;
    unsigned pattern_len, min_addr_width, max_addr_width, pc_width, index_len;
    int num_sets;
    AssociativeSet<PatternHistoryTableEntry> pht_cache;
  public:
    PatternHistoryTable(int pht_assoc,
                     int pht_entry_count,
                     BaseIndexingPolicy*  pht_indexing_policy,
                     replacement_policy::Base*  pht_replacement_policy,
                     unsigned pattern_len, unsigned min_addr_width,
                     unsigned max_addr_width, unsigned pc_width)
        :pattern_len(pattern_len), min_addr_width(min_addr_width),
            max_addr_width(max_addr_width), pc_width(pc_width),
            num_sets(pht_entry_count/pht_assoc),
            pht_cache(pht_assoc, pht_entry_count,
                pht_indexing_policy,
                pht_replacement_policy)

    {
        assert(this->max_addr_width >= this->min_addr_width);
        assert(this->pc_width + this->min_addr_width > 0);
        assert(__builtin_popcount(pattern_len) == 1);
        this->index_len = __builtin_ctz(num_sets);
    }

    /* address is actually block number */
    void insert(Addr pc, Addr address,
                std::vector<bool> pattern, bool is_secure) {
        assert((int)pattern.size() == this->pattern_len);
        //int offset = address % this->pattern_len;
        //pattern = my_rotate(pattern, -offset);
        Addr key = this->build_key(pc, address);
        PatternHistoryTableEntry *new_entry = pht_cache.findVictim(key);
        assert(new_entry != nullptr);
        new_entry->key = key;
        new_entry->pattern = pattern;
        pht_cache.insertEntry(key, is_secure, new_entry);
    }


    /**
     * @return An un-rotated pattern if match was found,
     * otherwise an empty std::vector.
     * Finds best match and in case of ties, uses the MRU entry.
     */
    std::vector<bool> find(Addr pc, Addr address, bool is_secure) {
        Addr key = this->build_key(pc, address);
        //Addr index = key % this->num_sets;
        Addr tag = key / this->num_sets;
        //auto &set = this->entries[index];
        std::vector<PatternHistoryTableEntry*> set =
                pht_cache.getPossibleEntries(key);
        Addr min_tag_mask = (1 << (this->pc_width + this->min_addr_width
                                         - this->index_len)) - 1;
        Addr max_tag_mask = (1 << (this->pc_width + this->max_addr_width
                                         - this->index_len)) - 1;
        std::vector<std::vector<bool>> min_matches;
        std::vector<bool> pattern;
        for (const auto& location : set) {
            PatternHistoryTableEntry* entry =
                        static_cast<PatternHistoryTableEntry *>(location);
            if (!entry->isValid())
                continue;
            bool min_match = ((entry->getTag() & min_tag_mask)
                                 == (tag & min_tag_mask))
                                && entry->isSecure() == is_secure;
            bool max_match = ((entry->getTag() & max_tag_mask)
                                 == (tag & max_tag_mask))
                                && entry->isSecure() == is_secure;
            std::vector<bool> &cur_pattern = entry->pattern;
            if (max_match) {
                pht_cache.accessEntry(entry);
                pattern = cur_pattern;
                break;
            }
            if (min_match) {
                min_matches.push_back(cur_pattern);
            }
        }
        //this->last_event = PC_ADDRESS;
        if (pattern.empty()) {
            /* no max match was found, time for a vote! */
            pattern = this->vote(min_matches);
            //this->last_event = PC_OFFSET;
        }
        //int offset = address % this->pattern_len;
        //pattern = my_rotate(pattern, +offset);
        return pattern;
    }




  private:
    Addr build_key(Addr pc, Addr address) {
        /* use [pc_width] bits from pc */
        pc &= (1 << this->pc_width) - 1;
        /* use [addr_width] bits from address */
        address &= (1 << this->max_addr_width) - 1;
        Addr offset = address & ((1 << this->min_addr_width) - 1);
        Addr base = (address >> this->min_addr_width);
        /* base + pc + offset */
        Addr key = (base << (this->pc_width + this->min_addr_width))
                        | (pc << this->min_addr_width) | offset;
        /* CRC */
        Addr tag = ((pc << this->min_addr_width) | offset);
        do {
            tag >>= this->index_len;
            key ^= tag & ((1 << this->index_len) - 1);
        } while (tag > 0);
        return key;
    }

    std::vector<bool> vote(const std::vector<std::vector<bool>> &x,
                float thresh = 0.2) {
        int n = x.size();
        std::vector<bool> ret(this->pattern_len, false);
        for (int i = 0; i < n; i += 1)
            assert((int)x[i].size() == this->pattern_len);
        for (int i = 0; i < this->pattern_len; i += 1) {
            int cnt = 0;
            for (int j = 0; j < n; j += 1)
                if (x[j][i])
                    cnt += 1;
            if (1.0 * cnt / n >= thresh)
                ret[i] = true;
        }
        return ret;
    }


};

class Bingo : public Queued
{
    private:
        /** Cacheline size used by the prefetcher using this object */
        //const unsigned block_size;
        const unsigned pc_width;
        const unsigned max_addr_width;
        const unsigned min_addr_width;
        const unsigned region_size;
        const float    pht_vote_threshold;
        const unsigned pattern_len;
         /** Cycles in an epoch period */
        const Cycles epochCycles;

        /** Access map table */
        FilterTable  filter_table;
        AccumulationTable accumulation_table;
        PatternHistoryTable pht;

        /** search pht to find the history pattern of one region number
         *  indexed by pc and address
         * @param pc: the program counter of the memroy access
         * @param address: the memory access address
         */
        std::vector<bool> find_in_phts(Addr pc, Addr address, bool is_secure);

        /**
         * insert one entry into pht         *
         * @param entry: come from accumulation table
         * as a victim or eviction entry
         */
        void insert_in_phts(const AccumulationTableEntry &entry,
                                bool is_secure);


    public:
        Bingo(const BingoPrefetcherParams &p);
        ~Bingo() = default;

        void startup() override;
        void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses) override;
        //void eviction(Addr block_number);
};

} // namespace prefetch
} // namespace gem5
#endif //__MEM_CACHE_PREFETCH_BINGO_HH__