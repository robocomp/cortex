/*
Reimplementation from https://github.com/CBaquero/delta-enabled-crdts
*/

#ifndef DELTA_CRDT
#define DELTA_CRDT

#include <iostream>
#include <cassert>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "dsr/core/serialization/serializable.h"
#include "dsr/core/profiling.h"

using key_type = uint64_t;

namespace detail {
template<typename T>
decltype(auto) agent_id_of(T&& value)
{
    if constexpr (requires { std::forward<T>(value).agent_id; }) {
        return std::forward<T>(value).agent_id;
    } else {
        return std::forward<T>(value).agent_id();
    }
}
} // namespace detail

// Autonomous causal context, for context sharing in maps
struct dot_context : public ISerializable<dot_context> {
public:

    std::map<key_type, int> cc; // Compact causal context
    std::set<std::pair<key_type, int> > dc; // Dot cloud

    dot_context() = default;

    dot_context(const dot_context &o) : cc(o.cc), dc(o.dc) {}

    dot_context(dot_context &&o) noexcept : cc(std::move(o.cc)), dc(std::move(o.dc)) {}

    dot_context &operator=(const dot_context &o) {
        if (&o == this) return *this;
        cc = o.cc;
        dc = o.dc;
        return *this;
    }

    dot_context &operator=(dot_context &&o) noexcept {
        if (&o == this) return *this;
        cc = std::move(o.cc);
        dc = std::move(o.dc);
        return *this;
    }

    void setContext(std::map<key_type, int> &&cc_, std::set<std::pair<key_type, int> > &&dc_) {
        cc = std::move(cc_);
        dc = std::move(dc_);
    }

    [[nodiscard]] bool dotin(const std::pair<key_type, int> &d) const {
        const auto itm = cc.find(d.first);
        if (itm != cc.end() && d.second <= itm->second) return true;
        // ⚠DO NOT REMOVE THE NEXT LINE. It is not a correct CRDT predicate — dc is ordered by the PAIR, so
        // rbegin() is the highest-numbered ACTOR's newest dot and its counter has nothing to do with d's
        // actor — but it is LOAD-BEARING. Removing it (tried 2026-08-08) makes dotin() answer "not seen" far
        // more often, so join_replace_conflict() both keeps local dots it should drop and imports remote ones
        // it should not; ds then holds more than one value and `assert(dk.ds.size() <= 1)` in mvreg::join
        // aborts every agent at startup (residual, controller, door). It is masking a genuinely broken causal
        // context: with a healthy cc the FIRST test would already answer true, and this one would never be
        // reached. Fix the context (the gap that strands the dot cloud), not this line.
        if (not dc.empty() and d.second < dc.rbegin()->second) return true;
        if (dc.count(d) != 0) return true;
        return false;
    }

