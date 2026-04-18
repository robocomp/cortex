#pragma once

#include <fastcdr/Cdr.h>
#include <fastcdr/CdrSizeCalculator.hpp>
#include <cstddef>

/**
 * CRTP base for types that can be serialized directly to/from the FastCDR
 * binary wire format.
 *
 * Each Derived class must implement:
 *   void serialize_impl(eprosima::fastcdr::Cdr& cdr) const;
 *   void deserialize_impl(eprosima::fastcdr::Cdr& cdr);
 *   size_t serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& current_alignment) const;
 *
 * CRDTPubSubType<Derived> uses these methods as the FastDDS TopicDataType
 * implementation, replacing the IDL-generated PubSubType classes.
 */
template<typename Derived>
class ISerializable {
public:
    void serialize(eprosima::fastcdr::Cdr& cdr) const
    {
        static_cast<const Derived*>(this)->serialize_impl(cdr);
    }

    void deserialize(eprosima::fastcdr::Cdr& cdr)
    {
        static_cast<Derived*>(this)->deserialize_impl(cdr);
    }

    size_t serialized_size(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& current_alignment) const
    {
        return static_cast<const Derived*>(this)->serialized_size_impl(calc, current_alignment);
    }
};
