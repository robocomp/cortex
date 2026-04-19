/*
Reimplementation from https://github.com/CBaquero/delta-enabled-crdts
*/

#ifndef DELTA_CRDT
#define DELTA_CRDT

#include <iostream>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>

#include "dsr/core/serialization/serializable.h"
#include "dsr/core/profiling.h"

using key_type = uint64_t;

// Autonomous causal context, for context sharing in maps
class dot_context : public ISerializable<dot_context> {
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
        if (not dc.empty() and d.second < dc.rbegin()->second) return true;
        if (dc.count(d) != 0) return true;
        return false;
    }

    //TODO: debug this
    void compact() {
        CORTEX_PROFILE_ZONE_N("dot_context::compact");
        // Compact DC to CC if possible
        //typename map<K,int>::iterator mit;
        //typename set<pair<K,int> >::iterator sit;
        bool flag; // may need to compact several times if ordering not best
        do {
            flag = false;
            for (auto sit = dc.begin(); sit != dc.end();) {

                auto mit = cc.find(sit->first);
                if (mit == cc.end()) // No CC entry
                    if (sit->second == 1) // Can compact
                    {
                        cc.insert(*sit);
                        dc.erase(sit++);
                        flag = true;
                    } else ++sit;
                else // there is a CC entry already
                if (sit->second == cc.at(sit->first) + 1) // Contiguous, can compact
                {
                    cc.at(sit->first)++;
                    dc.erase(sit++);
                    flag = true;
                } else if (sit->second <= cc.at(sit->first)) // dominated, so prune
                {
                    dc.erase(sit++);
                    // no extra compaction oportunities so flag untouched
                } else ++sit;
            }
        } while (flag == true);
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
        for (const auto& [k, v] : cc) {
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u64, ca);
            s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_i32, ca);
        }
        s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u32, ca);
        for (const auto& [k, v] : dc) {
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
                if (it->second.agent_id() > ito->second.agent_id() && *it != *ito) {
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