    // Compact DC into CC.
    //
    // ★This is a COMPLEXITY fix, not a semantic one: it produces exactly the same (cc, dc) as the previous
    // full scan, but visits only the dots that can possibly change instead of the entire cloud.
    //
    // WHY IT MATTERS. A dot (a,k) folds into cc only when k == cc[a]+1, and is pruned only when k <= cc[a].
    // Anything else stays in dc. So ONE missing sequence number is permanent damage: every later dot from
    // that actor is stranded for the lifetime of the process, dc grows by one on every incoming delta, and
    // the old scan walked ALL of it on EVERY join — quadratic in the number of deltas received.
    //
    // Measured on a live door_concept, 2026-08-08: cc = { 0 : 228479 }, dc.size() = 317998, main thread
    // pegged at 99% of a core inside this function for 4.5 h. The agent could not be stopped: its Qt event
    // loop never returned, so the SIGINT self-pipe notifier was never serviced and Ctrl-C/SIGTERM were both
    // inert (only SIGKILL, which leaks the agent's owned nodes into the shared graph). Undrained posted
    // events had reached 21 GB, growing 368 MB/min.
    //
    // HOW. dc is ordered by (actor, counter), so one actor's dots are contiguous AND ascending. That lets us
    // jump to each actor's frontier with lower_bound/upper_bound rather than scanning. Cost falls from
    // O(|dc|) per join to O(#actors · log|dc| + #dots actually compacted); #actors is the number of agents.
    //
    // One pass suffices — compacting actor X can never enable compacting actor Y, because cc[X] does not
    // appear in Y's test — which is why the old `do { } while(flag)` outer loop is gone rather than kept.
    void compact() {
        CORTEX_PROFILE_ZONE_N("dot_context::compact");
        constexpr int kMinCounter = std::numeric_limits<int>::min();
        constexpr int kMaxCounter = std::numeric_limits<int>::max();

        auto it = dc.begin();
        while (it != dc.end()) {
            const key_type actor = it->first;
            // One past this actor's last dot. std::set iterators stay valid across erases of OTHER elements,
            // so this remains a good bound while we erase within the actor's range.
            const auto actor_end = dc.upper_bound({actor, kMaxCounter});

            auto mit = cc.find(actor);
            if (mit == cc.end()) {
                // No CC entry yet: only counter 1 can seed the run.
                if (it->second != 1) {
                    it = actor_end;   // stranded above a gap; nothing here can ever compact on its own
                    continue;
                }
                mit = cc.emplace(actor, 1).first;
                it = dc.erase(it);
            } else {
                // Prune every dominated dot (k <= cc[actor]) in a single range erase.
                it = dc.erase(dc.lower_bound({actor, kMinCounter}), dc.upper_bound({actor, mit->second}));
            }

            // Absorb the contiguous run starting at cc[actor]+1.
            while (it != actor_end && it->second == mit->second + 1) {
                ++(mit->second);
                it = dc.erase(it);
            }
            it = actor_end;   // anything still here for this actor sits above a gap
        }
    }

    std::pair<key_type, int> makedot(const key_type &id) {
        // On a valid dot generator, all dots should be compact on the used id
        // Making the new dot, updates the dot generator and returns the dot
        if (auto [it, res] = cc.insert(std::make_pair(id, 1)) ; !res) {
            it->second+=1;
            return *it;
        } else {
            return *it;
        }
    }

    void insertdot(std::pair<key_type, int> &&d, bool compactnow = true) {
        // Set
        dc.emplace(std::move(d));
        if (compactnow) {
            compact();
        }
    }

    void insertdot(const std::pair<key_type, int> &d, bool compactnow = true) {
        // Set
        dc.insert(d);
        if (compactnow) {
            compact();
        }
    }

    void join(const dot_context &o) {
        CORTEX_PROFILE_ZONE_N("dot_context::join");
        if (this == &o) return; // Join is idempotent, but just dont do it.
        // CC
        auto mit = cc.begin();
        auto mito = o.cc.begin();
        do {
            if (mit != cc.end() && (mito == o.cc.end() || mit->first < mito->first)) {
                // entry only at here
                ++mit;
            } else if (mito != o.cc.end() && (mit == cc.end() || mito->first < mit->first)) {
                // entry only at other
                cc.insert(*mito);
                ++mito;
            } else if (mit != cc.end() && mito != o.cc.end()) {
                // in both
                cc.at(mit->first) = std::max(mit->second, mito->second);
                ++mit;
                ++mito;
            }
        } while (mit != cc.end() || mito != o.cc.end());

        // DC
        // Set
        for (const auto &e : o.dc)
            insertdot(e, false);

        compact();
    }

    friend std::ostream &operator<<(std::ostream &output, const dot_context &o) {
        output << "Context:";
        output << " CC ( ";
        for (const auto &ki : o.cc)
            output << ki.first << ":" << ki.second << " ";
        output << ")";
        output << " DC ( ";
        for (const auto &ki : o.dc)
            output << ki.first << ":" << ki.second << " ";
        output << ")";
        return output;
    }

