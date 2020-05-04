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

    class VEdge
    {
        public:
            VEdge(Edge _edge) : edge(std::move(_edge)) {};
            VEdge(VEdge &vedge) : edge(std::move(vedge.edge)) {};
        
        private:
            Edge edge;
    };

    class Vertex   
    {
        public:
            Vertex(N _node) : node(std::move(_node)) {};
            Vertex(Vertex &v) : node(std::move(v.node)) {};
            N& get_CDRT_node() { return node; };    // so it can be reinserted
            std::int32_t get_level() 
            {
                try 
                {  return get_attrib_by_name<std::int32_t>(node, "level"); } 
                catch(const std::exception &e)
                {  std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl; };
                return -1;
            }
            std::string get_type() const { return node.type(); };
            void add_attrib(std::map<string, Attribs> &v, std::string att_name, CRDT::MTypes att_value)
            {
                Attribs av;
                av.type(att_value.index());
                Val value;
                switch(att_value.index()) 
                {
                    case 0:
                        value.str(std::get<std::string>(att_value));
                        av.value( value);
                        break;
                    case 1:
                        value.dec(std::get<std::int32_t>(att_value));
                        av.value( value);
                        break;
                    case 2:
                        value.fl(std::get<float>(att_value));
                        av.value( value);
                        break;
                    case 3:
                        value.float_vec(std::get<std::vector<float>>(att_value));
                        av.value( value);
                        break;
                    case 4:
                        value.rtmat(std::get<RTMat>(att_value).toVector().toStdVector());
                        av.value(value);
                        break;
                }
                v[att_name] = av;
          }
            std::int32_t id() const { return node.id(); };
            std::string type() const { return node.type(); };    
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
                Attribs av = get_attrib_by_name_(n, key);
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
            
            // Edges
            VEdge get_edge(const std::string& to, const std::string& key);
            Edge get_edge(int to, const std::string& key);
            bool insert_or_assign_edge(const VEdge& attrs);
            bool delete_edge(const std::string& t, const std::string& key);
            bool delete_edge(int t, const std::string& key);
            std::vector<VEdge> get_edges_by_type(const std::string& type);
            std::vector<VEdge> get_edges_to_id(int id);

        private:
            N node;
    };




} // namespace CRDT

#endif
