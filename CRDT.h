//
// Created by crivac on 17/01/19.
//

#ifndef CRDT_GRAPH
#define CRDT_GRAPH

#include <iostream>
#include <map>
#include <chrono>
#include <thread>
#include "graph.h"
#include "libs/DSRGraph.h"
#include "libs/delta-crdts.cc"
#include <DataStorm/DataStorm.h>
#include <any>
#include <memory>
#include <vector>
#include <variant>
#include <qmat/QMatAll>
#include <typeinfo>


#define NO_PARENT -1
#define TIMEOUT 5

namespace CRDT {

    using N = RoboCompDSR::Node;
    using Nodes = ormap<int, aworset<N, int>, int>;
    using MTypes = std::variant<std::uint32_t, std::int32_t, float, std::string, std::vector<float>, RMat::RTMat>;

    /////////////////////////////////////////////////////////////////
    /// CRDT API
    /////////////////////////////////////////////////////////////////

    class CRDTGraph : public QObject
    {
        Q_OBJECT
        public:
            CRDTGraph(int root, std::string name, std::shared_ptr<DSR::Graph> graph_);
            CRDTGraph(int root, std::string name); // Empty
            ~CRDTGraph();

            void add_edge(int from, int to, const std::string &label_);
            void add_edge_attrib(int from, int to, std::string att_name, CRDT::MTypes att_value);
            void add_edge_attrib(int from, int to, std::string att_name, std::string att_type, std::string att_value, int length);
            void add_edge_attribs(int from, int to, const RoboCompDSR::Attribs &att);

            void add_node_attrib(int id, std::string att_name, CRDT::MTypes att_value);
            void add_node_attrib(int id, std::string att_name, std::string att_type, std::string att_value, int length);
            void add_node_attribs(int id, const RoboCompDSR::Attribs &att);

            Nodes get();
            N get(int id);
            RoboCompDSR::Attribs  get_node_attribs_crdt(int id);
            std::map<std::string, MTypes> get_node_attribs(int id);
            RoboCompDSR::AttribValue get_node_attrib_by_name(int id, const std::string &key);

            template<typename Ta>
            Ta get_node_attrib_by_name(int id, const std::string &key){
                RoboCompDSR::AttribValue av = get_node_attrib_by_name(id, key);
                return icevalue_to_nativetype<Ta>(av.type, av.value);
            }
            RoboCompDSR::EdgeAttribs get_edge_attrib(int from, int to);

            std::tuple<std::string, std::string, int>  mtype_to_icevalue(const MTypes &t);

            // Converts Ice string values into DSRGraph native types
            template<typename Ta>
            Ta icevalue_to_nativetype(const std::string &name, const std::string &val)
            {
                return std::get<Ta>(icevalue_to_mtypes(name,val));
            };
            MTypes icevalue_to_mtypes(const std::string &name, const std::string &val);
            std::int32_t get_node_level(std::int32_t id);
            std::string get_node_type(std::int32_t id);
            std::int32_t get_parent_id(std::int32_t id);

            bool in(const int &id);

            void insert_or_assign(int id, const std::string &type_);
            void insert_or_assign(int id, const N &node);

            void join_delta_node(RoboCompDSR::AworSet aworSet);
            void join_full_graph(RoboCompDSR::OrMap full_graph);

            void print();
            void print(int id);
            std::string printVisitor(const MTypes &t);

            void read_from_file(const std::string &xml_file_path);
            void replace_node(int id, const N &node);

            void start_fullgraph_request_thread();
            void start_fullgraph_server_thread();
            void start_subscription_thread(bool showReceived);


        private:
            Nodes nodes;
            int graph_root;
            bool work;

            std::thread read_thread, request_thread, server_thread; // Threads

            std::string agent_name, filter;

            DataStorm::Node node; // Main datastorm node
            std::shared_ptr<DataStorm::SingleKeyWriter<std::string, RoboCompDSR::AworSet >> writer;
            std::shared_ptr<DataStorm::Topic<std::string, RoboCompDSR::AworSet >> topic;

            void privateCRDTGraph(); // Private constructor

            int id();
            RoboCompDSR::DotContext context();
            RoboCompDSR::MapAworSet map();

            void clear();

            // Threads handlers
            void subscription_thread(bool showReceived);
            void fullgraph_server_thread();
            void fullgraph_request_thread();


            // Translators
            RoboCompDSR::AworSet translateAwCRDTtoICE(int id, aworset<N, int> &data);
            aworset<N, int> translateAwICEtoCRDT(int id, RoboCompDSR::AworSet &data);
            void createIceGraphFromDSRGraph(std::shared_ptr<DSR::Graph> graph);


        signals:
            void update_node_signal(const std::int32_t, const std::string &type);
            void update_edge_signal(const std::int32_t from, const std::int32_t to, const std::string &ege_tag);
            void update_attrs_signal(const std::int32_t &id, const RoboCompDSR::Attribs &attribs);
            void update_edge_attrs_signal(const std::int32_t &id, const std::int32_t);


    };
}

#endif