    bool operator==(const dot_context &rhs) const {
        return cc == rhs.cc &&
               dc == rhs.dc;
    }

    bool operator!=(const dot_context &rhs) const {
        return !(rhs == *this);
    }

    bool operator<(const dot_context &rhs) const {
        if (cc < rhs.cc)
            return true;
        if (rhs.cc < cc)
            return false;
        return dc < rhs.dc;
    }

    bool operator>(const dot_context &rhs) const {
        return rhs < *this;
    }

    bool operator<=(const dot_context &rhs) const {
        return !(rhs < *this);
    }

    bool operator>=(const dot_context &rhs) const {
        return !(*this < rhs);
    }

    // ---- ISerializable implementation ----
    void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
    {
        // cc: serialize as sequence of (uint64_t, int32_t) pairs
        auto cc_size = static_cast<uint32_t>(cc.size());
        cdr << cc_size;
        for (const auto& [k, v] : cc) {
            cdr << k;
            cdr << static_cast<int32_t>(v);
        }
        // dc: serialize as sequence of (uint64_t, int32_t) pairs
        auto dc_size = static_cast<uint32_t>(dc.size());
        cdr << dc_size;
        for (const auto& [k, v] : dc) {
            cdr << k;
            cdr << static_cast<int32_t>(v);
        }
    }

    void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
    {
        cc.clear();
        uint32_t cc_size = 0;
        cdr >> cc_size;
        for (uint32_t i = 0; i < cc_size; ++i) {
            key_type k = 0; int32_t v = 0;
            cdr >> k >> v;
            cc.emplace(k, static_cast<int>(v));
        }
        dc.clear();
        uint32_t dc_size = 0;
        cdr >> dc_size;
        for (uint32_t i = 0; i < dc_size; ++i) {
            key_type k = 0; int32_t v = 0;
            cdr >> k >> v;
            dc.emplace(k, static_cast<int>(v));
        }
    }

    size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        size_t s = 0;
        uint32_t dummy_u32 = 0;
        key_type dummy_u64 = 0;
        int32_t dummy_i32 = 0;
        s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u32, ca);
        for (size_t i = 0; i < cc.size(); ++i) {
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u64, ca);
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_i32, ca);
        }
        s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u32, ca);
        for (size_t i = 0; i < dc.size(); ++i) {
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u64, ca);
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_i32, ca);
        }
        return s;
    }
};



template<typename T>
class dot_kernel : public ISerializable<dot_kernel<T>> {
public:

    std::map<std::pair<key_type, int>, T> ds;  // Map of dots to vals
    dot_context c;

    // if no causal context supplied, used base one
    dot_kernel() = default;

    dot_kernel(const dot_kernel &o)
    {
        ds = o.ds;
        c = o.c;
    }

    dot_kernel(dot_kernel &&o) noexcept
    {
        ds = std::move(o.ds);
        c = std::move(o.c);
    }

    dot_kernel<T> &operator=(const dot_kernel<T> &o)
    {
        if (&o == this) return *this;
        ds = o.ds;
        c = o.c;
        return *this;
    }

    dot_kernel<T> &operator=(dot_kernel<T> &&o) noexcept
    {
        if (&o == this) return *this;
        ds = std::move(o.ds);
        c = std::move(o.c);
        return *this;
    }


    void dot_map(std::map<std::pair<key_type, int>, T> &&ds_)
    {
        ds = std::move(ds_);
    }

