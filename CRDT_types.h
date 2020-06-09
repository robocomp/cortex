//
// Created by juancarlos on 8/6/20.
//

#ifndef CRDT_RTPS_DSR_TESTS_CRDT_NODE_H
#define CRDT_RTPS_DSR_TESTS_CRDT_NODE_H

#include "libs/delta-crdts.cc"
#include <iostream>
#include "unordered_map"
#include "variant"
#include "map"
#include "CRDT.h"

namespace CRDT {

    static constexpr std::array<std::string_view, 7> TYPENAMES_UNION = { "UNINITIALIZED", "STRING", "INT", "FLOAT", "FLOAT_VEC", "BOOL", "BYTE_VEC" };
    using ValType = std::variant<std::string, int32_t, float, std::vector<float>, bool, std::vector<byte>>;
    enum Types : uint32_t
    {
        STRING,
        INT,
        FLOAT,
        FLOAT_VEC,
        BOOL,
        BYTE_VEC
    };


    class Value
    {
        public:

            Value() = default;

            ~Value()= default;

            explicit Value(IDL::Val &&x)
            {

                switch(x._d())
                {
                    case 0:
                        val = std::move(x.str());
                        break;
                    case 1:
                        val = x.dec();
                        break;
                    case 2:
                        val = x.fl();
                        break;
                    case 3:
                        val = std::move(x.float_vec());
                        break;
                    case 4:
                        val = x.bl();
                        break;
                    case 5:
                        val = std::move(x.byte_vec().data());
                        break;
                    default:
                        break;
                }
            }

            Value& operator=(const Value &x)
            {

                if(this == &x) return *this;
                val = x.val;
                return *this;
            }

            Value& operator=(Value &&x)
            {
                if(this == &x) return *this;
                val = std::move(x.val);
                return *this;
            }

            bool operator<(const Value &rhs) const {

                if (val.index() != rhs.selected()) return false;

                switch(val.index()) {
                    case 1:
                        return str() < rhs.str();
                    case 2:
                        return dec() < rhs.dec();
                    case 3:
                        return fl() < rhs.fl();
                    case 4:
                        return float_vec() < rhs.float_vec();
                    case 5:
                        return bl() < rhs.bl();
                    case 6:
                        return byte_vec() < rhs.byte_vec();
                    default:
                        return false;
                }
            }

            bool operator>(const Value &rhs) const {
                return rhs < *this;
            }

            bool operator<=(const Value &rhs) const {
                return !(rhs < *this);
            }

            bool operator>=(const Value &rhs) const {
                return !(*this < rhs);
            }

            bool operator==(const Value &rhs) const {

                if (val.index() != rhs.selected()) return false;
                return val == rhs.val;
            }

            bool operator!=(const Value &rhs) const {
                return !(rhs == *this);
            }

            friend std::ostream &operator<<(std::ostream &os, const Value &type) {

                switch (type.selected()) {
                    case 0:
                        os << "UNINITIALIZED ";
                        break;
                    case 1:
                        os << " str: " << type.str();
                        break;
                    case 2:
                        os << " dec: " << type.dec();
                        break;
                    case 3:
                        os << " float: " << type.fl();
                        break;
                    case 4:
                        os << " float_vec: [ ";
                        for (const auto &k: type.float_vec())
                            os << k << ", ";
                        os << "] ";
                        break;
                    case 5:
                        os << "bool: " << (type.bl() ? " TRUE" : " FALSE");
                        break;
                    case 6:
                        os << " byte_vec: [ ";
                        for (const auto &k: type.byte_vec())
                            os << static_cast<uint8_t >(k) << ", ";
                        os << "] ";
                        break;
                    default:
                        os << "OTRO TIPO";
                        break;
                }
                return os;
            }

            [[nodiscard]] int32_t selected() const
            {
                return val.index();
            }

