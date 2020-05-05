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
            VEdge() { edge.type("error"); };
            int to() const { return edge.to(); };
            int from() const { return edge.from(); };
            EdgeKey get_key() const { EdgeKey key; key.to(edge.to()); key.type(edge.type()); return key; };
            Edge& get_CRDT_edge() { return edge; }; // only for reinserting
            Attrib get_attrib_by_name_(const std::string &key)
            {
                try 
                {
                    auto attrs = edge.attrs();
                    auto value  = attrs.find(key);
                    if (value != attrs.end())
                        return value->second;
                }
                catch(const std::exception &e) { std::cout << e.what() << std::endl; };

                Attrib av;
                av.type(-1);
                Val v;
                v.str("unkown");
                av.value(v);
                return av;
            }
            template <typename Ta>
            Ta get_attrib_by_name(const std::string &key)
            {
                Attrib av = get_attrib_by_name_(key);
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
                    return av.value().float_vec();
                }
                if constexpr (std::is_same<Ta, RMat::RTMat>::value) {
                    auto &at = edge.attrs();
                    return RTMat { at["rot"].value().float_vec()[0], at["rot"].value().float_vec()[1], at["rot"].value().float_vec()[2],
                                   at["trans"].value().float_vec()[0], at["trans"].value().float_vec()[1], at["trans"].value().float_vec()[2]} ;
                }
            }
            
  
            void print()
            {
                std::cout << "------------------------------------" << std::endl;
                std::cout << "Edge-type->" << edge.type() << " from->" << edge.from() << " to->" << edge.to()  << std::endl;
                for(auto [k, v] : edge.attrs())
                    std::cout << "              Key->" << k << " Type->" << v.type() << " Value->" << v.value()  << std::endl;
                std::cout << "------------------------------------" << std::endl;
            }

        private:
            Edge edge;
    };

    using VEdgePtr = std::shared_ptr<VEdge>;

    class Vertex   
    {
        public:
            Vertex(N _node) : node(std::move(_node)) {};
            Vertex(Vertex &v) : node(std::move(v.node)) {};
            N& get_CDRT_node() { return node; };    // so it can be reinserted
            std::int32_t get_level() 
            {
                try 
                {  return get_attrib_by_name<std::int32_t>("level"); } 
                catch(const std::exception &e)
                {  std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl; };
                return -1;
            }
            std::string get_type() const { return node.type(); };
            std::int32_t get_parent()
            {
                try 
                {  return get_attrib_by_name<std::int32_t>("parent"); } 
                catch(const std::exception &e)
                {  std::cout <<"EXCEPTION: "<<__FILE__ << " " << __FUNCTION__ <<":"<<__LINE__<< " "<< e.what() << std::endl; };
                return -1;
            }
            void add_attrib(std::map<string, Attrib> &v, std::string att_name, CRDT::MTypes att_value)
            {
                Attrib av;
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
                }
                v[att_name] = av;
          }
            std::int32_t id() const { return node.id(); };
            std::string type() const { return node.type(); };    
            Attrib get_attrib_by_name_(const std::string &key)
            {
                try 
                {
                    auto attrs = node.attrs();
                    auto value  = attrs.find(key);
                    if (value != attrs.end())
                        return value->second;
                }
                catch(const std::exception &e) { std::cout << e.what() << std::endl; };

                Attrib av;
                av.type(-1);
                Val v;
                v.str("unkown");
                av.value(v);
                return av;
            }
            template <typename Ta>
            Ta get_attrib_by_name(const std::string &key)
            {
                Attrib av = get_attrib_by_name_(key);
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
                    return av.value().float_vec();
                }
                if constexpr (std::is_same<Ta, RMat::RTMat>::value) {
                    auto &at = node.attrs();
                    return RTMat { at["rot"].value().float_vec()[0], at["rot"].value().float_vec()[1], at["rot"].value().float_vec()[2],
                                   at["trans"].value().float_vec()[0], at["trans"].value().float_vec()[1], at["trans"].value().float_vec()[2]} ;
                }
            }
            
            // Edges
            VEdgePtr get_edge(int to, const std::string& key)
            {
                EdgeKey ek;
                ek.to(to);
                ek.type(key);
                auto edge = node.fano().find(ek);
                if (edge != node.fano().end())
                    return std::make_shared<VEdge>(Edge(edge->second));
                else
                    return std::make_shared<VEdge>();
            }
            void insert_or_assign_edge(const VEdgePtr& vedge)
            {
                node.fano().insert_or_assign(vedge->get_key(), vedge->get_CRDT_edge());
            }
            std::vector<VEdgePtr> get_edges()
            {
                std::vector<VEdgePtr> res;
                for(auto &[k,v]: node.fano())
                    res.emplace_back(std::make_shared<VEdge>(v));
                return res;
            }
            std::vector<VEdgePtr> get_edges_by_type(const std::string& type);
            std::vector<VEdgePtr> get_edges_to_id(int id);

            // Utils
            void print()
            {   
                std::cout << "------------------------------------" << std::endl;
                std::cout << "Node-> " << node.id() << std::endl;
                std::cout << "  Type->" << node.type() << std::endl;
                std::cout << "  Name->" << node.name() << std::endl;
                std::cout << "  Agent_id->" << node.agent_id()  << std::endl;
                for(auto [key, val] : node.attrs())
                std::cout << "      Key->" << key << " Type->" << val.type() << " Value->" << val.value()  << std::endl;
                for(auto [key, val] : node.fano())
                {
                    std::cout << "          Edge-type->" << val.type() << " from->" << val.from() << " to->" << val.to()  << std::endl;
                    for(auto [k, v] : val.attrs())
                    std::cout << "              Key->" << k << " Type->" << v.type() << " Value->" << v.value()  << std::endl;
                }
                std::cout << "------------------------------------" << std::endl;
            }

        private:
            N node;
    };




} // namespace CRDT

#endif