    void join_replace_conflict(dot_kernel<T> &&o) {
        CORTEX_PROFILE_ZONE_N("dot_kernel::join_replace_conflict");

        if (this == &o) return; // Join is idempotent, but just dont do it.

        // DS
        // will iterate over the two sorted sets to compute join
        auto it = ds.begin();
        auto ito = o.ds.begin();
        do {
            if (it != ds.end() && (ito == o.ds.end() || it->first < ito->first)) {
                // dot only at this
                if (o.c.dotin(it->first)) { // other knows dot, must delete here
                    ds.erase(it++);
                } else {// keep it
                    ++it;
                }
            } else if (ito != o.ds.end() && (it == ds.end() || ito->first < it->first)) {
                // dot only at other
                if (!c.dotin(ito->first) || ds.empty()) { // If I dont know, import
                    ds.insert(std::move(*ito));
                }
                ++ito;
            } else if (it != ds.end() && ito != o.ds.end()) {
                // dot in both
                //replace in case of conflict if the agent id has a lower value
                if (detail::agent_id_of(it->second) > detail::agent_id_of(ito->second) && *it != *ito) {
                    it = ds.erase(it);
                    ds.insert(std::move(*ito));
                } else {
                    ++it;
                }
                ++ito;
            }
        } while (it != ds.end() || ito != o.ds.end());
        // CC
        c.join(std::move(o.c));
    }

    dot_kernel<T> add(key_type &id, const T &val) {

        dot_kernel<T> res;
        // get new dot
        std::pair<key_type , int> dot = c.makedot(id);

        std::pair<std::pair<key_type, int>, T> tmp (dot, val);
        // add under new dot
        ds.insert(tmp);
        // make delta
        res.ds.insert(std::move(tmp));
        res.c.insertdot(dot);
        return res;
    }


    dot_kernel<T> add(key_type &id, T &&val) {

        dot_kernel<T> res;
        // get new dot
        std::pair<key_type , int> dot = c.makedot(id);
        std::pair<std::pair<key_type , int>, T> tmp (dot, std::move(val));
        // add under new dot
        ds.insert(tmp);
        // make delta
        res.ds.insert(std::move(tmp));
        res.c.insertdot(std::move(dot));
        return res;
    }

    dot_kernel<T> rmv()  // remove all dots
    {
        dot_kernel<T> res;
        for (const auto &dv : ds)
            res.c.insertdot(dv.first, false);
        res.c.compact();
        ds.clear(); // Clear the payload, but remember context
        return res;
    }

    friend std::ostream &operator<<(std::ostream &output, const dot_kernel<T> &o) {
        output << "Kernel: DS ( ";
        for (const auto &dv : o.ds) {
            output << dv.first.first << ":" << dv.first.second <<
                   "->" << dv.second << " ";
        }
        output << ") ";

        output << o.c;
        return output;
    }

    bool operator==(const dot_kernel &rhs) const {
        return ds == rhs.ds;
    }

    bool operator!=(const dot_kernel &rhs) const {
        return !(rhs == *this);
    }

    bool operator<(const dot_kernel &rhs) const {
        if (ds < rhs.ds)
            return true;
        return false;
    }

    bool operator>(const dot_kernel &rhs) const {
        return rhs < *this;
    }

    bool operator<=(const dot_kernel &rhs) const {
        return !(rhs < *this);
    }

    bool operator>=(const dot_kernel &rhs) const {
        return !(*this < rhs);
    }

    // ---- ISerializable implementation ----
    void serialize_impl(eprosima::fastcdr::Cdr& cdr) const
    {
        // ds: sequence of (uint64, int32, T) triples
        auto ds_size = static_cast<uint32_t>(ds.size());
        cdr << ds_size;
        for (const auto& [key, val] : ds) {
            cdr << key.first;
            cdr << static_cast<int32_t>(key.second);
            val.serialize(cdr);
        }
        c.serialize(cdr);
    }

    void deserialize_impl(eprosima::fastcdr::Cdr& cdr)
    {
        ds.clear();
        uint32_t ds_size = 0;
        cdr >> ds_size;
        for (uint32_t i = 0; i < ds_size; ++i) {
            key_type kf = 0; int32_t ks = 0;
            cdr >> kf >> ks;
            T val;
            val.deserialize(cdr);
            ds.emplace(std::make_pair(kf, static_cast<int>(ks)), std::move(val));
        }
        c.deserialize(cdr);
    }