            std::string& str()
            {
                if (auto pval = std::get_if<std::string>(&val)) {
                    return *pval;
                }
                throw std::runtime_error(("STRING is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());

            }

            [[nodiscard]] const std::string& str() const
            {
                if (auto pval = std::get_if<std::string>(&val)) {
                    return *pval;
                }
                throw std::runtime_error(("STRING is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());

            }

            void dec(int32_t _dec)
            {
                val = _dec;
            }

            [[nodiscard]] int32_t dec() const
            {
                if (auto pval = std::get_if<int32_t>(&val)) {
                    return *pval;
                }
                throw std::runtime_error(("INT is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());

            }

            void fl(float _fl)
            {
                val = _fl;
            }

            [[nodiscard]] float fl() const
            {
                if (auto pval = std::get_if<float>(&val)) {
                    return *pval;
                }

                throw std::runtime_error(("FLOAT is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());
            }

            void float_vec(const std::vector<float> &_float_vec)
            {
                val = _float_vec;
            }

            void float_vec(std::vector<float> &&_float_vec)
            {
                val = std::move(_float_vec);
            }

            [[nodiscard]] const std::vector<float>& float_vec() const
            {
                if (auto pval = std::get_if<vector<float>>(&val)) {
                    return *pval;
                }
                throw std::runtime_error(("VECTOR_FLOAT is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());
            }

            std::vector<float>& float_vec()
            {

                if (auto pval = std::get_if<vector<float>>(&val)) {
                    return *pval;
                }
                throw std::runtime_error(("VECTOR_FLOAT is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());
            }

            void bl(bool _bl)
            {
                val = _bl;
            }

            [[nodiscard]] bool bl() const {

                if (auto pval = std::get_if<bool>(&val)){
                    return *pval;
                }
                throw std::runtime_error(("BOOL is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());
            }

            void byte_vec(const std::vector<byte> &_float_vec)
            {
                val = _float_vec;
            }

            void byte_vec(std::vector<byte> &&_float_vec)
            {
                val = std::move(_float_vec);
            }

            [[nodiscard]] const std::vector<byte>& byte_vec() const
            {
                if (auto pval = std::get_if<vector<byte>>(&val)) {
                    return *pval;
                }
                throw std::runtime_error(("VECTOR_BYTE is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());
            }

            std::vector<byte>& byte_vec()
            {

                if (auto pval = std::get_if<vector<byte >>(&val)) {
                    return *pval;
                }
                throw std::runtime_error(("VECTOR_BYTE is not selected, selected is " + std::string(TYPENAMES_UNION[val.index()])).data());
            }
        private:
            ValType val;
    };


    class Attribute {
        public:

        Attribute()
        {
            m_type = 0;
            m_timestamp = 0;
        }

        ~Attribute()= default;


        explicit Attribute(IDL::Attrib &&x)
        {
            m_type = x.type();
            m_timestamp = x.timestamp();
            m_Value = Value(std::move(x.value()));
        }

        /*Attribute& operator=(const Attrib &x)
        {

            m_type = x.type();
            m_Value = x.Value();
            m_timestamp = x.timestamp();

            return *this;
        }*/

        Attribute& operator=(IDL::Attrib &&x)
        {

            m_type = x.type();
            m_timestamp = x.timestamp();
            m_Value = Value(std::move(x.value()));

            return *this;
        }

            bool operator==(const Attribute &av_) const {
                if (this == &av_) {
                    return true;
                }
                if (type() != av_.type() || val() != av_.val() || timestamp() != av_.timestamp()) {
                    return false;
                }
                return true;
            }
            bool operator<(const Attribute &av_) const {
                if (this == &av_) {
                    return false;
                }
                if (Value() < av_.val()) {
                    return true;
                } else if (av_.val() < Value()) {
                    return false;
                }
                return false;
            }

            bool operator!=(const Attribute &av_) const {
                return !operator==(av_);
            }

            bool operator<=(const Attribute &av_) const {
                return operator<(av_) || operator==(av_);
            }

            bool operator>(const Attribute &av_) const {
                return !operator<(av_) && !operator==(av_);
            }

            bool operator>=(const Attribute &av_) const {
                return !operator<(av_);
            }

            friend std::ostream &operator<<(std::ostream &output, const Attribute &av_) {
                output << "Type: "<<av_.type()<<", Value["<<av_.val()<<"]: "<<av_.val()<<", ";
                return output;
            };



            void type(int32_t _type)
            {
                m_type = _type;
            }


            [[nodiscard]] int32_t type() const
            {
                return m_type;
            }

            void timestamp(uint64_t _time)
            {
                m_timestamp = _time;
            }


            [[nodiscard]] uint64_t timestamp() const
            {
                return m_timestamp;
            }


            void val(IDL::Val &&_Value)
            {
                m_Value = Value(std::move(_Value));
            }

            void val(Value &&_Value)
            {
                m_Value = std::move(_Value);
            }

            [[nodiscard]] const Value& val() const
            {
                return m_Value;
            }

            Value& val()
            {
                return m_Value;
            }

        private:
            int32_t m_type;
            Value m_Value;
            uint64_t m_timestamp;
    };



    class Edge {
        public:



        Edge()
            {
                m_to = 0;
                m_type ="";
                m_from = 0;
            }

            ~Edge()  = default;

            explicit Edge(IDL::Edge &&x)
            {
                m_to = x.to();
                m_type = std::move(x.type());
                m_from = x.from();

                for (auto& [k,v] : x.attrs()) {
                    mvreg<Attribute, int> mv(0);
                    mv.write(Attribute(std::move(v)));
                    m_attrs[k] = mv;
                }
            }

            Edge& operator=(const Edge &x) = default;

            Edge& operator=(Edge &&x) noexcept
            {

                m_to = x.m_to;
                m_type = std::move(x.m_type);
                m_from = x.m_from;
                m_attrs = x.m_attrs;

                return *this;
            }

            bool operator==(const Edge &eA_) const {
                if (this == &eA_) {
                    return true;
                }
                if (m_type != eA_.m_type || from() != eA_.from() || to() != eA_.to() || attrs() != eA_.attrs()) {
                    return false;
                }
                return true;
            }

            bool operator<(const Edge &eA_) const {
                if (this == &eA_) {
                    return false;
                }
                if (m_type < eA_.m_type) {
                    return true;
                } else if (eA_.m_type < m_type) {
                    return false;
                }
                return false;
            }

            bool operator!=(const Edge &eA_) const {
                return !operator==(eA_);
            }

            bool operator<=(const Edge &eA_) const {
                return operator<(eA_) || operator==(eA_);
            }

            bool operator>(const Edge &eA_) const {
                return !operator<(eA_) && !operator==(eA_);
            }

            bool operator>=(const Edge &eA_) const {
                return !operator<(eA_);
            }

            friend std::ostream &operator<<(std::ostream &output,  Edge &ea_) {
                output << "IDL::EdgeAttribs["<<ea_.m_type<<", from:" << ea_.from() << "-> to:"<<ea_.to()<<" Attribs:[";
                for (const auto& v : ea_.attrs())
                    output << v.first <<":"<< v.second <<" - ";
                output<<"]]";
                return output;
            };



        void to(int32_t _to)
        {
            m_to = _to;
        }

        [[nodiscard]] int32_t to() const
        {
            return m_to;
        }


        void type(const std::string &_type)
        {
            m_type = _type;
        }

        void type(std::string &&_type)
        {
            m_type = std::move(_type);
        }

        [[nodiscard]] const std::string& type() const
        {
            return m_type;
        }

        std::string& type()
        {
            return m_type;
        }

        void from(int32_t _from)
        {
            m_from = _from;
        }

        [[nodiscard]] int32_t from() const
        {
            return m_from;
        }

        void attrs(const  std::unordered_map<std::string, mvreg<Attribute, int>> &_attrs)
        {
            m_attrs = _attrs;
        }

        void attrs(std::unordered_map<std::string, mvreg<Attribute, int>> &&_attrs)
        {
            m_attrs = std::move(_attrs);
        }

        const  std::unordered_map<std::string, mvreg<Attribute, int>>& attrs() const
        {
            return m_attrs;
        }


        std::unordered_map<std::string, mvreg<Attribute, int>>& attrs()
        {
            return m_attrs;
        }

        private:
            int32_t m_to;
            std::string m_type;
            int32_t m_from;
            std::unordered_map<std::string, mvreg<Attribute, int>> m_attrs;
    };

    class Node {

        public:

            Node()
            {
                m_type ="";
                m_name ="";
                m_id = 0;
                m_agent_id = 0;
               }

            ~Node()= default;

            explicit Node(IDL::Node &&x)
            {
                m_type = std::move(x.type());
                m_name = std::move(x.name());
                m_id = x.id();
                m_agent_id = x.agent_id();
                for (auto& [k,v] : x.attrs()) {
                    mvreg<Attribute, int> mv(0);
                    mv.write(Attribute(std::move(v)));
                    m_attrs[k] = mv;
                }
                for (auto& [k,v] : x.fano()) {
                    mvreg<Edge, int> mv(0);
                    mv.write(Edge(std::move(v)));
                    m_fano[make_pair(k.to(), k.type())] = mv;
                }
            }

            Node& operator=(const Node &x) = default;

            Node& operator=(Node &&x) noexcept
            {

                m_type = std::move(x.m_type);
                m_name = std::move(x.m_name);
                m_id = x.m_id;
                m_agent_id = x.m_agent_id;
                m_attrs = std::move(x.m_attrs);
                m_fano = std::move(x.m_fano);

                return *this;
            }


            bool operator==(const Node &n_) const {
                if (this == &n_) {
                    return true;
                }
                if (id() != n_.id() || type() != n_.type() || attrs() != n_.attrs() || fano() != n_.fano()) {
                    return false;
                }
                return true;
            }


            bool operator<(const Node &n_) const {
                if (this == &n_) {
                    return false;
                }
                if (id() < n_.id()) {
                    return true;
                } else if (n_.id() < id()) {
                    return false;
                }
                return false;
            }

            bool operator!=(const Node &n_) const {
                return !operator==(n_);
            }

            bool operator<=(const Node &n_) const {
                return operator<(n_) || operator==(n_);
            }

            bool operator>(const Node &n_) const {
                return !operator<(n_) && !operator==(n_);
            }

            bool operator>=(const Node &n_) const {
                return !operator<(n_);
            }

            friend std::ostream &operator<<(std::ostream &output,  Node &n_) {
                output <<"IDL::Node:["<<n_.id()<<"," << n_.name() <<"," << n_.type() <<"], Attribs:[";
                for (const auto& v : n_.attrs())
                    output << v.first <<":("<< v.second <<");";
                output<<"], FanOut:[";
                for (auto& v : n_.fano())
                    output << "[ "<< v.first.first << " "<< v.first.second << "] " <<":("<< v.second <<");";
                output << "]";
                return output;
            }



            void type(const std::string &_type)
            {
                m_type = _type;
            }

            void type(std::string &&_type)
            {
                m_type = std::move(_type);
            }

            [[nodiscard]] const std::string& type() const
            {
                return m_type;
            }

            std::string& type()
            {
                return m_type;
            }

            void name(const std::string &_name)
            {
                m_name = _name;
            }

            void name(std::string &&_name)
            {
                m_name = std::move(_name);
            }

            [[nodiscard]] const std::string& name() const
            {
                return m_name;
            }

            std::string& name()
            {
                return m_name;
            }

            void id(int32_t _id)
            {
                m_id = _id;
            }

            [[nodiscard]] int32_t id() const
            {
                return m_id;
            }

            void agent_id(int32_t _agent_id)
            {
                m_agent_id = _agent_id;
            }

            [[nodiscard]] int32_t agent_id() const
            {
                return m_agent_id;
            }

            void attrs(const std::unordered_map<std::string, mvreg<Attribute, int>> &_attrs)
            {
                m_attrs = _attrs;
            }

            void attrs(std::unordered_map<std::string, mvreg<Attribute, int>> &&_attrs)
            {
                m_attrs = std::move(_attrs);
            }

            const std::unordered_map<std::string, mvreg<Attribute, int>> & attrs() const
            {
                return m_attrs;
            }

            std::unordered_map<std::string, mvreg<Attribute, int>>& attrs()
            {
                return m_attrs;
            }

            void fano(const std::unordered_map<std::pair<int32_t, std::string>, mvreg<Edge, int>, CRDT::pair_hash> &_fano)
            {
                m_fano = _fano;
            }

            void fano(std::unordered_map<std::pair<int32_t, std::string>, mvreg<Edge, int>, CRDT::pair_hash>  &&_fano)
            {
                m_fano = std::move(_fano);
            }

            const std::unordered_map<std::pair<int32_t, std::string>, mvreg<Edge, int>, CRDT::pair_hash> & fano() const
            {
                return m_fano;
            }

            std::unordered_map<std::pair<int32_t, std::string>, mvreg<Edge, int>, CRDT::pair_hash> & fano()
            {
                return m_fano;
            }

            private:
                std::string m_type;
                std::string m_name;
                int32_t m_id;
                int32_t m_agent_id;
                std::unordered_map<std::string, mvreg<Attribute, int>> m_attrs;
                std::unordered_map<std::pair<int32_t, std::string>, mvreg<Edge, int>, CRDT::pair_hash> m_fano;
        };


}

#endif //CRDT_RTPS_DSR_TESTS_CRDT_NODE_H
