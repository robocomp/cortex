//
// Created by juancarlos on 1/6/21.
//

#include<dsr/core/types/common_types.h>


namespace DSR {

    /////////////////////////////////////////////////////
    /// Attribute — CDR serialization
    ////////////////////////////////////////////////////

    void Attribute::serialize_impl(eprosima::fastcdr::Cdr& cdr) const
    {
        auto disc = static_cast<uint32_t>(m_value.index());
        cdr << disc;
        switch (disc) {
            case 0:  cdr << std::get<std::string>(m_value);             break;
            case 1:  cdr << std::get<int32_t>(m_value);                 break;
            case 2:  cdr << std::get<float>(m_value);                   break;
            case 3:  cdr << std::get<std::vector<float>>(m_value);      break;
            case 4:  cdr << std::get<bool>(m_value);                    break;
            case 5:  cdr << std::get<std::vector<uint8_t>>(m_value);    break;
            case 6:  cdr << std::get<uint32_t>(m_value);                break;
            case 7:  cdr << std::get<uint64_t>(m_value);                break;
            case 8:  cdr << std::get<double>(m_value);                  break;
            case 9:  cdr << std::get<std::vector<uint64_t>>(m_value);   break;
            case 10: {
                const auto& a = std::get<std::array<float,2>>(m_value);
                for (float f : a) cdr << f;
                break;
            }
            case 11: {
                const auto& a = std::get<std::array<float,3>>(m_value);
                for (float f : a) cdr << f;
                break;
            }
            case 12: {
                const auto& a = std::get<std::array<float,4>>(m_value);
                for (float f : a) cdr << f;
                break;
            }
            case 13: {
                const auto& a = std::get<std::array<float,6>>(m_value);
                for (float f : a) cdr << f;
                break;
            }
            default: assert(false);
        }
        cdr << m_timestamp << m_agent_id;
    }

    void Attribute::deserialize_impl(eprosima::fastcdr::Cdr& cdr)
    {
        uint32_t disc = 0;
        cdr >> disc;
        switch (disc) {
            case 0:  { std::string v; cdr >> v; m_value = std::move(v); break; }
            case 1:  { int32_t   v; cdr >> v; m_value = v; break; }
            case 2:  { float     v; cdr >> v; m_value = v; break; }
            case 3:  { std::vector<float>    v; cdr >> v; m_value = std::move(v); break; }
            case 4:  { bool      v; cdr >> v; m_value = v; break; }
            case 5:  { std::vector<uint8_t>  v; cdr >> v; m_value = std::move(v); break; }
            case 6:  { uint32_t  v; cdr >> v; m_value = v; break; }
            case 7:  { uint64_t  v; cdr >> v; m_value = v; break; }
            case 8:  { double    v; cdr >> v; m_value = v; break; }
            case 9:  { std::vector<uint64_t> v; cdr >> v; m_value = std::move(v); break; }
            case 10: { std::array<float,2> a{}; for (float& f : a) cdr >> f; m_value = a; break; }
            case 11: { std::array<float,3> a{}; for (float& f : a) cdr >> f; m_value = a; break; }
            case 12: { std::array<float,4> a{}; for (float& f : a) cdr >> f; m_value = a; break; }
            case 13: { std::array<float,6> a{}; for (float& f : a) cdr >> f; m_value = a; break; }
            default: assert(false);
        }
        cdr >> m_timestamp >> m_agent_id;
    }

