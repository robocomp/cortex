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

            N get(int id);
            Nodes get();

            // Agents methods
            void insert_or_assign(int id, const std::string &type_);
            void insert_or_assign(int id, const N &node);
            bool in(const int &id);
            void add_edge(int from, int to, const std::string &label_);
            void add_node_attribs(int id, const RoboCompDSR::Attribs &att);
            void add_node_attrib(int id, std::string att_name, std::string att_type, std::string att_value, int length);
            void add_node_attrib(int id, std::string att_name, CRDT::MTypes att_value);
            void add_edge_attribs(int from, int to, const RoboCompDSR::Attribs &att);
            void add_edge_attrib(int from, int to, std::string att_name, std::string att_type, std::string att_value, int length);
            void add_edge_attrib(int from, int to, std::string att_name, CRDT::MTypes att_value);
            void replace_node(int id, const N &node);
            RoboCompDSR::AttribValue get_node_attrib_by_name(int id, const std::string &key);
            void join_full_graph(RoboCompDSR::OrMap full_graph);
            void join_delta_node(RoboCompDSR::AworSet aworSet);

            //Initial method
            void read_from_file(const std::string &xml_file_path);

            std::int32_t get_node_level(std::int32_t id);
            std::int32_t get_parent_id(std::int32_t id);
            std::string get_node_type(std::int32_t id);

            // Tools
            void print();
            void print(int id);
            void start_subscription_thread(bool showReceived);
            void start_fullgraph_server_thread();
            void start_fullgraph_request_thread();


        private:
            Nodes nodes; // Main data
            int graph_root;
            bool work;

            std::thread read_thread, request_thread, server_thread; // Threads

            DataStorm::Node node; // Main datastorm node

            std::string agent_name, filter;

            std::shared_ptr<DataStorm::SingleKeyWriter<std::string, RoboCompDSR::AworSet >> writer;
            std::shared_ptr<DataStorm::Topic<std::string, RoboCompDSR::AworSet >> topic;

            void privateCRDTGraph();
            void subscription_thread(bool showReceived);
            void fullgraph_server_thread();
            void fullgraph_request_thread();

            RoboCompDSR::AworSet translateAwCRDTtoICE(int id, aworset<N, int> &data);
            aworset<N, int> translateAwICEtoCRDT(int id, RoboCompDSR::AworSet &data);
            void createIceGraphFromDSRGraph(std::shared_ptr<DSR::Graph> graph);

            void clear();
            int id();
            RoboCompDSR::MapAworSet map();
            RoboCompDSR::DotContext context();

            std::tuple<std::string, std::string, int>  get_type_mtype(const MTypes &t);

        signals:
            void update_node_signal(const std::int32_t, const std::string &type);
            void update_edge_signal(const std::int32_t from, const std::int32_t to, const std::string &ege_tag);
            void update_attrs_signal(const std::int32_t &id, const RoboCompDSR::Attribs &attribs);

    };
}

#endif
