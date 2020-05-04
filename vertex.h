#ifndef CRDT_VERTEX
#define CRDT_VERTEX

#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <any>
#include <memory>
#include <vector>
#include <variant>
#include <qmat/QMatAll>
#include <typeinfo>
#include "topics/DSRGraphPubSubTypes.h"

namespace CRDT
{
    
    using N = Node;
    using Nodes = ormap<int, aworset<N,  int >, int>;
    using MTypes = std::variant<std::string, std::int32_t, float , std::vector<float>, RMat::RTMat>;
    using IDType = std::int32_t;
    using AttribsMap = std::unordered_map<std::string, MTypes>;

    class Vertex   
    {
        public:
            Vertex(N _node) : node(std::move(_node)) {};
            Vertex(Vertex &v) : node(std::move(v.node)) {};
            N& getNode() { return node; };              // so it can be reinserted
            int id() const { return node.id(); };
            std::string type() const { return node.type(); };
            //int getLevel() const { return getAttrib("level").value()); };
            //int getParentId() const { return std::stoi(getAttrib("parent").value()); };
            
            template <typename T, typename = std::enable_if_t<std::is_same<Node,  T>::value || std::is_same<Edge, T>::value ,T >  >
            Attribs get_attrib_by_name_(const T& n, const std::string &key) 
            {
                try 
                {
                    auto attrs = n.attrs();
                    auto value  = attrs.find(key);
                    if (value != attrs.end())
                        return value->second;
                }
                catch(const std::exception &e)
                {
                    if constexpr (std::is_same<Node,  T>::value)
                        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                                << "-> " << n.id() << std::endl;
                    if constexpr (std::is_same<Attribs,  T>::value)
                        std::cout << "EXCEPTION: " << __FILE__ << " " << __FUNCTION__ << ":" << __LINE__ << " " << e.what()
                                << "-> " << n.to() << std::endl;
                };
                Attribs av;
                av.type(-1);
                Val v;
                v.str("unkown");
                av.value(v);
                return av;
            }
        template <typename Ta, typename Type, typename =  std::enable_if_t<std::is_same<Node,  Type>::value || std::is_same<Edge, Type>::value, Type>>
        Ta get_attrib_by_name(Type& n, const std::string &key) 
        {
            Attribs av = get_attrib_by_name_(node, key);
            bool err = (av.type() == -1);
            if constexpr (std::is_same<Ta, std::string>::value) {
                if (err) return "error";
                return av.value().str();
            }
            if constexpr (std::is_same<Ta, std::int32_t>::value){
                if (err) return -1;
                return av.value().dec();
            }
            if constexpr (std::is_same<Ta, float>::value) {
                if (err) return 0.0;
                return av.value().fl();
            }
            if constexpr (std::is_same<Ta, std::vector<float>>::value)
            {
                if (err) return {};
                if (key == "RT" || key == "RTMat") return av.value().rtmat();
                return av.value().float_vec();
            }
            if constexpr (std::is_same<Ta, RMat::RTMat>::value) {
                if (err) return RTMat();
                return RTMat { QMat{ av.value().rtmat()} } ;
            }
        }
            
        private:
            N node;
    };
} // namespace CRDT

#endif