    size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        size_t s = 0;
        uint32_t dummy_u32 = 0;
        key_type dummy_u64 = 0;
        int32_t dummy_i32 = 0;
        s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u32, ca);
        for (const auto& [key, val] : ds) {
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u64, ca);
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_i32, ca);
            s += val.serialized_size(calc, ca);
        }
        s += c.serialized_size(calc, ca);
        return s;
    }
};

template<typename V>
class mvreg : public ISerializable<mvreg<V>>   // Multi-value register, Optimized
{
public:
    key_type id;
    dot_kernel<V> dk; // Dot kernel

    mvreg(): id(0) {}

    mvreg(const mvreg &o) {
        dk = o.dk;
        id = o.id;
    }

    mvreg(mvreg &&o) noexcept {
        dk = std::move(o.dk);
        id = o.id;
    }

    mvreg &operator=(const mvreg &o) {
        if (&o == this) return *this;
        dk = o.dk;
        id = o.id;
        return *this;
    }

    mvreg &operator=(mvreg &&o) noexcept {
        if (&o == this) return *this;
        dk = std::move(o.dk);
        id = o.id;
        return *this;
    }

    dot_context &context() {
        return dk.c;
    }

    [[nodiscard]] const dot_context &context() const {
        return dk.c;
    }

    mvreg<V> write(const V &val) {
        CORTEX_PROFILE_ZONE_N("mvreg::write(copy)");
        mvreg<V> r, a;
        r.dk = dk.rmv();
        a.dk = dk.add(id, val);
        r.join(std::move(a));
        assert(r.dk.ds.size() <= 1);
        return r;
    }

    mvreg<V> write(V &&val) {
        CORTEX_PROFILE_ZONE_N("mvreg::write(move)");
        mvreg<V> r, a;
        r.dk = dk.rmv();
        a.dk = dk.add(id, std::move(val));
        r.join(std::move(a));
        assert(r.dk.ds.size() <= 1);
        return r;
    }

    const V &read_reg() const {
        assert(dk.ds.size() >= 1);
        return dk.ds.begin()->second;
    }

    V &read_reg() {
        assert(dk.ds.size() >= 1);
        return dk.ds.begin()->second;
    }

    bool empty() {
        return dk.ds.empty();
    }

    bool empty() const {
        return dk.ds.empty();
    }

    friend std::ostream &operator<<(std::ostream &output, const mvreg<V> &o) {
        output << "MVReg:" << o.dk;
        return output;
    }

    mvreg<V> reset() {
        mvreg<V> r;
        r.dk = dk.rmv();
        return r;
    }

    void join(mvreg<V> &&o) {
        CORTEX_PROFILE_ZONE_N("mvreg::join");
        dk.join_replace_conflict(std::move(o.dk));
        assert(dk.ds.size() <= 1);
    }

    bool operator==(const mvreg &rhs) const {
        return id == rhs.id &&
               dk == rhs.dk;
    }

    bool operator!=(const mvreg &rhs) const {
        return !(rhs == *this);
    }

    bool operator<(const mvreg &rhs) const {
        if (id < rhs.id)
            return true;
        if (rhs.id < id)
            return false;
        return dk < rhs.dk;
    }

    bool operator>(const mvreg &rhs) const {
        return rhs < *this;
    }

    bool operator<=(const mvreg &rhs) const {
        return !(rhs < *this);
    }

    bool operator>=(const mvreg &rhs) const {
        return !(*this < rhs);
    }

    // ---- ISerializable implementation ----
    void serialize_impl(eprosima::fastcdr::Cdr& cdr) const { dk.serialize(cdr); }
    void deserialize_impl(eprosima::fastcdr::Cdr& cdr)     { dk.deserialize(cdr); }
    size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        return dk.serialized_size(calc, ca);
    }
};


#endif