    size_t Attribute::serialized_size_impl(eprosima::fastcdr::CdrSizeCalculator& calc, size_t& ca) const
    {
        size_t s = 0;
        uint32_t dummy_u32 = 0;
        uint64_t dummy_u64 = 0;
        int32_t  dummy_i32 = 0;
        float    dummy_f   = 0.f;
        double   dummy_d   = 0.0;
        bool     dummy_b   = false;

        // discriminant
        s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u32, ca);
        switch (m_value.index()) {
            case 0: { const auto& v = std::get<std::string>(m_value);
                      s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), v, ca); break; }
            case 1: s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_i32, ca); break;
            case 2: s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_f,   ca); break;
            case 3: { const auto& v = std::get<std::vector<float>>(m_value);
                      s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), v, ca); break; }
            case 4: s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_b,   ca); break;
            case 5: { const auto& v = std::get<std::vector<uint8_t>>(m_value);
                      s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), v, ca); break; }
            case 6: s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u32, ca); break;
            case 7: s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u64, ca); break;
            case 8: s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_d,   ca); break;
            case 9: { const auto& v = std::get<std::vector<uint64_t>>(m_value);
                      s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), v, ca); break; }
            case 10: for (int i = 0; i < 2; ++i) s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_f, ca); break;
            case 11: for (int i = 0; i < 3; ++i) s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_f, ca); break;
            case 12: for (int i = 0; i < 4; ++i) s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_f, ca); break;
            case 13: for (int i = 0; i < 6; ++i) s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_f, ca); break;
            default: break;
        }
        s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u64, ca); // timestamp
        s += calc.calculate_member_serialized_size(eprosima::fastcdr::MemberId(0), dummy_u32, ca); // agent_id
        return s;
    }

    const Value &Attribute::value() const
    {
        return m_value;
    }

    Value& Attribute::value()
    {
        return m_value;
    }

    uint64_t Attribute::timestamp() const
    {
        return m_timestamp;
    }

    uint32_t Attribute::agent_id() const
    {
        return m_agent_id;
    }

    void Attribute::timestamp(uint64_t t)
    {
        m_timestamp = t;
    }

    void Attribute::value(const Value &value)
    {
        m_value = value;
    }

    void Attribute::value(Value &&value)
    {
        m_value = std::move(value);
    }

    void Attribute::agent_id(uint32_t agentId)
    {
        m_agent_id = agentId;
    }


    [[nodiscard]] std::size_t Attribute::selected() const
    {
        return m_value.index();
    }

    std::string &Attribute::str()
    {
        if (auto pval = std::get_if<std::string>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("STRING is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    [[nodiscard]] const std::string &Attribute::str() const
    {
        if (auto pval = std::get_if<std::string>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("STRING is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    void Attribute::str(const std::string &str)
    {
        m_value = str;
    }

    void Attribute::str(std::string &&str)
    {
        m_value = std::move(str);
    }

    void Attribute::dec(int32_t dec)
    {
        m_value = dec;
    }

    [[nodiscard]] int32_t Attribute::dec() const
    {
        if (auto pval = std::get_if<int32_t>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("INT is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    void Attribute::uint(uint32_t uint)
    {
        m_value = uint;
    }

    [[nodiscard]] uint32_t Attribute::uint() const
    {
        if (auto pval = std::get_if<uint32_t>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("UINT is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    void Attribute::uint64(uint64_t uint)
    {
        m_value = uint;
    }

    [[nodiscard]] uint64_t Attribute::uint64() const
    {
        if (auto pval = std::get_if<uint64_t>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("UINT64 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    void Attribute::fl(float fl)
    {
        m_value = fl;
    }

    [[nodiscard]] float Attribute::fl() const
    {
        if (auto pval = std::get_if<float>(&m_value)) {
            return *pval;
        }

        throw std::runtime_error(
                ("FLOAT is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }

    void Attribute::dob(double dob)
    {
        m_value = dob;
    }

    [[nodiscard]] double Attribute::dob() const
    {
        if (auto pval = std::get_if<double>(&m_value)) {
            return *pval;
        }

        throw std::runtime_error(
                ("DOUBLE is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }

    void Attribute::float_vec(const std::vector<float> &float_vec)
    {
        m_value = float_vec;
    }

    void Attribute::float_vec(std::vector<float> &&float_vec)
    {
        m_value = std::move(float_vec);
    }

    const std::vector<float> &Attribute::float_vec() const
    {
        if (auto pval = std::get_if<std::vector<float>>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VECTOR_FLOAT is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }

    std::vector<float> &Attribute::float_vec()
    {

        if (auto pval = std::get_if<std::vector<float>>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VECTOR_FLOAT is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }

    void Attribute::bl(bool bl)
    {
        m_value = bl;
    }

    [[nodiscard]] bool Attribute::bl() const
    {

        if (auto pval = std::get_if<bool>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("BOOL is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }

    void Attribute::byte_vec(const std::vector<uint8_t> &float_vec)
    {
        m_value = float_vec;
    }

    void Attribute::byte_vec(std::vector<uint8_t> &&float_vec)
    {
        m_value = std::move(float_vec);
    }

    [[nodiscard]] const std::vector<uint8_t> &Attribute::byte_vec() const
    {
        if (auto pval = std::get_if<std::vector<uint8_t>>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VECTOR_BYTE is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }

    std::vector<uint8_t> &Attribute::byte_vec()
    {

        if (auto pval = std::get_if<std::vector<uint8_t >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VECTOR_BYTE is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }



    void Attribute::u64_vec(const std::vector<uint64_t> &uint64_vec)
    {
        m_value = uint64_vec;
    }

    void Attribute::u64_vec(std::vector<uint64_t> &&uint64_vec)
    {
        m_value = std::move(uint64_vec);
    }

    [[nodiscard]] const std::vector<uint64_t> &Attribute::u64_vec() const
    {
        if (auto pval = std::get_if<std::vector<uint64_t >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("U64_VEC is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    std::vector<uint64_t> &Attribute::u64_vec()
    {
        if (auto pval = std::get_if<std::vector<uint64_t >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("U64_VEC is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());
    }



    void Attribute::vec2(const std::array<float, 2> &vec_float2)
    {
        m_value = vec_float2;
    }

    [[nodiscard]] const std::array<float, 2> &Attribute::vec2() const
    {
        if (auto pval = std::get_if<std::array<float, 2 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC2 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    std::array<float, 2> &Attribute::vec2()
    {
        if (auto pval = std::get_if<std::array<float, 2 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC2 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }



    void Attribute::vec3(const std::array<float, 3> &vec_float3)
    {
        m_value = vec_float3;
    }

    [[nodiscard]] const std::array<float, 3> &Attribute::vec3() const
    {
        if (auto pval = std::get_if<std::array<float, 3 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC3 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

    std::array<float, 3> &Attribute::vec3()
    {
        if (auto pval = std::get_if<std::array<float, 3 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC3 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());


    }


    void Attribute::vec4(const std::array<float, 4> &vec_float4)
    {
        m_value = vec_float4;
    }

    [[nodiscard]] const std::array<float, 4> &Attribute::vec4() const
    {
        if (auto pval = std::get_if<std::array<float, 4 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC4 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());


    }

    std::array<float, 4> &Attribute::vec4()
    {
        if (auto pval = std::get_if<std::array<float, 4 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC4 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());


    }


    void Attribute::vec6(const std::array<float, 6> &vec_float6)
    {
        m_value = vec_float6;
    }


    [[nodiscard]] const std::array<float, 6> &Attribute::vec6() const
    {
        if (auto pval = std::get_if<std::array<float, 6 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC6 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());


    }

    std::array<float, 6> &Attribute::vec6()
    {
        if (auto pval = std::get_if<std::array<float, 6 >>(&m_value)) {
            return *pval;
        }
        throw std::runtime_error(
                ("VEC6 is not selected, selected is " + std::string(TYPENAMES_UNION[m_value.index()])).data());

    }

}